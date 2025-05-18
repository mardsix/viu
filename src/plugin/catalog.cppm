export module viu.plugin.factory;

import std;

import viu.plugin.interfaces;
import viu.usb;

export namespace viu::device::plugin {

class catalog final : public catalog_interface {
public:
    auto set_name(std::string n) -> void { name_ = std::move(n); }
    auto set_version(std::string v) -> void { version_ = std::move(v); }

    using device_factory_fn = viu::usb::mock::interface* (*)();
    auto register_device_factory(const std::string& name, device_factory_fn f)
        -> catalog&
    {
        entries_.push_back(
            std::make_pair(
                name,
                std::shared_ptr<viu::usb::mock::interface>(f())
            )
        );
        return *this;
    }

    auto name() const -> std::string override { return name_; }
    auto version() const -> std::string override { return version_; }

    auto number_of_devices() const -> std::size_t override
    {
        return entries_.size();
    }

    auto device_name(std::size_t index) const -> std::string override
    {
        return entries_[index].first;
    }

    auto device(const std::string& name) const -> std::
        expected<std::shared_ptr<viu::usb::mock::interface>, error> override
    {
        const auto it = std::ranges::find_if(entries_, [&name](const auto& p) {
            return p.first == name;
        });

        if (it == entries_.end()) {
            return std::unexpected{viu::device::plugin::error::no_device};
        }

        return it->second;
    }

    template <typename C>
    auto register_device(std::string const& name) -> catalog&
    {
        entries_.push_back(std::make_pair(name, std::make_shared<C>()));
        return *this;
    }

private:
    using entry_type =
        std::pair<std::string, std::shared_ptr<viu::usb::mock::interface>>;

    std::string name_{};
    std::string version_{};
    std::vector<entry_type> entries_{};
};

} // namespace viu::device::plugin
