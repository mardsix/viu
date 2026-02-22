// This example demonstrates a USB mouse device proxy that asynchronously
// records all IN transfer requests to a file for analysis.

// The transfer records are written to `/tmp/usb_transfers.jsonl` by default.
// Each line is a valid JSON object with the following fields:
//
// {
//   "timestamp_ms": 1708444800000,
//   "endpoint": "0x81",
//   "size": 4,
//   "data": "00010203"
// }

// viud proxydev -d <vid>:<pid> \
//     -m $(pwd)/out/build/examples/mouse/libviumouse-proxy.so
//

// Use the provided `parse_transfers.py` script to analyze the recorded
// transfers:
//
// python3 examples/mouse/parse_transfers.py /tmp/usb_transfers.jsonl

#include "usb_mock_abi.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
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
    std::chrono::system_clock::time_point timestamp;
};

class transfer_recorder final {
public:
    explicit transfer_recorder(
        const std::filesystem::path& output_file = "/tmp/usb_transfers.jsonl"
    )
        : output_file_(output_file), shutdown_(false)
    {
        writer_thread_ = std::thread(&transfer_recorder::writer_loop, this);
    }

    ~transfer_recorder()
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            shutdown_ = true;
        }
        queue_cv_.notify_one();
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
    }

    transfer_recorder(const transfer_recorder&) = delete;
    transfer_recorder(transfer_recorder&&) = delete;
    auto operator=(const transfer_recorder&) -> transfer_recorder& = delete;
    auto operator=(transfer_recorder&&) -> transfer_recorder& = delete;

    void record_transfer(const transfer_record& record)
    {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            write_queue_.push(record);
        }
        queue_cv_.notify_one();
    }

private:
    void writer_loop()
    {
        std::ofstream output(output_file_, std::ios::app);
        if (!output.is_open()) {
            return;
        }

        while (true) {
            transfer_record record;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this] {
                    return !write_queue_.empty() || shutdown_;
                });

                if (write_queue_.empty()) {
                    if (shutdown_) {
                        break;
                    }
                    continue;
                }

                record = write_queue_.front();
                write_queue_.pop();
            }

            write_jsonl_record(output, record);
            output.flush();
        }

        output.close();
    }

    static void write_jsonl_record(
        std::ofstream& output,
        const transfer_record& record
    )
    {
        std::ostringstream json;

        auto timestamp_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                record.timestamp.time_since_epoch()
            );

        json << "{"
             << "\"timestamp_ms\":" << timestamp_ms.count() << ","
             << "\"endpoint\":\"0x" << std::hex << std::setfill('0')
             << std::setw(2) << static_cast<int>(record.endpoint) << std::dec
             << "\","
             << "\"size\":" << record.size << ","
             << "\"data\":\"";

        for (const auto byte : record.data) {
            json << std::hex << std::setfill('0') << std::setw(2)
                 << static_cast<int>(byte);
        }
        json << std::dec << "\"";
        json << "}\n";

        output << json.str();
    }

    std::filesystem::path output_file_{};
    std::thread writer_thread_{};
    std::queue<transfer_record> write_queue_{};
    std::mutex queue_mutex_{};
    std::condition_variable queue_cv_{};
    bool shutdown_{};
};

struct mouse_proxy final {
    mouse_proxy() : recorder_(std::make_unique<transfer_recorder>()) {}

    ~mouse_proxy() = default;

    mouse_proxy(const mouse_proxy&) = delete;
    mouse_proxy(mouse_proxy&&) = delete;
    auto operator=(const mouse_proxy&) -> mouse_proxy& = delete;
    auto operator=(mouse_proxy&&) -> mouse_proxy& = delete;

    void on_transfer_request(viu_usb_mock_transfer_control_opaque xfer)
    {
        return;
    }

    void on_transfer_complete(viu_usb_mock_transfer_control_opaque xfer)
    {
        if (xfer.is_in(xfer.ctx)) {
            const auto size = xfer.size(xfer.ctx);
            if (size > 0) {
                auto data = std::vector<std::uint8_t>(
                    static_cast<std::size_t>(size)
                );
                xfer.read(
                    xfer.ctx,
                    data.data(),
                    static_cast<std::uint32_t>(size)
                );

                auto record = transfer_record{
                    .endpoint = xfer.ep(xfer.ctx),
                    .size = static_cast<std::uint32_t>(size),
                    .data = std::move(data),
                    .timestamp = std::chrono::system_clock::now()
                };

                recorder_->record_transfer(record);
            }
        }
    }

    int on_control_setup(
        [[maybe_unused]] libusb_control_setup setup,
        [[maybe_unused]] const std::uint8_t* data,
        [[maybe_unused]] std::size_t data_size,
        [[maybe_unused]] std::uint8_t* out,
        [[maybe_unused]] std::size_t* out_size
    )
    {
        return LIBUSB_SUCCESS;
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

    std::uint64_t tick_interval() const
    {
        return std::chrono::milliseconds{0}.count();
    }

    void tick() {}

private:
    std::unique_ptr<transfer_recorder> recorder_;
};

static_assert(!std::copyable<mouse_proxy>);

} // namespace app

REGISTER_USB_MOCK(mouse_proxy_plugin, app::mouse_proxy)

extern "C" {

void on_plug(plugin_catalog_api* api)
{
    api->set_name(api->ctx, "Proxy HID Devices");
    api->set_version(api->ctx, "1.0.0-beta");

    const auto factory = []() -> viu_usb_mock_opaque* {
        try {
            return mouse_proxy_plugin_create();
        } catch (...) {
            return nullptr;
        }
    };

    api->register_device(api->ctx, "mouse.proxy-1", factory);
}
}
