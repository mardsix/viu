export module viu.device.mock;

import std;

import viu.usb;
import viu.usb.mock.abi;
import viu.device.proxy;
import viu.usb.descriptors;

namespace viu::device {

export class mock : public proxy {
public:
    mock(
        usb::descriptor::tree descriptor_tree,
        viu_usb_mock_opaque* xfer_instance
    )
        : proxy{
              std::make_shared<viu::usb::mock>(descriptor_tree, xfer_instance)
          }
    {
    }
};

static_assert(!std::copyable<mock>);

} // namespace viu::device
