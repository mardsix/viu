module viu.device.mock;

using viu::device::mock;

mock::mock(
    usb::descriptor::tree descriptor_tree,
    std::shared_ptr<usb::mock::interface> xfer_iface
)
    : device_{std::make_shared<viu::usb::mock>(descriptor_tree, xfer_iface)},
      proxy_{device_},
      device_thread_{std::jthread{[](const std::stop_token& stoken) {
          while (!stoken.stop_requested()) {
              using namespace std::chrono_literals;
              std::this_thread::sleep_for(100ms);
          }
      }}}
{
}

mock::~mock()
{
    device_thread_.request_stop();
    device_thread_.join();
}
