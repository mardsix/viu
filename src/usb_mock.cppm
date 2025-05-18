export module viu.device.mock;

import std;

import viu.usb;
import viu.device.proxy;
import viu.usb.descriptors;

namespace viu::device {

export class mock {
public:
    mock(
        usb::descriptor::tree descriptor_tree,
        std::shared_ptr<usb::mock::interface> xfer_iface
    );

    ~mock();

    mock(const mock&) = delete;
    mock(mock&&) = delete;
    auto operator=(const mock&) -> mock& = delete;
    auto operator=(mock&&) -> mock& = delete;

private:
    std::shared_ptr<usb::mock> device_{};
    proxy proxy_{};
    std::jthread device_thread_{};
};

static_assert(!std::copyable<mock>);

} // namespace viu::device
