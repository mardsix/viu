export module viu.plugin.interfaces;

import std;

import viu.usb.mock.abi;
import viu.usb;

export namespace viu::device::plugin {

enum class error { no_device };

struct catalog_interface {
    virtual auto name() const -> std::string = 0;
    virtual auto version() const -> std::string = 0;
    virtual auto number_of_devices() const -> std::size_t = 0;
    virtual auto device_name(std::size_t index) const -> std::string = 0;
    virtual auto device(const std::string& name) const
        -> std::expected<viu_usb_mock_opaque*, error> = 0;
};

using on_plug_fn = void (*)(plugin_catalog_api* api);
extern "C" void on_plug(plugin_catalog_api* api);

} // namespace viu::device::plugin
