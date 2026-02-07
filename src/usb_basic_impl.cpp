module;

#include <cerrno>

#include <libusb.h>

module viu.device.basic;

import std;

import viu.assert;
import viu.boost;
import viu.format;
import viu.transfer;
import viu.usb.descriptors;

using viu::device::basic;

basic::~basic()
{
    commands_queue_.close();
    replies_queue_.close();

    for (auto& command_queue : in_commands_) {
        command_queue.close();
    }

    for (auto& q : in_data_) {
        q.close();
    }

    vhci_driver_.request_stop();
    for (auto& t : threads_) {
        t.request_stop();
        t.join();
    }
}

void basic::attach(const std::uint32_t speed, const std::uint8_t device_id)
{
    vhci_driver_.attach(speed, device_id);
}

void basic::command_produce_thread()
{
    const auto func = [this](const std::stop_token& stoken) {
        while (!stoken.stop_requested()) {
            try {
                commands_queue_.push(read_command());
            } catch (const boost::concurrent::sync_queue_is_closed&) {
                break;
            } catch (const boost::system::system_error& se) {
                if (se.code() == boost::asio::error::eof) {
                    break;
                }
            }
        }
    };

    threads_.emplace_back(func);
}

void basic::reply_consume_thread()
{
    const auto func = [this](const std::stop_token& stoken) {
        while (!stoken.stop_requested()) {
            try {
                const auto rep = replies_queue_.pull();
                const auto cmd_seqnum = format::endian::from_big(rep.seqnum());

                {
                    [[maybe_unused]] const std::lock_guard<std::mutex> _{
                        unlinked_set_mutex_
                    };

                    if (unlinked_seqnums_.contains(cmd_seqnum)) {
                        unlinked_seqnums_.erase(cmd_seqnum);
                        continue;
                    }
                }

                auto write_buffer = boost::asio::streambuf{};
                const auto payload = rep.payload();
                const auto header = rep.header();

                write_buffer.sputn((const char*)&header, sizeof(header));
                write_buffer.sputn(
                    (const char*)payload.data(),
                    std::size(payload)
                );
                const auto total_size = sizeof(header) + std::size(payload);

                vhci_driver_.write(write_buffer, total_size);
            } catch (const boost::concurrent::sync_queue_is_closed&) {
                break;
            } catch (const boost::system::system_error& se) {
                if (se.code() == boost::asio::error::eof) {
                    break;
                }
            }
        }
    };

    threads_.emplace_back(func);
}

void basic::transfer_thread(const std::uint32_t ep)
{
    const auto func = [this, ep](const std::stop_token& stoken) {
        while (!stoken.stop_requested()) {
            try {
                send_data_to_host(ep);
            } catch (const boost::concurrent::sync_queue_is_closed&) {
                break;
            }
        }
    };

    threads_.emplace_back(func);
}

void basic::command_execution_thread()
{
    const auto func = [this](const std::stop_token& stoken) {
        while (!stoken.stop_requested()) {
            try {
                execute_command();
            } catch (const boost::concurrent::sync_queue_is_closed&) {
                break;
            }
        }
    };

    threads_.emplace_back(func);
}

void basic::start()
{
    if (!threads_.empty()) {
        return;
    }

    command_produce_thread();
    reply_consume_thread();

    for (std::size_t ep = 0; ep < std::size(in_commands_); ++ep) {
        transfer_thread(ep);
    }

    command_execution_thread();
}

auto basic::read_command() -> usbip::command
{
    constexpr auto hdr_size = usbip::command::header_size();
    auto read_buffer = boost::asio::streambuf{hdr_size};
    vhci_driver_.read(read_buffer, hdr_size);

    auto cmd = usbip::command::from_big_endian(read_buffer);
    auto payload_size = cmd.payload_size();

    if (payload_size != 0) {
        auto payload = boost::asio::streambuf{};
        vhci_driver_.read(payload, payload_size);

        std::copy(
            boost::asio::buffers_begin(payload.data()),
            boost::asio::buffers_end(payload.data()),
            std::back_inserter(cmd.payload())
        );
    }

    return cmd;
}

void basic::execute_command()
{
    auto cmd = usbip::command{};
    if (auto result = commands_queue_.wait_pull(cmd);
        result != boost::concurrent::queue_op_status::success) {

        if (result == boost::concurrent::queue_op_status::closed) {
            throw boost::concurrent::sync_queue_is_closed{};
        }

        std::println(
            std::cerr,
            "Failed to pull command queue: {}",
            static_cast<int>(result)
        );
        return;
    }

    if (cmd.is_submit()) {
        if (cmd.ep() == 0) {
            execute_control_command(cmd);
        } else {
            execute_ep_command(cmd);
        }
        return;
    }

    if (cmd.is_unlink()) {
        unlink_command(cmd);
        return;
    }

    throw std::runtime_error(
        format::make_string("Invalid usbip command:", cmd.request())
    );
}

void basic::execute_control_command(const usbip::command& cmd)
{
    if (cmd.is_in()) {
        // device->host
        execute_in_control_command(cmd);
    } else if (cmd.is_out()) {
        // host->device
        execute_out_control_command(cmd);
    } else {
        throw std::runtime_error("Invalid command direction");
    }
}

void basic::execute_ep_command(const usbip::command& cmd)
{
    if (cmd.is_in()) {
        // device->host
        read_data_from_device(cmd);
        in_commands_[cmd.ep()].push(cmd);
    } else if (cmd.is_out()) {
        // host->device
        send_data_to_device(cmd);
    } else {
        throw std::runtime_error("Invalid command direction");
    }
}

void basic::unlink_command(const usbip::command& cmd)
{
    viu::_assert(cmd.ep() < usb::endpoint::max_count_in);
    viu::_assert(cmd.is_unlink());

    auto status = std::int32_t{};
    {
        [[maybe_unused]] const std::lock_guard<std::mutex> _{
            unlinked_set_mutex_
        };

        const auto result = unlinked_seqnums_.insert(cmd.unlink_seqnum());
        if (result.second) {
            status = -ECONNRESET;
        }
    }

    basic::queue_reply_request req{};
    req.cmd = cmd;
    req.data = nullptr;
    req.size = 0;
    req.status = status;
    queue_reply_to_host(req);
}

void basic::queue_reply_to_host(const queue_reply_request& req)
{
    const auto& cmd = req.cmd;
    const auto data = req.data;
    const auto size = req.size;
    const auto status = req.status;
    const auto iso_descriptor_size = req.iso_descriptor_size;
    const auto error_count = req.error_count;

    auto replay = usbip::command{};
    replay.header().base = cmd.reply_header();
    switch (cmd.request()) {
        case USBIP_CMD_SUBMIT: {
            replay.header().ret_submit =
                cmd.make_ret_submit_header(size, status, error_count);

            auto payload_size = cmd.is_out() ? 0 : size;
            if (cmd.is_iso()) {
                payload_size += iso_descriptor_size;
            }

            if (data != nullptr) {
                replay.payload().resize(payload_size);
                std::memcpy(replay.payload().data(), data, payload_size);
            }
        } break;

        case USBIP_CMD_UNLINK:
            replay.header().ret_unlink = cmd.make_ret_unlink_header(status);
            break;

        default:
            throw std::runtime_error(
                format::make_string("Invalid request:", cmd.request())
            );
    }

    replies_queue_.push(replay);
}

void basic::send_data_to_host(const std::uint32_t ep)
{
    const auto& cmd = in_commands_[ep].pull();
    viu::_assert(cmd.transfer_buffer_size() > 0);
    auto& ep_in_data = in_data_[cmd.ep()];
    const auto& data = ep_in_data.pull();

    const auto data_size = data.buffer.size() - data.iso_descriptor_size;
    viu::_assert(data_size <= cmd.transfer_buffer_size());

    basic::queue_reply_request req{};
    req.cmd = cmd;
    req.data = data.buffer.data();
    req.size = data_size;
    req.status = 0;
    req.iso_descriptor_size = data.iso_descriptor_size;
    req.error_count = data.error_count;
    queue_reply_to_host(req);
}

void basic::queue_data_for_host(const usb::transfer::pointer& transfer)
{
    viu::_assert(transfer != nullptr);
    const auto direction = transfer->endpoint & LIBUSB_ENDPOINT_DIR_MASK;
    viu::_assert(direction == LIBUSB_ENDPOINT_IN);
    const std::uint8_t ep_index = transfer->endpoint &
                                  LIBUSB_ENDPOINT_ADDRESS_MASK;
    viu::_assert(ep_index < std::size(in_data_));

    auto& ep_in_data = in_data_[ep_index];

    auto size = transfer->actual_length;
    auto data = usb::transfer::buffer_type{};
    auto error_count = 0;

    const auto iso_desc_size = usb::transfer::iso_descriptor_size(transfer);

    if (usb::transfer::is_iso(transfer)) {
        const auto iso_desc = usb::transfer::iso_descriptors(transfer);
        data = usb::transfer::iso_data(transfer);
        viu::_assert(iso_desc.data_size == std::size(data));

        data.resize(iso_desc.data_size + iso_desc_size);
        std::memcpy(
            data.data() + iso_desc.data_size,
            iso_desc.descriptors.data(),
            iso_desc_size
        );

        size = iso_desc.data_size;
        error_count = iso_desc.error_count;
    } else {
        data = format::unsafe::vectorize(transfer->buffer, size);
    }

    transfer_data d{
        .iso_descriptor_size = iso_desc_size,
        .error_count = error_count
    };

    const auto total_size = size + iso_desc_size;

    viu::_assert(total_size <= std::size(data));
    std::copy_n(std::begin(data), total_size, std::back_inserter(d.buffer));

    ep_in_data.push(d);
}
