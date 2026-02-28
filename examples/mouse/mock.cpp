// device.config contains a sample USB mouse device descriptor.
//
// To run this example using a USB mouse descriptor from a device
// connected to your machine, execute:
//
//   viud save -d <vid>:<pid> -f $(pwd)/hid.cfg
//   viud mock -c $(pwd)/hid.cfg \
//       -m $(pwd)/out/build/examples/mouse/libviumouse-mock.so

#include "usb_mock_abi.hpp"

#include <algorithm>
#include <chrono>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace app {

struct mouse_mock final {
    mouse_mock()
        : tick_thread_([this](const std::stop_token& st) { tick_loop(st); })
    {
    }

    ~mouse_mock()
    {
        tick_thread_.request_stop();
        if (tick_thread_.joinable()) {
            tick_thread_.join();
        }
    }

    mouse_mock(const mouse_mock&) = delete;
    mouse_mock(mouse_mock&&) = delete;
    auto operator=(const mouse_mock&) -> mouse_mock& = delete;
    auto operator=(mouse_mock&&) -> mouse_mock& = delete;

    void on_transfer_request(viu_usb_mock_transfer_control_opaque xfer)
    {
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

    void move(const std::string& direction)
    {
        auto report = std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 0};
        constexpr auto max = std::numeric_limits<std::uint8_t>::max();

        // Bits 0-15: 16 buttons (1 bit each, usage page 0x09) (byte 0, 1)
        // Bits 16-31: X axis (16 bits) (little endian) (byte 2, 3)
        // Bits 32-47: Y axis (16 bits) (little endian) (byte 4, 5)
        // Bits 48-55: Wheel (8 bits) (byte 6)
        // Bits 56-63: Consumer control (8 bits) (byte 7)

        // 00 00 FF FF 00 00 00 00
        const auto move_left = [&report]() {
            report.at(2) = max;
            report.at(3) = max;
        };
        // 00 00 01 00 00 00 00 00
        const auto move_right = [&report]() { report.at(2) = 1; };
        // 00 00 00 00 FF FF 00 00
        const auto move_up = [&report]() {
            report.at(4) = max - 4;
            report.at(5) = max;
        };
        // 00 00 00 00 01 00 00 00
        const auto move_down = [&report]() { report.at(4) = 1; };

        const auto actions = std::map<std::string, const std::function<void()>>{
            {"left", move_left},
            {"right", move_right},
            {"up", move_up},
            {"down", move_down}
        };

        if (auto a = actions.find(direction); a != actions.end()) {
            a->second();
        }

        auto xfer = viu_usb_mock_transfer_control_opaque{};
        {
            std::lock_guard<std::mutex> lock(input_mutex_);
            if (!input_.empty()) {
                xfer = input_.front();
                input_.pop();
                xfer.fill(xfer.ctx, report.data(), report.size());
                xfer.complete(xfer.ctx);
            }
        }
    }

    void tick_loop(const std::stop_token& st)
    {
        while (!st.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!st.stop_requested()) {
                move("up");
            }
        }
    }

private:
    std::queue<viu_usb_mock_transfer_control_opaque> input_;
    std::mutex input_mutex_;
    std::jthread tick_thread_;
};

static_assert(!std::copyable<mouse_mock>);

} // namespace app

REGISTER_USB_MOCK(mouse_mock_plugin, app::mouse_mock)

extern "C" {

void on_plug(plugin_catalog_api* api)
{
    api->set_name(api->ctx, "Virtual HID Devices");
    api->set_version(api->ctx, "1.0.0-demo");

    const auto factory = []() -> viu_usb_mock_opaque* {
        try {
            return mouse_mock_plugin_create();
        } catch (...) {
            return nullptr;
        }
    };

    api->register_device(api->ctx, "mouse-1", factory);

    // You can register multiple mock devices, including additional instances
    // of the same type or entirely different devices.
    // Example:
    // api->register_device(api->ctx, "mouse-2", factory);
}
}