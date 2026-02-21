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
#include <vector>

namespace app {

struct mouse_mock final {
    mouse_mock() = default;
    mouse_mock(const mouse_mock&) = delete;
    mouse_mock(mouse_mock&&) = delete;
    auto operator=(const mouse_mock&) -> mouse_mock& = delete;
    auto operator=(mouse_mock&&) -> mouse_mock& = delete;

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

    void on_key_input(const char c)
    {
        auto report = std::vector<std::uint8_t>{0, 0, 0, 0};
        constexpr auto max = std::numeric_limits<std::uint8_t>::max();

        const auto move_left = [&report]() { report.at(1) = max; };
        const auto move_right = [&report]() { report.at(1) = 1; };
        const auto move_up = [&report]() { report.at(2) = max; };
        const auto move_down = [&report]() { report.at(2) = 1; };

        const auto actions = std::map<const char, const std::function<void()>>{
            {'a', move_left},
            {'d', move_right},
            {'w', move_up},
            {'s', move_down}
        };

        if (auto a = actions.find(c); a != actions.end()) {
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

    std::uint64_t tick_interval() const
    {
        return std::chrono::milliseconds{250}.count();
    }

    void tick() { on_key_input('w'); }

private:
    std::queue<viu_usb_mock_transfer_control_opaque> input_;
    std::mutex input_mutex_;
};

static_assert(!std::copyable<mouse_mock>);

} // namespace app

REGISTER_USB_MOCK(mouse_mock_plugin, app::mouse_mock)

extern "C" {

void on_plug(plugin_catalog_api* api)
{
    api->set_name(api->ctx, "HID Devices");
    api->set_version(api->ctx, "1.0.0-beta");

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