// This example demonstrates a USB mouse device playback that replays
// IN transfers from a recorded jsonl file (produced by proxy.cpp).
//
// The playback reads transfers from `/tmp/usb_transfers.jsonl` by default
// and replays them at intervals based on timestamp differences between
// consecutive records. When the end of the file is reached, playback loops
// back to the beginning.
//
// To use this example:
//   1. First save the device config and run the proxy to record transfers:
//      viud save -d <vid>:<pid> -f $(pwd)/mouse.cfg
//      viud proxydev -d <vid>:<pid> \
//          -m $(pwd)/out/build/examples/mouse/libviumouse-proxy.so
//   2. Interact with the device to generate transfers
//   3. Then run playback to replay the recorded transfers:
//      viud mock \
//          -c $(pwd)/mouse.cfg \
//          -m $(pwd)/out/build/examples/mouse/libviumouse-playback.so

#include "usb_mock_abi.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace app {

struct transfer_record {
    std::uint8_t endpoint;
    std::uint32_t size;
    std::vector<std::uint8_t> data;
    std::uint64_t timestamp_ms;
};

class transfer_playback_engine {
public:
    explicit transfer_playback_engine(
        const std::string& input_file = "/tmp/usb_transfers.jsonl"
    )
    {
        load_transfers(input_file);
    }

    transfer_playback_engine(const transfer_playback_engine&) = delete;
    transfer_playback_engine(transfer_playback_engine&&) = delete;
    auto operator=(const transfer_playback_engine&)
        -> transfer_playback_engine& = delete;
    auto operator=(transfer_playback_engine&&)
        -> transfer_playback_engine& = delete;

    std::optional<transfer_record> get_next_record()
    {
        if (records_.empty()) {
            return std::nullopt;
        }

        if (current_index_ >= records_.size()) {
            current_index_ = 0;
        }

        return records_.at(current_index_);
    }

    void advance()
    {
        if (!records_.empty()) {
            current_index_ = (current_index_ + 1) % records_.size();
        }
    }

    std::uint64_t get_interval_ms()
    {
        if (records_.size() < 2 || current_index_ >= records_.size()) {
            return 100;
        }

        const auto& current = records_.at(current_index_);
        const auto& next = records_.at((current_index_ + 1) % records_.size());

        if (next.timestamp_ms < current.timestamp_ms) {
            return 100;
        }

        const auto diff = next.timestamp_ms - current.timestamp_ms;
        return diff > 0 ? diff : 100;
    }

private:
    void load_transfers(const std::string& filename)
    {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }

            auto record = parse_jsonl_record(line);
            if (record) {
                records_.push_back(*record);
            }
        }

        file.close();
    }

    static std::optional<transfer_record> parse_jsonl_record(
        const std::string& line
    )
    {
        try {
            transfer_record record{};

            const auto ts_pos = line.find("\"timestamp_ms\":");
            if (ts_pos == std::string::npos) {
                return std::nullopt;
            }
            const auto ts_start = ts_pos + 15;
            const auto ts_end = line.find(',', ts_start);
            if (ts_end == std::string::npos) {
                return std::nullopt;
            }
            record.timestamp_ms = std::stoull(
                line.substr(ts_start, ts_end - ts_start)
            );

            const auto ep_pos = line.find("\"endpoint\":");
            if (ep_pos == std::string::npos) {
                return std::nullopt;
            }
            const auto ep_quote_start = line.find("0x", ep_pos);
            if (ep_quote_start == std::string::npos) {
                return std::nullopt;
            }
            const auto ep_quote_end = line.find('"', ep_quote_start);
            if (ep_quote_end == std::string::npos) {
                return std::nullopt;
            }
            const auto ep_str = line.substr(
                ep_quote_start + 2,
                ep_quote_end - ep_quote_start - 2
            );
            record.endpoint = static_cast<std::uint8_t>(
                std::stoul(ep_str, nullptr, 16)
            );

            const auto sz_pos = line.find("\"size\":");
            if (sz_pos == std::string::npos) {
                return std::nullopt;
            }
            const auto sz_start = sz_pos + 7;
            const auto sz_end = line.find(',', sz_start);
            if (sz_end == std::string::npos) {
                return std::nullopt;
            }
            record.size = std::stoul(line.substr(sz_start, sz_end - sz_start));

            const auto data_pos = line.find("\"data\":");
            if (data_pos == std::string::npos) {
                return std::nullopt;
            }
            const auto data_quote_start = line.find('"', data_pos + 7);
            if (data_quote_start == std::string::npos) {
                return std::nullopt;
            }
            const auto data_quote_end = line.find('"', data_quote_start + 1);
            if (data_quote_end == std::string::npos) {
                return std::nullopt;
            }
            const auto hex_str = line.substr(
                data_quote_start + 1,
                data_quote_end - data_quote_start - 1
            );

            for (std::size_t i = 0; i < hex_str.size(); i += 2) {
                const auto byte_str = hex_str.substr(i, 2);
                const auto byte = static_cast<std::uint8_t>(
                    std::stoul(byte_str, nullptr, 16)
                );
                record.data.push_back(byte);
            }

            return record;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::vector<transfer_record> records_{};
    std::size_t current_index_{0};
};

struct mouse_playback final {
    mouse_playback()
        : playback_engine_(std::make_unique<transfer_playback_engine>()),
          shutdown_(false)
    {
        playback_thread_ = std::thread(&mouse_playback::playback_loop, this);
    }

    ~mouse_playback()
    {
        {
            std::lock_guard<std::mutex> lock(playback_mutex_);
            shutdown_ = true;
        }
        playback_cv_.notify_one();
        if (playback_thread_.joinable()) {
            playback_thread_.join();
        }
    }

    mouse_playback(const mouse_playback&) = delete;
    mouse_playback(mouse_playback&&) = delete;
    auto operator=(const mouse_playback&) -> mouse_playback& = delete;
    auto operator=(mouse_playback&&) -> mouse_playback& = delete;

    void on_transfer_request(viu_usb_mock_transfer_control_opaque xfer)
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_.push(xfer);
    }

    int on_control_setup(
        [[maybe_unused]] libusb_control_setup setup,
        [[maybe_unused]] const std::uint8_t* data,
        [[maybe_unused]] std::size_t data_size,
        [[maybe_unused]] std::uint8_t* out,
        [[maybe_unused]] std::size_t* out_size
    )
    {
        return LIBUSB_ERROR_NOT_SUPPORTED;
    }

    int on_set_configuration([[maybe_unused]] std::uint8_t index)
    {
        return LIBUSB_SUCCESS;
    }

    int on_set_interface(
        [[maybe_unused]] std::uint8_t interface,
        [[maybe_unused]] std::uint8_t alt_setting
    )
    {
        return LIBUSB_SUCCESS;
    }

    std::uint64_t tick_interval() const { return 0; }
    void tick() {}

private:
    void playback_loop()
    {
        while (true) {
            transfer_record record;
            {
                std::unique_lock<std::mutex> lock(playback_mutex_);
                if (shutdown_) {
                    break;
                }

                auto current_record = playback_engine_->get_next_record();
                if (!current_record) {
                    playback_cv_.wait_for(lock, std::chrono::milliseconds(100));
                    continue;
                }

                record = *current_record;
            }

            const auto interval_ms = playback_engine_->get_interval_ms();
            {
                std::unique_lock<std::mutex> lock(playback_mutex_);
                if (playback_cv_.wait_for(
                        lock,
                        std::chrono::milliseconds(interval_ms),
                        [this] { return shutdown_; }
                    )) {

                    break;
                }
            }

            auto xfer = viu_usb_mock_transfer_control_opaque{};
            {
                std::lock_guard<std::mutex> lock(input_mutex_);
                if (!input_.empty()) {
                    xfer = input_.front();
                    input_.pop();

                    if (xfer.is_in(xfer.ctx) &&
                        record.endpoint == xfer.ep(xfer.ctx)) {
                        const auto write_size = std::min(
                            static_cast<std::size_t>(xfer.size(xfer.ctx)),
                            record.data.size()
                        );
                        xfer.fill(xfer.ctx, record.data.data(), write_size);
                    }

                    xfer.complete(xfer.ctx);
                }
            }

            playback_engine_->advance();
        }
    }

    std::unique_ptr<transfer_playback_engine> playback_engine_;
    std::queue<viu_usb_mock_transfer_control_opaque> input_;
    std::mutex input_mutex_;
    std::thread playback_thread_;
    std::mutex playback_mutex_;
    std::condition_variable playback_cv_;
    bool shutdown_;
};

static_assert(!std::copyable<mouse_playback>);

} // namespace app

REGISTER_USB_MOCK(mouse_playback_plugin, app::mouse_playback)

extern "C" {

void on_plug(plugin_catalog_api* api)
{
    api->set_name(api->ctx, "Playback HID Devices");
    api->set_version(api->ctx, "1.0.0-beta");

    const auto factory = []() -> viu_usb_mock_opaque* {
        try {
            return mouse_playback_plugin_create();
        } catch (...) {
            return nullptr;
        }
    };

    api->register_device(api->ctx, "mouse.playback-1", factory);
}
}
