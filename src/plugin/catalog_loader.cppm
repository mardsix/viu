module;

#include <dlfcn.h>

export module viu.plugin.loader;

import std;

import viu.assert;
import viu.plugin.interfaces;
import viu.usb;
import viu.plugin.factory;

export namespace viu::device::plugin {

class catalog_loader final {
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

        local_catalog_ = std::make_unique<::viu::device::plugin::catalog>();
        catalog_iface_ = local_catalog_.get();

        auto api = plugin_catalog_api{};
        api.ctx = this;
        api.set_name = &catalog_loader::api_set_name;
        api.set_version = &catalog_loader::api_set_version;
        api.register_device = &catalog_loader::api_register_device;

        ep(&api);
    }

    ~catalog_loader() { this->close(); }

    catalog_loader(catalog_loader const&) = delete;
    catalog_loader(catalog_loader&& rhs) = delete;

    auto operator=(const catalog_loader&) -> catalog_loader& = delete;
    auto operator=(catalog_loader&& rhs) -> catalog_loader& = delete;

    auto catalog() const -> catalog_interface* { return catalog_iface_; }

    auto device(const std::string& name)
        -> std::expected<std::shared_ptr<viu::usb::mock::interface>, error>
    {
        return catalog_iface_->device(name);
    }

private:
    auto is_open() const noexcept -> bool { return lib_handle_ != nullptr; }

    void close()
    {
        if (is_open()) {
            ::dlclose(lib_handle_);
            lib_handle_ = nullptr;
        }
    }

    static constexpr auto ep_symbol_ = std::string{"on_plug"};
    void* lib_handle_{};
    std::unique_ptr<::viu::device::plugin::catalog> local_catalog_{};
    catalog_interface* catalog_iface_{};

    static void api_set_name(void* ctx, const char* name)
    {
        const auto self = static_cast<catalog_loader*>(ctx);
        self->local_catalog_->set_name(std::string{name});
    }

    static void api_set_version(void* ctx, const char* version)
    {
        const auto self = static_cast<catalog_loader*>(ctx);
        self->local_catalog_->set_version(std::string{version});
    }

    static void api_register_device(
        void* ctx,
        const char* device_name,
        device_factory_fn f
    )
    {
        const auto self = static_cast<catalog_loader*>(ctx);
        self->local_catalog_->register_device_factory(
            std::string{device_name},
            f
        );
    }
};

static_assert(!std::copyable<catalog_loader>);

class virtual_device_manager final {
public:
    virtual_device_manager() = default;

    auto register_catalog(const std::string& name) -> catalog_interface*
    {
        plugins_.emplace(name, name);
        return plugins_[name].catalog();
    }

    auto device(const std::string& catalog_name, const std::string& device_name)
        -> std::expected<std::shared_ptr<viu::usb::mock::interface>, error>
    {
        const auto it = plugins_.find(catalog_name);

        if (it == plugins_.end()) {
            return std::unexpected(error::no_device);
        }

        return it->second.device(device_name);
    }

private:
    std::map<std::string, catalog_loader> plugins_{};
};

} // namespace viu::device::plugin
