module;

#include <boost/describe/class.hpp>

export module viu.usb.descriptors:structs;

import std;

export struct usbip_iso_packet_descriptor {
    std::uint32_t offset;
    std::uint32_t length;
    std::uint32_t actual_length;
    std::uint32_t status;
} __attribute__((packed));

BOOST_DESCRIBE_STRUCT(
    usbip_iso_packet_descriptor,
    (),
    (offset, length, actual_length, status)
);

namespace viu::usb::descriptor {

export constexpr auto iso_descriptor_size()
{
    return sizeof(usbip_iso_packet_descriptor);
}

export struct iso {
    std::size_t data_size{};
    std::vector<usbip_iso_packet_descriptor> descriptors;
    std::int32_t error_count{};
};

} // namespace viu::usb::descriptor
