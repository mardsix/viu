// device.config contains a sample USB mouse device descriptor.
//
// To run this example using a USB mouse descriptor from a device
// connected to your machine, execute:
//
//   ./cli save -d <vid>:<pid> -f $(pwd)/hid.cfg
//   ./cli mock -c $(pwd)/hid.cfg -m $(pwd)/out/build/examples/mouse/libmm.so

#include "libusb.h"

import std;

import viu.boost;
import viu.device.mock;
import viu.plugin.factory;
import viu.plugin.interfaces;
import viu.tickable;
import viu.transfer;
import viu.usb;
import viu.usb.descriptors;

namespace app {

struct mouse_mock final : viu::usb::mock::interface {
    mouse_mock() = default;
    ~mouse_mock() { input_.close(); }

    mouse_mock(const mouse_mock&) = delete;
    mouse_mock(mouse_mock&&) = delete;
    auto operator=(const mouse_mock&) -> mouse_mock& = delete;
    auto operator=(mouse_mock&&) -> mouse_mock& = delete;

    void on_transfer_request(viu::usb::transfer::control xfer) override
    {
        input_.push(xfer);
    }

    auto on_control_setup(
        [[maybe_unused]] const libusb_control_setup& setup,
        [[maybe_unused]] const std::vector<std::uint8_t>& data
    ) -> std::expected<std::vector<std::uint8_t>, int> override
    {
        return std::unexpected{LIBUSB_ERROR_NOT_SUPPORTED};
    }

    auto on_set_configuration([[maybe_unused]] const std::uint8_t index)
        -> int override
    {
        return LIBUSB_SUCCESS;
    }

    auto on_set_interface(
        [[maybe_unused]] const std::uint8_t interface,
        [[maybe_unused]] const std::uint8_t alt_setting
    ) -> int override
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

        auto xfer = viu::usb::transfer::control{};
        if (auto result = input_.try_pull(xfer);
            result == boost::concurrent::queue_op_status::success) {
            xfer.fill(report);
            xfer.complete();
        }
    }

    std::chrono::milliseconds interval() const override
    {
        return std::chrono::milliseconds{250};
    }

    void tick() override { on_key_input('w'); }

private:
    boost::sync_queue<viu::usb::transfer::control> input_{};
};

static_assert(!std::copyable<mouse_mock>);

} // namespace app

extern "C" {
void on_plug(viu::device::plugin::plugin_catalog_api* api)
{
    api->set_name(api->ctx, "HID Devices");
    api->set_version(api->ctx, "1.0.0-beta");

    const auto factory = []() -> viu::usb::mock::interface* {
        try {
            return new app::mouse_mock();
        } catch (...) {
            return nullptr;
        }
    };

    api->register_device(api->ctx, "mouse-1", factory);

    // You can register multiple mock devices, including additional instances
    // of the same type or entirely different devices.
    // Example: register a second mouse device:
    // api->register_device(api->ctx, "mouse-2", factory);
}
}
