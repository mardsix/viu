module;

#include "usb_mock_abi.h"

export module viu.usb.mock.abi;

export {
    using ::device_factory_fn;
    using ::plugin_catalog_api;
    using ::viu_usb_mock_opaque;
    using ::viu_usb_mock_transfer_control_opaque;
}
