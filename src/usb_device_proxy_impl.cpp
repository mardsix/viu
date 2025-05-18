module;

#include <boost/describe.hpp>

#include "libusb.h"

BOOST_DESCRIBE_ENUM(
    libusb_descriptor_type,
    LIBUSB_DT_DEVICE,
    LIBUSB_DT_CONFIG,
    LIBUSB_DT_STRING,
    LIBUSB_DT_INTERFACE,
    LIBUSB_DT_ENDPOINT,
    LIBUSB_DT_BOS,
    LIBUSB_DT_DEVICE_CAPABILITY,
    LIBUSB_DT_HID,
    LIBUSB_DT_REPORT,
    LIBUSB_DT_PHYSICAL,
    LIBUSB_DT_HUB,
    LIBUSB_DT_SUPERSPEED_HUB,
    LIBUSB_DT_SS_ENDPOINT_COMPANION
);

module viu.device.proxy;

import std;

import viu.assert;
import viu.format;
import viu.transfer;
import viu.usb.descriptors;
import viu.vhci;

using viu::device::proxy;

proxy::proxy(const std::shared_ptr<usb::device>& device) : usb_device_{device}
{
    device_thread_ = std::jthread{[this](const std::stop_token& stoken) {
        auto completed = int{0};

        const auto usb_event_handler = [this, &completed]() {
            while (completed == 0) {
                auto result = usb_device_->handle_events(
                    std::chrono::milliseconds{100},
                    &completed
                );
                viu::_assert(result == LIBUSB_SUCCESS);
            }
        };

        attach(usb_device_->speed(), 1);
        auto event_handler_thread = std::thread{usb_event_handler};

        while (!stoken.stop_requested()) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
        }

        usb_device_->cancel_transfers();

        completed = 1;
        event_handler_thread.join();
    }};
}

proxy::~proxy()
{
    device_thread_.request_stop();
    device_thread_.join();
}

void proxy::read_data_from_device(const usbip::command& cmd)
{
    viu::_assert(cmd.ep() < usb::endpoint::max_count_in);
    viu::_assert(cmd.is_in());
    submit_transfer(cmd);
}

void proxy::descriptor(const usbip::command& cmd)
{
    const auto control_setup = cmd.control_setup();
    const auto descriptor_type = usb::descriptor::type_from_value(
        control_setup.wValue
    );
    const auto descriptor_index = usb::descriptor::index_from_value(
        control_setup.wValue
    );
    auto descriptor_data = usb::descriptor::vector_type{};

    switch (descriptor_type) {
        case libusb_descriptor_type::LIBUSB_DT_DEVICE:
            descriptor_data = usb_device_->pack_device_descriptor();
            break;

        case libusb_descriptor_type::LIBUSB_DT_CONFIG:
            descriptor_data = usb_device_->pack_config_descriptor(
                descriptor_index
            );
            break;

        case libusb_descriptor_type::LIBUSB_DT_STRING:
            descriptor_data = usb_device_->pack_string_descriptor(
                control_setup.wIndex,
                descriptor_index
            );
            break;

        case libusb_descriptor_type::LIBUSB_DT_BOS:
            descriptor_data = usb_device_->pack_bos_descriptor();
            break;

        case libusb_descriptor_type::LIBUSB_DT_REPORT:
            descriptor_data = usb_device_->pack_report_descriptor();
            break;

        default: {
            auto data = usb_device_->submit_control_setup(control_setup);
            if (!data.has_value()) {
                const auto unknown = std::to_string(
                    static_cast<std::int32_t>(descriptor_type)
                );
                std::println(
                    std::cerr,
                    "libusb error: {} for descriptor type: {}",
                    data.error(),
                    boost::describe::enum_to_string(
                        descriptor_type,
                        unknown.c_str()
                    )
                );

                queue_reply_to_host(cmd, nullptr, 0, data.error());
                return;
            }

            usb::descriptor::packer::to_packing_type(*data, descriptor_data);
        } break;
    }

    const auto status = std::int32_t{descriptor_data.empty() ? 1 : 0};
    const auto reply_length =
        std::min(descriptor_data.size(), std::size_t{control_setup.wLength});

    queue_reply_to_host(cmd, descriptor_data.data(), reply_length, status);
}

void proxy::on_out_iso_transfer_complete(
    const usbip::command& cmd,
    const usb::transfer::pointer& transfer
)
{
    viu::_assert(transfer != nullptr);

    const auto iso_desc = usb::transfer::iso_descriptors(transfer);

    queue_reply_to_host(
        cmd,
        iso_desc.descriptors.data(),
        iso_desc.data_size,
        0,
        usb::transfer::iso_descriptor_size(transfer),
        iso_desc.error_count
    );
}

void proxy::on_in_iso_transfer_complete(const usb::transfer::pointer& transfer)
{
    viu::_assert(transfer != nullptr);
    viu::_assert(usb::transfer::is_iso(transfer));

    queue_data_for_host(transfer);
}

void proxy::on_in_transfer_complete(const usb::transfer::pointer& transfer)
{
    viu::_assert(transfer != nullptr);
    viu::_assert(transfer->status == LIBUSB_TRANSFER_COMPLETED);
    viu::_assert(transfer->actual_length > 0);

    queue_data_for_host(transfer);
}

void proxy::on_out_transfer_complete(
    const usbip::command& cmd,
    const usb::transfer::pointer& transfer
)
{
    viu::_assert(transfer != nullptr);
    viu::_assert(transfer->actual_length == transfer->length);
    queue_reply_to_host(cmd, nullptr, transfer->actual_length);
}

void proxy::send_data_to_device(const usbip::command& cmd)
{
    viu::_assert(cmd.ep() < usb::endpoint::max_count_out);
    viu::_assert(cmd.is_out());
    submit_transfer(cmd);
}

auto proxy::prepare_buffer(const usbip::command& cmd)
{
    auto buffer = usb::transfer::buffer_type{};

    if (cmd.is_out()) {
        std::ranges::copy(cmd.payload(), std::back_inserter(buffer));
        if (cmd.is_iso()) {
            buffer.resize(buffer.size() - cmd.iso_descriptor_size());
        }
    } else {
        buffer.resize(cmd.transfer_buffer_size());
    }

    return buffer;
}

auto proxy::prepare_iso_descriptors_buffer(const usbip::command& cmd)
{
    viu::_assert(cmd.is_iso());

    auto iso_desc_buffer = usb::transfer::buffer_type{};

    if (cmd.is_out()) {
        auto payload = usb::transfer::buffer_type{};
        std::ranges::copy(cmd.payload(), std::back_inserter(payload));

        const auto iso_desc_size = cmd.iso_descriptor_size();
        viu::_assert(std::size(payload) > iso_desc_size);

        iso_desc_buffer.insert(
            std::end(iso_desc_buffer),
            std::make_move_iterator(std::end(payload) - iso_desc_size),
            std::make_move_iterator(std::end(payload))
        );
    }

    return iso_desc_buffer;
}

auto proxy::prepare_transfer(const usbip::command& cmd) -> usb::transfer::info
{
    using xfr_ptr = usb::transfer::pointer;
    using cb_t = usb::transfer::callback::type;

    const auto buffer = prepare_buffer(cmd);

    if (cmd.is_iso()) {
        const cb_t in_iso_cb = [this](const xfr_ptr& xfr) {
            on_in_iso_transfer_complete(xfr);
        };

        const cb_t out_iso_cb = [=, this](const xfr_ptr& xfr) {
            on_out_iso_transfer_complete(cmd, xfr);
        };

        auto xfer_info = usb::transfer::info{
            .ep_address = cmd.ep_address(),
            .buffer = buffer,
            .callback = (cmd.is_in() ? in_iso_cb : out_iso_cb),
        };

        xfer_info.iso = usb::transfer::iso{
            .packet_count = cmd.iso_packet_count(),
            .descriptors = prepare_iso_descriptors_buffer(cmd)
        };

        return xfer_info;
    }

    const cb_t in_cb = [this](const xfr_ptr& xfr) {
        on_in_transfer_complete(xfr);
    };

    const cb_t out_cb = [=, this](const xfr_ptr& xfr) {
        on_out_transfer_complete(cmd, xfr);
    };

    return usb::transfer::info{
        .ep_address = cmd.ep_address(),
        .buffer = buffer,
        .callback = cmd.is_in() ? in_cb : out_cb
    };
}

void proxy::submit_transfer(const usbip::command& cmd)
{
    const auto xfer_type = usb_device_->ep_transfer_type(cmd.ep_address());
    viu::_assert(xfer_type.has_value());

    switch (*xfer_type) {
        case LIBUSB_ENDPOINT_TRANSFER_TYPE_CONTROL:
            viu::_assert(false);
            break;

        case LIBUSB_ENDPOINT_TRANSFER_TYPE_ISOCHRONOUS:
            submit_iso_transfer(cmd);
            break;

        case LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK:
            submit_bulk_transfer(cmd);
            break;

        case LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT:
            submit_interrupt_transfer(cmd);
            break;
        default:
            viu::_assert(false);
            break;
    }
}

void proxy::submit_iso_transfer(const usbip::command& cmd)
{
    const auto xfer_info = prepare_transfer(cmd);
    usb_device_->submit_iso_transfer(xfer_info);
}

void proxy::submit_bulk_transfer(const usbip::command& cmd)
{
    const auto xfer_info = prepare_transfer(cmd);
    usb_device_->submit_bulk_transfer(xfer_info);
}

void proxy::submit_interrupt_transfer(const usbip::command& cmd)
{
    const auto xfer_info = prepare_transfer(cmd);
    usb_device_->submit_interrupt_transfer(xfer_info);
}

void proxy::execute_in_control_command(const usbip::command& cmd)
{
    const auto submit_ctrl_setup = [&]() {
        const auto control_setup = cmd.control_setup();
        const auto data = usb_device_->submit_control_setup(control_setup);
        queue_reply_to_host(
            cmd,
            data.has_value() ? data->data() : nullptr,
            data.has_value() ? std::size(*data) : 0,
            data.has_value() ? 0 : data.error()
        );
    };

    if (cmd.request_type() != LIBUSB_REQUEST_TYPE_STANDARD) {
        submit_ctrl_setup();
        return;
    }

    switch (cmd.recipient()) {
        case LIBUSB_RECIPIENT_DEVICE:
            execute_std_in_device_control_command(cmd);
            break;

        case LIBUSB_RECIPIENT_INTERFACE:
            execute_std_in_interface_control_command(cmd);
            break;

        default:
            submit_ctrl_setup();
            break;
    }
}

void proxy::execute_std_in_device_control_command(const usbip::command& cmd)
{
    const auto control_setup = cmd.control_setup();
    switch (control_setup.bRequest) {
        case LIBUSB_REQUEST_GET_STATUS: {
            const std::uint16_t reply = usb_device_->is_self_powered() ? 1 : 0;
            queue_reply_to_host(cmd, &reply, sizeof(reply));
            break;
        }

        case LIBUSB_REQUEST_GET_DESCRIPTOR:
            descriptor(cmd);
            break;

        default: {
            const auto data = usb_device_->submit_control_setup(control_setup);
            queue_reply_to_host(
                cmd,
                data.has_value() ? data->data() : nullptr,
                data.has_value() ? std::size(*data) : 0,
                data.has_value() ? 0 : data.error()
            );
        } break;
    }
}

void proxy::execute_std_in_interface_control_command(const usbip::command& cmd)
{
    const auto control_setup = cmd.control_setup();
    switch (control_setup.bRequest) {
        case LIBUSB_REQUEST_GET_DESCRIPTOR:
            descriptor(cmd);
            break;

        default: {
            const auto data = usb_device_->submit_control_setup(control_setup);
            queue_reply_to_host(
                cmd,
                data.has_value() ? data->data() : nullptr,
                data.has_value() ? std::size(*data) : 0,
                data.has_value() ? 0 : data.error()
            );
        } break;
    }
}

void proxy::set_configuration(const usbip::command& cmd)
{
    auto result = usb_device_->set_configuration(cmd.config_index());
    viu::_assert(result == LIBUSB_SUCCESS);
    queue_reply_to_host(cmd, nullptr, cmd.transfer_buffer_size());
}

void proxy::interface(const usbip::command& cmd)
{
    const auto control_setup = cmd.control_setup();
    const auto alt_setting = usb_device_->current_altsetting(
        format::integral<std::uint8_t>::at<0>(control_setup.wIndex)
    );
    queue_reply_to_host(cmd, &alt_setting, sizeof(alt_setting));
}

void proxy::execute_out_control_command(const usbip::command& cmd)
{
    const auto submit_ctrl_setup = [&]() {
        const auto control_setup = cmd.control_setup();
        const auto data =
            usb_device_->submit_control_setup(control_setup, cmd.payload());
        queue_reply_to_host(
            cmd,
            nullptr,
            control_setup.wLength,
            data.has_value() ? 0 : data.error()
        );
    };

    if (cmd.request_type() != LIBUSB_REQUEST_TYPE_STANDARD) {
        submit_ctrl_setup();
        return;
    }

    switch (cmd.recipient()) {
        case LIBUSB_RECIPIENT_DEVICE:
            execute_std_out_device_control_command(cmd);
            break;
        case LIBUSB_RECIPIENT_INTERFACE:
            execute_std_out_interface_control_command(cmd);
            break;

        default:
            submit_ctrl_setup();
            break;
    }
}

void proxy::execute_std_out_device_control_command(const usbip::command& cmd)
{
    const auto control_setup = cmd.control_setup();
    switch (control_setup.bRequest) {
        case LIBUSB_REQUEST_SET_CONFIGURATION:
            set_configuration(cmd);
            break;

        case libusb_standard_request::LIBUSB_SET_ISOCH_DELAY:
            queue_reply_to_host(cmd, nullptr, 0);
            break;

        default: {
            const auto data =
                usb_device_->submit_control_setup(control_setup, cmd.payload());
            queue_reply_to_host(
                cmd,
                nullptr,
                control_setup.wLength,
                data.has_value() ? 0 : data.error()
            );
        } break;
    }
}

void proxy::execute_std_out_interface_control_command(const usbip::command& cmd)
{
    const auto control_setup = cmd.control_setup();
    switch (control_setup.bRequest) {
        case libusb_standard_request::LIBUSB_REQUEST_GET_INTERFACE:
            interface(cmd);
            break;

        case libusb_standard_request::LIBUSB_REQUEST_SET_INTERFACE: {
            const auto interface = format::integral<std::uint8_t>::at<0>(
                control_setup.wIndex
            );
            const auto alt_setting = format::integral<std::uint8_t>::at<0>(
                control_setup.wValue
            );
            const auto result =
                usb_device_->set_interface(interface, alt_setting);
            viu::_assert(result == LIBUSB_SUCCESS);
            queue_reply_to_host(cmd, nullptr, control_setup.wLength);
        } break;

        default: {
            const auto data =
                usb_device_->submit_control_setup(control_setup, cmd.payload());
            queue_reply_to_host(
                cmd,
                nullptr,
                control_setup.wLength,
                data.has_value() ? 0 : data.error()
            );
        } break;
    }
}
