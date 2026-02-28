module;

#include <dlfcn.h>

export module viu.plugin.loader;

import std;

import viu.assert;
import viu.error;
import viu.plugin.catalog;
import viu.plugin.interfaces;
import viu.usb;
import viu.usb.mock.abi;

namespace viu::device::plugin {

extern "C" {
void api_set_name(void* ctx, const char* name);
void api_set_version(void* ctx, const char* version);

void api_register_device(
    void* ctx,
    const char* device_name,
    device_factory_fn f
);
}

export class catalog_loader final {
public:
    catalog_loader() = default;

    explicit catalog_loader(const std::string& file)
    {
        lib_handle_ = ::dlopen(file.c_str(), RTLD_LAZY);

        if (lib_handle_ == nullptr) {
            const char* err = dlerror();
            std::println(
                std::cerr,
                "dlopen failed: {}",
                (err != nullptr ? err : "unknown error")
            );
        }

        viu::_assert(lib_handle_ != nullptr);

        const auto ep = reinterpret_cast<on_plug_fn>(
            dlsym(lib_handle_, ep_symbol_.c_str())
        );

        viu::_assert(ep != nullptr);

        catalog_ = std::make_unique<device::plugin::catalog>();

        auto api = plugin_catalog_api{};
        api.ctx = this;
        api.set_name = &api_set_name;
        api.set_version = &api_set_version;
        api.register_device = &api_register_device;

        ep(&api);
    }

    ~catalog_loader() { this->close(); }

    catalog_loader(catalog_loader const&) = delete;
    catalog_loader(catalog_loader&& rhs) = delete;

    auto operator=(const catalog_loader&) -> catalog_loader& = delete;
    auto operator=(catalog_loader&& rhs) -> catalog_loader& = delete;

    auto catalog() const -> const std::unique_ptr<device::plugin::catalog>&
    {
        viu::_assert(catalog_ != nullptr);
        return catalog_;
    }

    auto device(const std::string& name) -> viu::result<viu_usb_mock_opaque*>
    {
        return catalog()->device(name);
    }

private:
    auto is_open() const noexcept -> bool { return lib_handle_ != nullptr; }

    void close()
    {
        if (is_open()) {
            catalog_.reset();

            ::dlclose(lib_handle_);
            lib_handle_ = nullptr;
        }
    }

    static constexpr auto ep_symbol_ = std::string{"on_plug"};
    void* lib_handle_{};
    std::unique_ptr<::viu::device::plugin::catalog> catalog_{};
};

static_assert(!std::copyable<catalog_loader>);

export void print_catalog_info(
    std::stringstream& ss,
    viu::device::plugin::catalog_interface* catalog
)
{
    std::println(ss, "Catalog Information:");
    std::println(ss, "  Name: {}", catalog->name());
    std::println(ss, "  Version: {}", catalog->version());
    std::println(ss, "  Number of devices: {}", catalog->number_of_devices());

    for (std::size_t n = 0; n < catalog->number_of_devices(); n++) {
        std::println(ss, "    Device {}: {}", n, catalog->device_name(n));
    }
}

export class virtual_device_manager final {
public:
    virtual_device_manager() = default;

    auto register_catalog(const std::string& name)
        -> viu::result<catalog_interface*>
    {
        if (plugins_.find(name) != plugins_.end()) {
            return viu::make_error(
                error::duplicate_catalog,
                std::format("Catalog {} already registered", name)
            );
        }

        plugins_.emplace(name, name);
        return plugins_[name].catalog().get();
    }

    auto device(const std::string& catalog_name, const std::string& device_name)
        -> viu::result<viu_usb_mock_opaque*>
    {
        const auto it = plugins_.find(catalog_name);
        if (it == plugins_.end()) {
            return viu::make_error(error::no_device);
        }
        return it->second.device(device_name);
    }

    auto list_catalogs(std::stringstream& ss) -> void
    {
        std::println(ss, "Registered Catalogs:");
        for (const auto& [name, loader] : plugins_) {
            std::println(ss, "{}:", name);

            const auto catalog = loader.catalog().get();
            print_catalog_info(ss, catalog);
        }
    }

private:
    std::map<std::string, catalog_loader> plugins_{};
};

extern "C" {
void api_set_name(void* ctx, const char* name)
{
    viu::_assert(ctx != nullptr);
    viu::_assert(name != nullptr);

    const auto self = static_cast<catalog_loader*>(ctx);
    self->catalog()->set_name(std::string{name});
}

void api_set_version(void* ctx, const char* version)
{
    viu::_assert(ctx != nullptr);
    viu::_assert(version != nullptr);

    const auto self = static_cast<catalog_loader*>(ctx);
    self->catalog()->set_version(std::string{version});
}

void api_register_device(
    void* ctx,
    const char* device_name,
    device_factory_fn f
)
{
    viu::_assert(ctx != nullptr);
    viu::_assert(device_name != nullptr);
    viu::_assert(f != nullptr);

    const auto self = static_cast<catalog_loader*>(ctx);
    self->catalog()->register_device_factory(std::string{device_name}, f);
}
}

} // namespace viu::device::plugin
