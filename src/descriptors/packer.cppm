module;

#include <libusb.h>

export module viu.usb.descriptors:packer;

import std;

import viu.types;
import viu.vector;

import :descriptor_classes;
import :structs;
import :traits;
import :types;

namespace viu::usb::descriptor {

export struct packer {
    [[nodiscard]] auto data() const { return packed_data_; }

    void pack(const libusb_device_descriptor& device_descriptor);
    void pack(const libusb_config_descriptor& config_descriptor);
    void pack(const config& wrapped_config_descriptor);
    void pack(const libusb_bos_descriptor* bos_descriptor);
    void pack(const bos& wrapped_bos_descriptor);

    template <typename T>
    requires std::integral<T>
    static void to_packing_type(const std::vector<T>& in, vector_type& out)
    {
        std::ranges::transform(
            in,
            std::back_inserter(out),
            [](const std::integral auto b) -> auto { return packing_type{b}; }
        );
    }

private:
    vector_type packed_data_;

    template <typename R, typename... Args>
    [[nodiscard]] auto member_fn_lambda(R (packer::*member_fn)(Args...));

    void pack_extra(const descriptor_with_extra auto& descriptor);
    void pack_endpoint_descriptor(
        const libusb_endpoint_descriptor& ep_descriptor
    );
    void pack_interface_descriptor(
        const libusb_interface_descriptor& interface_descriptor
    );
    void pack_interface(const libusb_interface& interface);
    void pack_interfaces(const libusb_config_descriptor& config_descriptor);
    void pack_bos_dev_capability_descriptor(
        const libusb_bos_dev_capability_descriptor* dev_cap_desc
    );
};

} // namespace viu::usb::descriptor
