
module;

#include <libusb.h>

export module viu.usb.descriptors:descriptor_classes;

import std;

import viu.types;
import viu.vector;

import :structs;
import :traits;
import :types;

namespace viu::usb::descriptor {

export [[nodiscard]] auto type_from_value(std::uint16_t value)
    -> libusb_descriptor_type;
export [[nodiscard]] auto index_from_value(std::uint16_t value) -> std::uint8_t;

enum class attribute : std::uint8_t {
    with_wrapped,
    without_wrapped,
    with_extra,
    without_extra
};

template <typename T, attribute extra_attr = attribute::without_extra>
struct libusb_wrap {
    using underlying_type = T;
    using extra_type = std::vector<std::uint8_t>;

    virtual void wrap(const T& w) noexcept { wrapped_ = w; };

    [[nodiscard]] auto extra() const -> extra_type;
    [[nodiscard]] auto extra_length() const -> extra_type::size_type;
    void fill_extra(const extra_type& extra);

protected:
    void stream_out_wrapped(std::ostream& os) const;
    void stream_in_wrapped(std::stringstream& is);
    void pack_wrapped(vector_type& out) const;
    void stream_in_extra(std::stringstream& is);
    void stream_out_extra(std::ostream& os) const;
    void pack_extra(vector_type& out) const;
    [[nodiscard]] auto wrapped() const noexcept { return wrapped_; }
    [[nodiscard]] auto wrapped() noexcept -> T& { return wrapped_; }

private:
    T wrapped_{};
    extra_type extra_vector_;
};

template <
    typename W,
    attribute wrapped,
    attribute extra,
    typename K,
    typename V>
struct basic_descriptor : libusb_wrap<W, extra>, viu::vector::plugin<K, V> {
    void stream_out(std::ostream& os) const;
    void stream_in(std::stringstream& is);
    void pack(vector_type& out) const;
};

template <typename W, typename K, typename V>
struct basic_descriptor<
    W,
    attribute::without_wrapped,
    attribute::with_extra,
    K,
    V> {
    static_assert(
        false,
        "Conflicting attributes. "
        "Cannot have extra descriptor data in non-data wrapping descriptor"
    );
};

namespace key {

export constexpr auto ep = std::string_view{"endpoint"};
export constexpr auto interface = std::string_view{"interface"};
export constexpr auto dev_capability_data = std::string_view{
    "dev_capability_data"
};
export constexpr auto dev_capability = std::string_view{"dev_capability"};
export constexpr auto altsetting = std::string_view{"altsetting"};

} // namespace key

// clang-format off
export struct endpoint final : basic_descriptor<
    libusb_endpoint_descriptor,
    attribute::with_wrapped,
    attribute::with_extra,
    viu::vector::empty_list,
    viu::vector::empty_list
> {
    [[nodiscard]] auto address() const noexcept
    {
        return wrapped().bEndpointAddress;
    }

    [[nodiscard]] auto attributes() const noexcept
    {
        return wrapped().bmAttributes;
    }
};

export struct interface final : basic_descriptor<
    libusb_interface_descriptor,
    attribute::with_wrapped,
    attribute::with_extra,
    viu::vector::key_list<key::ep>,
    viu::vector::type_list<endpoint>
> {};

export struct usb_interface final : basic_descriptor<
    libusb_interface,
    attribute::without_wrapped,
    attribute::without_extra,
    viu::vector::key_list<key::altsetting>,
    viu::vector::type_list<interface>
> {};

export struct config final : basic_descriptor<
    libusb_config_descriptor,
    attribute::with_wrapped,
    attribute::with_extra,
    viu::vector::key_list<key::interface>,
    viu::vector::type_list<usb_interface>
> {
    static constexpr auto self_powered_mask = std::uint8_t{0b10000000};

    [[nodiscard]] auto is_self_powered() -> bool
    {
        return (wrapped().bmAttributes & self_powered_mask) != 0;
    }
};

export struct bos_dev_capability_descriptor final : basic_descriptor<
    libusb_bos_dev_capability_descriptor,
    attribute::with_wrapped,
    attribute::without_extra,
    viu::vector::key_list<key::dev_capability_data>,
    viu::vector::type_list<std::uint8_t>
> {};

export struct bos final : basic_descriptor<
    libusb_bos_descriptor,
    attribute::with_wrapped,
    attribute::without_extra,
    viu::vector::key_list<key::dev_capability>,
    viu::vector::type_list<bos_dev_capability_descriptor>
> {};
// clang-format on

} // namespace viu::usb::descriptor
