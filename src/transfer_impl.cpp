module;

#include <cerrno>

#include <libusb.h>

module viu.transfer;

import viu.assert;
import viu.format;
import viu.usb;

namespace viu::usb::transfer {

namespace {

auto give_away_transfer(libusb_transfer* const transfer)
{
    return viu::usb::transfer::pointer{
        transfer,
        [](libusb_transfer* const transfer) {
            if (transfer != nullptr) {
                delete[] transfer->buffer;
                libusb_free_transfer(transfer);
            }
        }
    };
}

} // namespace

void pending_map::on_transfer_completed_impl(libusb_transfer* const transfer)
{
    std::unique_lock lock(mutex_);

    if (transfer->status == LIBUSB_TRANSFER_TIMED_OUT ||
        transfer->status == LIBUSB_TRANSFER_CANCELLED) {

        auto erased = pending_transfers_.erase(transfer);
        viu::_assert(erased != 0);

        lock.unlock();
        give_away_transfer(transfer);
    } else {
        auto it = pending_transfers_.find(transfer);
        viu::_assert(it != pending_transfers_.end());

        auto callback_func = it->second;
        pending_transfers_.erase(it);

        lock.unlock();
        callback_func(give_away_transfer(transfer));
    }
}

void pending_map::attach(
    const callback_type& cb,
    libusb_transfer* const transfer,
    void* user_data
)
{
    transfer->user_data = user_data;

    {
        [[maybe_unused]] const std::unique_lock _(mutex_);
        auto r = pending_transfers_.insert({transfer, cb});
        viu::_assert(r.second);
    }
}

void pending_map::cancel()
{
    {
        std::unique_lock lock(mutex_);

        std::erase_if(pending_transfers_, [&](auto& p) {
            auto* transfer = p.first;
            viu::_assert(transfer);

            if (is_mock(transfer)) {
                give_away_transfer(transfer);
                return true;
            }

            libusb_cancel_transfer(transfer);
            return false;
        });

        transfers_canceled_ = true;
    }

    wait_for_canceled_transfers();

    std::unique_lock lock(mutex_);
    pending_transfers_.clear();
}

void pending_map::wait_for_canceled_transfers()
{
    using namespace std::chrono_literals;
    while (true) {
        std::shared_lock lock(mutex_);
        if (pending_transfers_.empty()) {
            break;
        }
        lock.unlock();
        std::this_thread::sleep_for(10ms);
    }
}

auto alloc(std::optional<int> iso_packets) -> libusb_transfer*
{
    const auto usb_transfer = libusb_alloc_transfer(iso_packets.value_or(0));
    viu::_assert(usb_transfer != nullptr);
    return usb_transfer;
}

auto is_mock(const libusb_transfer* const transfer) -> bool
{
    viu::_assert(transfer != nullptr);
    return transfer->dev_handle == nullptr;
}

auto actual_length(const usb::transfer::pointer& transfer) -> std::uint32_t
{
    viu::_assert(transfer != nullptr);

    if (is_iso(transfer)) {
        const auto iso_packet_descs = format::unsafe::vectorize(
            transfer->iso_packet_desc,
            transfer->num_iso_packets
        );

        return std::accumulate(
            std::cbegin(iso_packet_descs),
            std::cend(iso_packet_descs),
            0,
            [](int sum, const auto& ipd) { return sum + ipd.actual_length; }
        );
    }

    return transfer->actual_length;
}

void pending_map::submit(
    const usb::device::context_pointer& ctx,
    libusb_transfer* transfer
)
{
    if (is_mock(transfer)) {
        return;
    }

    {
        std::unique_lock lock{mutex_};

        if (transfers_canceled_) {
            const auto erased = pending_transfers_.erase(transfer);
            viu::_assert(erased != 0);

            lock.unlock();
            give_away_transfer(transfer);
            return;
        }
    }

    const auto res = libusb_submit_transfer(transfer);
    viu::_assert(res == LIBUSB_SUCCESS);
}

auto iso_data(const usb::transfer::pointer& transfer)
    -> usb::transfer::buffer_type
{
    viu::_assert(transfer != nullptr);
    viu::_assert(transfer->buffer != nullptr);
    viu::_assert(usb::transfer::is_iso(transfer));

    auto offset = 0;
    auto iso_data = usb::transfer::buffer_type{};

    const auto iso_packets = format::unsafe::vectorize(
        transfer->iso_packet_desc,
        transfer->num_iso_packets
    );

    auto read_buffer =
        format::unsafe::vectorize(transfer->buffer, transfer->length);

    using element_type = usb::transfer::buffer_type::value_type;
    const auto in_buffer = std::span<const element_type>{read_buffer};

    for (const auto& iso : iso_packets) {
        if (iso.status != LIBUSB_TRANSFER_COMPLETED) {
            continue;
        }

        const auto data_chunk = in_buffer.subspan(offset, iso.actual_length);
        std::ranges::copy(data_chunk, std::back_inserter(iso_data));

        offset += iso.length;
    }

    return iso_data;
}

auto iso_descriptors(const usb::transfer::pointer& transfer)
    -> usb::descriptor::iso
{
    viu::_assert(transfer != nullptr);
    viu::_assert(usb::transfer::is_iso(transfer));

    auto iso_desc = usb::descriptor::iso{};

    auto offset = 0;
    const auto to_usbip_iso_desc = [&offset, &iso_desc](auto iso) {
        using namespace format;

        auto iso_descriptor = usbip_iso_packet_descriptor{};
        iso_desc.data_size += iso.actual_length;

        iso_descriptor.actual_length = endian::to_big(iso.actual_length);
        iso_descriptor.length = endian::to_big(iso.length);
        iso_descriptor.offset = endian::to_big(offset);
        // https://www.kernel.org/doc/html/v4.18/driver-api/usb/error-codes.html
        if (iso.status != LIBUSB_TRANSFER_COMPLETED) {
            iso_descriptor.status = endian::to_big(-EINVAL);
            iso_desc.error_count++;
        }
        offset += iso.length;

        return iso_descriptor;
    };

    const auto iso_packets = format::unsafe::vectorize(
        transfer->iso_packet_desc,
        transfer->num_iso_packets
    );

    std::ranges::copy(
        iso_packets | std::views::transform(to_usbip_iso_desc),
        std::back_inserter(iso_desc.descriptors)
    );

    return iso_desc;
}

void control::complete() const
{
    viu::_assert(xfer_ != nullptr);
    viu::_assert(usb::transfer::is_mock(xfer_));

    xfer_->status = LIBUSB_TRANSFER_COMPLETED;

    if (xfer_->callback != nullptr) {
        xfer_->callback(xfer_);
    }
}

auto control::is_in() -> bool const
{
    viu::_assert(xfer_ != nullptr);
    return (xfer_->endpoint & direction_mask) ==
           libusb_endpoint_direction::LIBUSB_ENDPOINT_IN;
}

auto control::is_out() -> bool const
{
    viu::_assert(xfer_ != nullptr);
    return (xfer_->endpoint & direction_mask) ==
           libusb_endpoint_direction::LIBUSB_ENDPOINT_OUT;
}

void control::fill(const std::vector<std::uint8_t>& data)
{
    viu::_assert(xfer_ != nullptr);
    viu::_assert(is_in());
    viu::_assert(std::size(data) <= xfer_->length);

    std::memcpy(xfer_->buffer, data.data(), std::size(data));
    xfer_->actual_length = std::size(data);

    if (transfer::is_iso(xfer_)) {
        libusb_iso_packet_descriptor* ipd = xfer_->iso_packet_desc;
        for (auto i = 0; i < xfer_->num_iso_packets; ++i) {
            ipd->actual_length = ipd->length;
            ipd++;
        }
    }
}

auto control::read(std::optional<std::uint32_t> size)
    -> transfer::buffer_type const
{
    viu::_assert(xfer_ != nullptr);

    const auto read_size = size.value_or(this->size());
    viu::_assert(read_size <= xfer_->length);

    if (is_out()) {
        xfer_->actual_length = read_size;

        if (transfer::is_iso(xfer_)) {
            libusb_iso_packet_descriptor* ipd = xfer_->iso_packet_desc;
            for (auto i = 0; i < xfer_->num_iso_packets; ++i) {
                ipd->actual_length = ipd->length;
                ipd++;
            }
        }
    }

    return viu::format::unsafe::vectorize(xfer_->buffer, read_size);
}

auto control::size() const -> int
{
    viu::_assert(xfer_ != nullptr);
    return xfer_->length;
}

auto control::type() const -> unsigned char
{
    viu::_assert(xfer_ != nullptr);
    return xfer_->type;
}

auto control::ep() const -> std::uint8_t
{
    viu::_assert(xfer_ != nullptr);
    return xfer_->endpoint;
}

void control::attach(
    const pending_map::callback_type& cb,
    pending_map& xfer_map,
    void* user_data
)
{
    viu::_assert(xfer_ != nullptr);
    xfer_map.attach(cb, xfer_, user_data);
}

void control::submit(
    const usb::device::context_pointer& ctx,
    pending_map& xfer_map
)
{
    viu::_assert(xfer_ != nullptr);
    xfer_map.submit(ctx, xfer_);
}

auto control::read_iso_packet_descriptors() const
    -> std::vector<libusb_iso_packet_descriptor>
{
    viu::_assert(xfer_ != nullptr);
    viu::_assert(usb::transfer::is_iso(xfer_));

    return format::unsafe::vectorize(
        xfer_->iso_packet_desc,
        xfer_->num_iso_packets
    );
}

auto control::iso_packet_descriptor_count() const -> std::size_t
{
    viu::_assert(xfer_ != nullptr);

    if (usb::transfer::is_iso(xfer_)) {
        return xfer_->num_iso_packets;
    }

    return 0;
}

void control::fill_iso_packet_descriptors(
    const std::vector<libusb_iso_packet_descriptor>& data
)
{
    viu::_assert(xfer_ != nullptr);
    viu::_assert(usb::transfer::is_iso(xfer_));
    viu::_assert(
        std::size(data) <= static_cast<std::size_t>(xfer_->num_iso_packets)
    );

    for (std::size_t i = 0; i < std::size(data); ++i) {
        xfer_->iso_packet_desc[i] = data[i];
    }
}

} // namespace viu::usb::transfer
