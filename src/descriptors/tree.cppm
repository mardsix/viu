module;

#include <libusb.h>

export module viu.usb.descriptors:tree;

import std;

import viu.types;
import viu.vector;

import :descriptor_classes;
import :packer;
import :structs;
import :traits;
import :types;

namespace viu::usb::descriptor {

export using bos_descriptor_pointer =
    viu::type::unique_pointer_t<libusb_bos_descriptor>;
export using config_descriptor_pointer =
    viu::type::unique_pointer_t<libusb_config_descriptor>;
export using string_descriptor_type = std::vector<std::vector<std::uint8_t>>;
export using language_id_type = std::uint16_t;
export using string_descriptor_map =
    std::map<language_id_type, string_descriptor_type>;

export struct tree {
    tree() = default;

    explicit tree(
        libusb_device_descriptor device_desc,
        const config_descriptor_pointer& config_desc,
        string_descriptor_map string_descs,
        const bos_descriptor_pointer& bos_desc,
        std::vector<std::uint8_t> report_desc
    );

    [[nodiscard]] auto device_descriptor() const noexcept
    {
        return device_desc_;
    }
    [[nodiscard]] auto device_config() const { return wrapped_config_desc_; }
    [[nodiscard]] auto bos_descriptor() const { return wrapped_bos_desc_; }
    [[nodiscard]] auto string_descriptors() const { return string_descs_; }
    [[nodiscard]] auto report_descriptor() const { return report_desc_; }

    void save(const std::filesystem::path& path) const;
    void load(const std::filesystem::path& path);

private:
    [[nodiscard]] static auto vector_of_extra(
        const descriptor_with_extra auto& desc
    );
    [[nodiscard]] auto build(const libusb_endpoint_descriptor& ep);
    [[nodiscard]] auto build(libusb_interface_descriptor iface_desc);
    [[nodiscard]] auto build(const libusb_interface& usb_iface);
    void build(const config_descriptor_pointer& config_desc);
    [[nodiscard]] auto build(
        const libusb_bos_dev_capability_descriptor* dev_cap_desc
    );
    void build(const bos_descriptor_pointer& bos_desc);

    libusb_device_descriptor device_desc_{};
    config wrapped_config_desc_{};
    string_descriptor_map string_descs_;
    bos wrapped_bos_desc_{};
    std::vector<std::uint8_t> report_desc_;
};

} // namespace viu::usb::descriptor
