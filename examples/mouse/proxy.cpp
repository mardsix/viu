#include "libusb.h"

import std;

import viu.plugin.factory;
import viu.plugin.interfaces;
import viu.tickable;
import viu.transfer;
import viu.usb;
import viu.usb.descriptors;

namespace app {

struct mouse_proxy final : viu::usb::device::interface {
    mouse_proxy() = default;

    mouse_proxy(const mouse_proxy&) = delete;
    mouse_proxy(mouse_proxy&&) = delete;
    auto operator=(const mouse_proxy&) -> mouse_proxy& = delete;
    auto operator=(mouse_proxy&&) -> mouse_proxy& = delete;

    void on_transfer_request(viu::usb::transfer::control xfer) override
    {
        return;
    }

    auto on_control_setup(
        [[maybe_unused]] const libusb_control_setup& setup,
        [[maybe_unused]] const std::vector<std::uint8_t>& data
    ) -> std::expected<std::vector<std::uint8_t>, int> override
    {
        return {};
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

    std::chrono::milliseconds interval() const override
    {
        return std::chrono::milliseconds{0};
    }

    void tick() override {}
};

static_assert(!std::copyable<mouse_proxy>);

} // namespace app

extern "C" {
void on_plug(viu::device::plugin::plugin_catalog_api* api)
{
    api->set_name(api->ctx, "HID Devices");
    api->set_version(api->ctx, "1.0.0-beta");

    const auto factory = []() -> viu::usb::device::interface* {
        try {
            return new app::mouse_proxy();
        } catch (...) {
            return nullptr;
        }
    };

    api->register_device(api->ctx, "mouse.proxy-1", factory);
}
}
