module;

#include <libusb.h>

#include <boost/describe/class.hpp>

export module viu.usb.descriptors:types;

import std;
import viu.types;
import viu.vector;

namespace viu::usb::descriptor {

export using packing_type = std::byte;
export using vector_type = std::vector<packing_type>;

} // namespace viu::usb::descriptor

namespace viu::usb::endpoint {

export const auto max_count_out = std::uint8_t{16};
export const auto max_count_in = std::uint8_t{16};

}; // namespace viu::usb::endpoint

BOOST_DESCRIBE_STRUCT(
    libusb_device_descriptor,
    (),
    (bLength,
     bDescriptorType,
     bcdUSB,
     bDeviceClass,
     bDeviceSubClass,
     bDeviceProtocol,
     bMaxPacketSize0,
     idVendor,
     idProduct,
     bcdDevice,
     iManufacturer,
     iProduct,
     iSerialNumber,
     bNumConfigurations)
);

BOOST_DESCRIBE_STRUCT(
    libusb_config_descriptor,
    (),
    (bLength,
     bDescriptorType,
     wTotalLength,
     bNumInterfaces,
     bConfigurationValue,
     iConfiguration,
     bmAttributes,
     MaxPower)
);

BOOST_DESCRIBE_STRUCT(
    libusb_interface_descriptor,
    (),
    (bLength,
     bDescriptorType,
     bInterfaceNumber,
     bAlternateSetting,
     bNumEndpoints,
     bInterfaceClass,
     bInterfaceSubClass,
     bInterfaceProtocol,
     iInterface)
);

BOOST_DESCRIBE_STRUCT(
    libusb_endpoint_descriptor,
    (),
    (bLength,
     bDescriptorType,
     bEndpointAddress,
     bmAttributes,
     wMaxPacketSize,
     bInterval,
     bRefresh,
     bSynchAddress)
);

struct endpoint_descriptor : libusb_endpoint_descriptor {};
struct audio_endpoint_descriptor : libusb_endpoint_descriptor {};

BOOST_DESCRIBE_STRUCT(
    endpoint_descriptor,
    (),
    (bLength,
     bDescriptorType,
     bEndpointAddress,
     bmAttributes,
     wMaxPacketSize,
     bInterval)
);

BOOST_DESCRIBE_STRUCT(
    audio_endpoint_descriptor,
    (),
    (bLength,
     bDescriptorType,
     bEndpointAddress,
     bmAttributes,
     wMaxPacketSize,
     bInterval,
     bRefresh,
     bSynchAddress)
);

BOOST_DESCRIBE_STRUCT(
    libusb_bos_descriptor,
    (),
    (bLength, bDescriptorType, wTotalLength, bNumDeviceCaps)
);

BOOST_DESCRIBE_STRUCT(
    libusb_usb_2_0_extension_descriptor,
    (),
    (bLength, bDescriptorType, bDevCapabilityType, bmAttributes)
);

BOOST_DESCRIBE_STRUCT(
    libusb_ss_usb_device_capability_descriptor,
    (),
    (bLength,
     bDescriptorType,
     bDevCapabilityType,
     bmAttributes,
     wSpeedSupported,
     bFunctionalitySupport,
     bU1DevExitLat,
     bU2DevExitLat)
);

BOOST_DESCRIBE_STRUCT(
    libusb_bos_dev_capability_descriptor,
    (),
    (bLength, bDescriptorType, bDevCapabilityType)
);
