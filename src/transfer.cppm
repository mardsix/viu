module;

#include <libusb.h>

export module viu.transfer;

import std;

import viu.types;
import viu.usb.descriptors;

namespace viu::usb::transfer {

using deleter_type = std::function<void(libusb_transfer*)>;
export using pointer = std::unique_ptr<libusb_transfer, deleter_type>;

export using buffer_type = std::vector<std::uint8_t>;

export struct iso {
    std::int32_t packet_count{};
    buffer_type descriptors{};
};

export struct info {
    std::uint8_t ep_address{};
    buffer_type buffer{};
    std::function<void(transfer::pointer)> callback{};
    std::optional<iso> iso{};
};

auto number_of_packets(const libusb_transfer* const xfer) noexcept
{
    return xfer == nullptr ? 0 : xfer->num_iso_packets;
}

auto number_of_packets(const pointer& transfer) noexcept
{
    return number_of_packets(transfer.get());
}

export auto is_iso(const auto& obj) noexcept
{
    const auto num_of_packets = number_of_packets(obj);
    return (num_of_packets != 0) && (num_of_packets != 0xffffffff);
}

export auto iso_descriptor_size(const auto& obj) noexcept
{
    return number_of_packets(obj) * usb::descriptor::iso_descriptor_size();
}

export auto is_mock(const libusb_transfer* const transfer) -> bool;
export auto actual_length(const pointer& transfer) -> std::uint32_t;
export auto iso_data(const pointer& transfer) -> buffer_type;
export auto iso_descriptors(const pointer& transfer) -> usb::descriptor::iso;

export struct pending_map {
    using callback_type = std::function<void(transfer::pointer)>;
    using id_type = libusb_transfer*;
    using context_type = void*;

    void attach(
        const callback_type& cb,
        libusb_transfer* const transfer,
        void* user_data = nullptr
    );
    void submit(
        const viu::type::unique_pointer_t<libusb_context>& ctx,
        libusb_transfer* transfer
    );
    void cancel();
    void on_transfer_completed_impl(libusb_transfer* transfer);

private:
    void wait_for_canceled_transfers();
    auto give_away_transfer(libusb_transfer* const transfer)
        -> viu::usb::transfer::pointer;

    std::shared_mutex mutex_;
    std::map<id_type, callback_type> pending_transfers_;
    std::atomic_bool transfers_canceled_;
};

export struct control {
    control() = default;
    explicit control(libusb_transfer* xfer) : xfer_{xfer} {}
    void complete() const;
    [[nodiscard]] auto is_in() -> bool const;
    [[nodiscard]] auto is_out() -> bool const;
    void fill(const std::vector<std::uint8_t>& data);
    [[nodiscard]] auto read(std::optional<std::uint32_t> size = std::nullopt)
        -> transfer::buffer_type const;
    [[nodiscard]] auto size() const -> int;
    [[nodiscard]] auto type() const -> unsigned char;
    [[nodiscard]] auto ep() const -> std::uint8_t;
    [[nodiscard]] auto read_iso_packet_descriptors() const
        -> std::vector<libusb_iso_packet_descriptor>;
    [[nodiscard]] auto iso_packet_descriptor_count() const -> std::size_t;
    void fill_iso_packet_descriptors(
        const std::vector<libusb_iso_packet_descriptor>& data
    );

    void attach(
        const std::function<void(transfer::pointer)>& cb,
        pending_map& cbs,
        void* user_data = nullptr
    );
    void submit(
        const viu::type::unique_pointer_t<libusb_context>& ctx,
        pending_map& cbs
    );
    [[nodiscard]] auto underlying_transfer() const -> libusb_transfer*
    {
        return xfer_;
    }

private:
    static constexpr std::uint8_t direction_mask = 1 << 7;
    libusb_transfer* xfer_{};
};

export auto alloc(std::optional<int> iso_packets = std::nullopt)
    -> libusb_transfer*;

} // namespace viu::usb::transfer
