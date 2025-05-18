module;

#include <libusb.h>

export module viu.device.proxy;

import std;

import viu.device.basic;
import viu.transfer;
import viu.usb;
import viu.vhci;

namespace viu::device {

export class proxy final : private basic {
public:
    proxy() = default;
    explicit proxy(const std::shared_ptr<usb::device>& device);
    ~proxy() override;

    proxy(const proxy&) = delete;
    proxy(proxy&&) = delete;
    auto operator=(const proxy&) -> proxy& = delete;
    auto operator=(proxy&&) -> proxy& = delete;

    auto config_descriptor() const { return usb_device_->config_descriptor(); }
    auto bos_descriptor() const { return usb_device_->bos_descriptor(); }

    auto device_descriptor() const { return usb_device_->device_descriptor(); }

    auto string_descriptors() const
    {
        return usb_device_->string_descriptors();
    }

    auto report_descriptor() const { return usb_device_->report_descriptor(); }

private:
    using transfer_tuple = std::tuple<
        usb::transfer::callback::type,
        usbip::command::payload_shared_ptr>;

    void on_out_iso_transfer_complete(
        const usbip::command& cmd,
        const usb::transfer::pointer& transfer
    );

    void on_in_iso_transfer_complete(const usb::transfer::pointer& transfer);
    void on_in_transfer_complete(const usb::transfer::pointer& transfer);

    void on_out_transfer_complete(
        const usbip::command& cmd,
        const usb::transfer::pointer& transfer
    );

    auto prepare_buffer(const usbip::command& cmd);
    auto prepare_iso_descriptors_buffer(const usbip::command& cmd);
    auto prepare_transfer(const usbip::command& cmd) -> usb::transfer::info;
    void submit_transfer(const usbip::command& cmd);
    void submit_iso_transfer(const usbip::command& cmd);
    void submit_bulk_transfer(const usbip::command& cmd);
    void submit_interrupt_transfer(const usbip::command& cmd);
    void set_configuration(const usbip::command& cmd);
    void interface(const usbip::command& cmd);
    void descriptor(const usbip::command& cmd);
    void execute_in_control_command(const usbip::command& cmd) override;
    void execute_std_in_device_control_command(const usbip::command& cmd);
    void execute_std_in_interface_control_command(const usbip::command& cmd);
    void execute_out_control_command(const usbip::command& cmd) override;
    void execute_std_out_device_control_command(const usbip::command& cmd);
    void execute_std_out_interface_control_command(const usbip::command& cmd);
    void send_data_to_device(const usbip::command& cmd) override;
    void read_data_from_device(const usbip::command& cmd) override;

    std::shared_ptr<usb::device> usb_device_{};
    std::jthread device_thread_{};
};

static_assert(!std::copyable<proxy>);

} // namespace viu::device
