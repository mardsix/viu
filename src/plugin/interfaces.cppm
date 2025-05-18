export module viu.plugin.interfaces;

import std;

import viu.usb;

export namespace viu::device::plugin {

enum class error { no_device };

struct catalog_interface {
    virtual auto name() const -> std::string = 0;
    virtual auto version() const -> std::string = 0;
    virtual auto number_of_devices() const -> std::size_t = 0;
    virtual auto device_name(std::size_t index) const -> std::string = 0;
    virtual auto device(const std::string& name) const
        -> std::expected<std::shared_ptr<viu::usb::mock::interface>, error> = 0;
};

using device_factory_fn = viu::usb::mock::interface* (*)();

using set_name_fn = void (*)(void* ctx, const char* name);
using set_version_fn = void (*)(void* ctx, const char* version);
using register_device_fn =
    void (*)(void* ctx, const char* device_name, device_factory_fn factory);

struct plugin_catalog_api {
    void* ctx;
    set_name_fn set_name;
    set_version_fn set_version;
    register_device_fn register_device;
};

using on_plug_fn = void (*)(plugin_catalog_api* api);

extern "C" void on_plug(plugin_catalog_api* api);

} // namespace viu::device::plugin
