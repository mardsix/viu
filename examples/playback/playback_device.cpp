// This example demonstrates a USB device playback that replays
// IN and control transfers from a recorded jsonl file
// (produced by recording_proxy.cpp).
//
// The playback reads transfers from `/tmp/usb_transfers.jsonl` by default
// and payload bytes from `/tmp/usb_transfers.bin`
// and replays them at intervals based on timestamp differences between
// consecutive records. When the end of the file is reached, playback loops
// back to the beginning.
// Contorl setup records are read from `/tmp/control_setup.jsonl` and
// payload bytes from `/tmp/control_setup.bin`.
//
// To use this example:
//   1. First save the device config and run the proxy to record transfers:
//      viud save -d <vid>:<pid> -f $(pwd)/device.cfg
//      viud proxydev -d <vid>:<pid> \
//          -m $(pwd)/out/build/examples/playback/libviumock-record.so
//   2. Interact with the device to generate transfers
//   3. Then run playback to replay the recorded transfers:
//      viud mock \
//          -c $(pwd)/device.cfg \
//          -m $(pwd)/out/build/examples/playback/libviumock-playback.so

#include "usb_mock_abi.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace app {

struct transfer_record {
    std::uint8_t endpoint{};
    std::uint32_t size{};
    std::uint64_t data_offset{};
    std::uint64_t iso_offset{};
    std::vector<std::uint8_t> data{};
    std::vector<libusb_iso_packet_descriptor> iso_descriptors{};
    std::uint64_t timestamp_ms{};
    std::size_t iso_packet_descriptor_count{};
};

struct control_setup_record {
    std::uint64_t setup{};
    std::uint32_t data_size{};
    std::uint64_t data_offset{};
    std::vector<std::uint8_t> data{};
};

class transfer_playback_engine final {
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

    auto get_next_record() -> std::optional<transfer_record>
    {
        if (std::empty(records_)) {
            return std::nullopt;
        }

        if (current_index() >= std::size(records_)) {
            set_index(0);
        }

        return records_.at(current_index());
    }

    void advance()
    {
        if (!std::empty(records_)) {
            set_index(next_index());
        }
    }

    auto get_interval_ms() -> std::uint64_t
    {
        if (std::size(records_) < 2 || current_index() >= std::size(records_)) {
            return 100;
        }

        const auto& current = records_.at(current_index());
        const auto& next = records_.at(next_index());

        if (next.timestamp_ms < current.timestamp_ms) {
            return 100;
        }

        return next.timestamp_ms - current.timestamp_ms;
    }

private:
    void load_transfers(const std::string& filename)
    {
        auto file = std::ifstream{filename};
        if (!file.is_open()) {
            return;
        }

        auto payload_path = std::filesystem::path(filename).replace_extension(
            ".bin"
        );
        auto payload = std::ifstream{payload_path, std::ios::binary};
        if (!payload.is_open()) {
            return;
        }

        auto line = std::string{};
        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }

            auto record = parse_jsonl_record(line);
            if (record) {
                record->data =
                    read_payload(payload, record->data_offset, record->size);
                if (record->data.size() != record->size) {
                    continue;
                }

                if (record->iso_packet_descriptor_count > 0) {
                    record->iso_descriptors = read_iso_descriptors(
                        payload,
                        record->iso_offset,
                        record->iso_packet_descriptor_count
                    );
                }

                records_.push_back(*record);
            }
        }

        payload.close();
        file.close();
    }

    static auto read_payload(
        std::ifstream& payload,
        std::uint64_t offset,
        std::uint32_t size
    ) -> std::vector<std::uint8_t>
    {
        auto bytes = std::vector<std::uint8_t>(size);
        if (size == 0) {
            return bytes;
        }

        payload.clear();
        payload.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!payload.good()) {
            return {};
        }

        payload.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        if (payload.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return {};
        }

        return bytes;
    }

    static auto read_iso_descriptors(
        std::ifstream& payload,
        std::uint64_t offset,
        std::size_t count
    ) -> std::vector<libusb_iso_packet_descriptor>
    {
        auto descriptors = std::vector<libusb_iso_packet_descriptor>(count);
        if (count == 0) {
            return descriptors;
        }

        payload.clear();
        payload.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!payload.good()) {
            return {};
        }

        const auto total_bytes = count * sizeof(libusb_iso_packet_descriptor);
        payload.read(
            reinterpret_cast<char*>(descriptors.data()),
            static_cast<std::streamsize>(total_bytes)
        );
        if (payload.gcount() != static_cast<std::streamsize>(total_bytes)) {
            return {};
        }

        return descriptors;
    }

    static std::optional<transfer_record> parse_jsonl_record(
        const std::string& line
    )
    {
        try {
            auto record = transfer_record{};
            const std::regex re{
                // clang-format off
                R"JSON(\{)JSON"
                R"JSON(\s*"timestamp_ms"\s*:\s*([0-9]+)\s*,)JSON"
                R"JSON(\s*"endpoint"\s*:\s*"0x([0-9a-fA-F]+)"\s*,)JSON"
                R"JSON(\s*"size"\s*:\s*([0-9]+)\s*,)JSON"
                R"JSON(\s*"data"\s*:\s*([0-9]+)\s*,)JSON"
                R"JSON(\s*"iso_packet_descriptor_count"\s*:)JSON"
                    R"JSON(\s*((?:[0-9]+|"NA"))\s*,)JSON"
                R"JSON(\s*"iso_packet_descriptor_offset"\s*:)JSON"
                    R"JSON(\s*((?:[0-9]+|"NA"))\s*\})JSON"
                // clang-format on
            };

            auto match = std::smatch{};
            if (!std::regex_search(line, match, re)) {
                return std::nullopt;
            }

            record.timestamp_ms = std::stoull(match[1].str());
            record.endpoint = static_cast<std::uint8_t>(
                std::stoul(match[2].str(), nullptr, 16)
            );
            record.size = std::stoul(match[3].str());
            record.data_offset = std::stoull(match[4].str());

            const auto& iso_count_str = match[5].str();
            if (iso_count_str == "\"NA\"") {
                record.iso_packet_descriptor_count = 0;
                record.iso_offset = 0;
            } else {
                record.iso_packet_descriptor_count = std::stoull(iso_count_str);
                const auto& iso_offset_str = match[6].str();
                record.iso_offset = iso_offset_str == "\"NA\""
                                        ? 0
                                        : std::stoull(iso_offset_str);
            }

            return record;
        } catch (...) {
            return std::nullopt;
        }
    }

    void set_index(std::size_t index) noexcept { current_index_ = index; }

    auto current_index() const noexcept -> std::size_t
    {
        return current_index_;
    }

    auto next_index() const -> std::size_t
    {
        return (current_index() + 1) % std::size(records_);
    }

    std::vector<transfer_record> records_{};
    std::size_t current_index_{0};
};

static_assert(!std::copyable<transfer_playback_engine>);

class control_setup_playback_engine final {
public:
    explicit control_setup_playback_engine(
        const std::string& input_file = "/tmp/control_setup.jsonl"
    )
    {
        load_control_setups(input_file);
    }

    control_setup_playback_engine(const control_setup_playback_engine&) =
        delete;
    control_setup_playback_engine(control_setup_playback_engine&&) = delete;
    auto operator=(const control_setup_playback_engine&)
        -> control_setup_playback_engine& = delete;
    auto operator=(control_setup_playback_engine&&)
        -> control_setup_playback_engine& = delete;

    auto get_control_setup(std::uint64_t setup) const
        -> std::optional<control_setup_record>
    {
        auto it = setups_.find(setup);
        if (it != setups_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

private:
    void load_control_setups(const std::string& filename)
    {
        auto file = std::ifstream{filename};
        if (!file.is_open()) {
            return;
        }

        auto payload_path = std::filesystem::path(filename).replace_extension(
            ".bin"
        );
        auto payload = std::ifstream{payload_path, std::ios::binary};
        if (!payload.is_open()) {
            return;
        }

        auto line = std::string{};
        while (std::getline(file, line)) {
            if (line.empty()) {
                continue;
            }

            auto record = parse_jsonl_record(line);
            if (record) {
                record->data = read_payload(
                    payload,
                    record->data_offset,
                    record->data_size
                );
                if (record->data.size() != record->data_size) {
                    continue;
                }
                setups_[record->setup] = *record;
            }
        }

        payload.close();
        file.close();
    }

    static auto read_payload(
        std::ifstream& payload,
        std::uint64_t offset,
        std::uint32_t size
    ) -> std::vector<std::uint8_t>
    {
        auto bytes = std::vector<std::uint8_t>(size);
        if (size == 0) {
            return bytes;
        }

        payload.clear();
        payload.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!payload.good()) {
            return {};
        }

        payload.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        if (payload.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return {};
        }

        return bytes;
    }

    static std::optional<control_setup_record> parse_jsonl_record(
        const std::string& line
    )
    {
        try {
            auto record = control_setup_record{};
            const std::regex re{R"JSON(\{)JSON"
                                R"JSON(\s*"setup"\s*:\s*([0-9]+)\s*,)JSON"
                                R"JSON(\s*"data_size"\s*:\s*([0-9]+)\s*,)JSON"
                                R"JSON(\s*"data"\s*:\s*([0-9]+)\s*\})JSON"};

            auto match = std::smatch{};
            if (!std::regex_search(line, match, re)) {
                return std::nullopt;
            }

            record.setup = std::stoull(match[1].str());
            record.data_size = std::stoul(match[2].str());
            record.data_offset = std::stoull(match[3].str());

            return record;
        } catch (...) {
            return std::nullopt;
        }
    }

    std::map<std::uint64_t, control_setup_record> setups_{};
};

static_assert(!std::copyable<control_setup_playback_engine>);

struct playback_device final {
    playback_device()
        : playback_engine_(std::make_unique<transfer_playback_engine>()),
          control_setup_engine_(
              std::make_unique<control_setup_playback_engine>()
          ),
          shutdown_(false)
    {
        playback_thread_ = std::thread(&playback_device::playback_loop, this);
    }

    ~playback_device()
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

    playback_device(const playback_device&) = delete;
    playback_device(playback_device&&) = delete;
    auto operator=(const playback_device&) -> playback_device& = delete;
    auto operator=(playback_device&&) -> playback_device& = delete;

    void on_transfer_request(viu_usb_mock_transfer_control_opaque xfer)
    {
        // TODO: Playback all endpoints
        if (xfer.ep(xfer.ctx) == 0x81) {
            std::lock_guard<std::mutex> lock(input_mutex_);
            input_.push(xfer);
        }
    }

    int on_control_setup(
        [[maybe_unused]] libusb_control_setup setup,
        [[maybe_unused]] std::uint8_t* data,
        [[maybe_unused]] std::size_t data_size,
        [[maybe_unused]] int result
    )
    {
        const auto is_in_control_setup = [&]() {
            return (setup.bmRequestType & 0x80) != 0;
        };

        if (!is_in_control_setup()) {
            return data_size;
        }

        auto setup_uint64 = std::bit_cast<std::uint64_t>(setup);
        auto recorded = control_setup_engine_->get_control_setup(setup_uint64);

        if (recorded && data && data_size > 0) {
            const auto copy_size = std::min(
                data_size,
                static_cast<std::size_t>(recorded->data_size)
            );

            std::memcpy(data, recorded->data.data(), copy_size);
            return copy_size;
        }

        return result;
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

private:
    void playback_loop()
    {
        while (true) {
            transfer_record record;
            {
                auto lock = std::unique_lock<std::mutex>{playback_mutex_};
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
                auto lock = std::unique_lock<std::mutex>{playback_mutex_};
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
                auto lock = std::unique_lock<std::mutex>{input_mutex_};
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

                        if (!record.iso_descriptors.empty()) {
                            xfer.fill_iso_packet_descriptors(
                                xfer.ctx,
                                record.iso_descriptors.data(),
                                record.iso_descriptors.size()
                            );
                        }
                    }

                    xfer.complete(xfer.ctx);
                }
            }

            playback_engine_->advance();
        }
    }

    std::unique_ptr<transfer_playback_engine> playback_engine_{};
    std::unique_ptr<control_setup_playback_engine> control_setup_engine_{};
    std::queue<viu_usb_mock_transfer_control_opaque> input_{};
    std::mutex input_mutex_{};
    std::thread playback_thread_{};
    std::mutex playback_mutex_{};
    std::condition_variable playback_cv_{};
    bool shutdown_{};
};

static_assert(!std::copyable<playback_device>);

} // namespace app

REGISTER_USB_MOCK(playback_plugin, app::playback_device)

extern "C" {

void on_plug(plugin_catalog_api* api)
{
    api->set_name(api->ctx, "Virtual Playback Device");
    api->set_version(api->ctx, "1.0.0-demo");

    const auto factory = []() -> viu_usb_mock_opaque* {
        try {
            return playback_plugin_create();
        } catch (...) {
            return nullptr;
        }
    };

    api->register_device(api->ctx, "viu.playback-1", factory);
}
}
