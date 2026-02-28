// This example demonstrates a USB device proxy that asynchronously
// records all IN and control transfer requests to a file for
// analysis and playback.

// The transfer records are written to `/tmp/usb_transfers.jsonl` by default.
// Transfer payload bytes are written to `/tmp/usb_transfers.bin`.
// Each line is a valid JSON object with the following fields:
//
// {
//   "timestamp_ms": 1708444800000,
//   "endpoint": "0x81",
//   "size": 4,
//   "data": 123456,
//   "iso_packet_descriptor_count": 32,
//   "iso_packet_descriptor_offset": 12345678
// }

// The control setup records are written to `/tmp/control_setup.jsonl`
// by default.
// Control setup payload bytes are written to `/tmp/control_setup.bin`.
// Each line is a valid JSON object with the following fields:
//
// {
//   "setup": 12345678,
//   "data_size": 4,
//   "data": 123456
// }

// viud proxydev -d <vid>:<pid> \
//     -m $(pwd)/out/build/examples/playback/libviumock-record.so
//

// Use the provided `parse_transfers.py` script to analyze the recorded
// transfers:
//
// python3 examples/playback/parse_transfers.py /tmp/usb_transfers.jsonl
// python3 examples/playback/parse_transfers.py /tmp/control_setup.jsonl

#include "usb_mock_abi.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
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
    std::vector<libusb_iso_packet_descriptor> iso_descriptors;
    std::chrono::system_clock::time_point timestamp;
};

struct control_setup_record {
    std::uint64_t setup;
    std::uint32_t data_size;
    std::vector<std::uint8_t> data;
    std::chrono::system_clock::time_point timestamp;
};

class transfer_recorder final {
public:
    explicit transfer_recorder(
        const std::filesystem::path& output_file = "/tmp/usb_transfers.jsonl"
    )
        : output_file_(output_file),
          payload_file_(
              std::filesystem::path(output_file).replace_extension(".bin")
          ),
          shutdown_(false)
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
        std::ofstream payload(payload_file_, std::ios::binary | std::ios::app);
        if (!output.is_open() || !payload.is_open()) {
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

            payload.seekp(0, std::ios::end);
            const auto payload_pos = payload.tellp();
            if (payload_pos < 0) {
                continue;
            }

            const auto payload_offset = static_cast<std::uint64_t>(payload_pos);

            if (!record.data.empty()) {
                payload.write(
                    reinterpret_cast<const char*>(record.data.data()),
                    static_cast<std::streamsize>(record.data.size())
                );
                payload.flush();
            }

            payload.seekp(0, std::ios::end);
            const auto iso_offset_pos = payload.tellp();
            const auto iso_offset = static_cast<std::uint64_t>(iso_offset_pos);

            if (!record.iso_descriptors.empty()) {
                payload.write(
                    reinterpret_cast<const char*>(
                        record.iso_descriptors.data()
                    ),
                    static_cast<std::streamsize>(
                        record.iso_descriptors.size() *
                        sizeof(libusb_iso_packet_descriptor)
                    )
                );
                payload.flush();
            }

            write_jsonl_record(output, record, payload_offset, iso_offset);
            output.flush();
        }

        payload.close();
        output.close();
    }

    static void write_jsonl_record(
        std::ofstream& output,
        const transfer_record& record,
        std::uint64_t payload_offset,
        std::uint64_t iso_offset
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
             << "\"data\":" << payload_offset << ",";

        if (!record.iso_descriptors.empty()) {
            json << "\"iso_packet_descriptor_count\":"
                 << record.iso_descriptors.size() << ","
                 << "\"iso_packet_descriptor_offset\":" << iso_offset;
        } else {
            json << "\"iso_packet_descriptor_count\":\"NA\","
                 << "\"iso_packet_descriptor_offset\":\"NA\"";
        }

        json << "}\n";

        output << json.str();
    }

    std::filesystem::path output_file_{};
    std::filesystem::path payload_file_{};
    std::thread writer_thread_{};
    std::queue<transfer_record> write_queue_{};
    std::mutex queue_mutex_{};
    std::condition_variable queue_cv_{};
    bool shutdown_{};
};

class control_setup_recorder final {
public:
    explicit control_setup_recorder(
        const std::filesystem::path& output_file = "/tmp/control_setup.jsonl"
    )
        : output_file_(output_file),
          payload_file_(
              std::filesystem::path(output_file).replace_extension(".bin")
          ),
          shutdown_(false)
    {
        writer_thread_ =
            std::thread(&control_setup_recorder::writer_loop, this);
    }

    ~control_setup_recorder()
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

    control_setup_recorder(const control_setup_recorder&) = delete;
    control_setup_recorder(control_setup_recorder&&) = delete;
    auto operator=(const control_setup_recorder&)
        -> control_setup_recorder& = delete;
    auto operator=(control_setup_recorder&&)
        -> control_setup_recorder& = delete;

    void record_setup(const control_setup_record& record)
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
        std::ofstream payload(payload_file_, std::ios::binary | std::ios::app);
        if (!output.is_open() || !payload.is_open()) {
            return;
        }

        while (true) {
            control_setup_record record;
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

            payload.seekp(0, std::ios::end);
            const auto payload_pos = payload.tellp();
            if (payload_pos < 0) {
                continue;
            }

            const auto payload_offset = static_cast<std::uint64_t>(payload_pos);

            if (!record.data.empty()) {
                payload.write(
                    reinterpret_cast<const char*>(record.data.data()),
                    static_cast<std::streamsize>(record.data.size())
                );
                payload.flush();
            }

            write_jsonl_record(output, record, payload_offset);
            output.flush();
        }

        payload.close();
        output.close();
    }

    static void write_jsonl_record(
        std::ofstream& output,
        const control_setup_record& record,
        std::uint64_t payload_offset
    )
    {
        std::ostringstream json;

        json << "{"
             << "\"setup\":" << record.setup << ","
             << "\"data_size\":" << record.data_size << ","
             << "\"data\":" << payload_offset;
        json << "}\n";

        output << json.str();
    }

    std::filesystem::path output_file_{};
    std::filesystem::path payload_file_{};
    std::thread writer_thread_{};
    std::queue<control_setup_record> write_queue_{};
    std::mutex queue_mutex_{};
    std::condition_variable queue_cv_{};
    bool shutdown_{};
};

struct recording_proxy final {
    recording_proxy()
        : recorder_(std::make_unique<transfer_recorder>()),
          control_setup_recorder_(std::make_unique<control_setup_recorder>())
    {
    }

    ~recording_proxy() = default;

    recording_proxy(const recording_proxy&) = delete;
    recording_proxy(recording_proxy&&) = delete;
    auto operator=(const recording_proxy&) -> recording_proxy& = delete;
    auto operator=(recording_proxy&&) -> recording_proxy& = delete;

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

                auto iso_descriptors =
                    std::vector<libusb_iso_packet_descriptor>{};
                const auto iso_count = xfer.iso_packet_descriptor_count(
                    xfer.ctx
                );
                if (iso_count > 0) {
                    iso_descriptors.resize(iso_count);
                    xfer.read_iso_packet_descriptors(
                        xfer.ctx,
                        iso_descriptors.data(),
                        iso_count
                    );
                }

                auto record = transfer_record{
                    .endpoint = xfer.ep(xfer.ctx),
                    .size = static_cast<std::uint32_t>(size),
                    .data = std::move(data),
                    .iso_descriptors = std::move(iso_descriptors),
                    .timestamp = std::chrono::system_clock::now()
                };

                recorder_->record_transfer(record);
            }
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

        if (is_in_control_setup()) {
            auto setup_uint64 = std::bit_cast<std::uint64_t>(setup);

            auto record_data =
                std::vector<std::uint8_t>(data, data + data_size);

            auto record = control_setup_record{
                .setup = setup_uint64,
                .data_size = static_cast<std::uint32_t>(data_size),
                .data = std::move(record_data),
                .timestamp = std::chrono::system_clock::now()
            };

            control_setup_recorder_->record_setup(record);
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
    std::unique_ptr<transfer_recorder> recorder_;
    std::unique_ptr<control_setup_recorder> control_setup_recorder_;
};

static_assert(!std::copyable<recording_proxy>);

} // namespace app

REGISTER_USB_MOCK(recording_proxy_plugin, app::recording_proxy)

extern "C" {

void on_plug(plugin_catalog_api* api)
{
    api->set_name(api->ctx, "Recording Proxy Device");
    api->set_version(api->ctx, "1.0.0-demo");

    const auto factory = []() -> viu_usb_mock_opaque* {
        try {
            return recording_proxy_plugin_create();
        } catch (...) {
            return nullptr;
        }
    };

    api->register_device(api->ctx, "recording.proxy-1", factory);
}
}
