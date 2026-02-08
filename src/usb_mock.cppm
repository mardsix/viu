export module viu.device.mock;

import std;

import viu.usb;
import viu.device.proxy;
import viu.usb.descriptors;

namespace viu::device {

export class mock : public proxy {
public:
    mock(
        usb::descriptor::tree descriptor_tree,
        std::shared_ptr<usb::mock::interface> xfer_iface
    )
        : proxy{std::make_shared<viu::usb::mock>(descriptor_tree, xfer_iface)}
    {
    }
};

static_assert(!std::copyable<mock>);

} // namespace viu::device
