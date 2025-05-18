module;

#include <libusb.h>

module viu.usb.descriptors;

import viu.assert;
import viu.boost;
import viu.format;
import viu.io;
import viu.json;

namespace viu::usb::descriptor {

auto type_from_value(std::uint16_t value) -> libusb_descriptor_type
{
    return static_cast<libusb_descriptor_type>(
        viu::format::integral<std::uint8_t>::at<1>(value)
    );
}

auto index_from_value(std::uint16_t value) -> std::uint8_t
{
    return viu::format::integral<std::uint8_t>::at<0>(value);
}

auto is_audio(const libusb_endpoint_descriptor& ep_descriptor)
{
    return ep_descriptor.bLength == 0x09;
}

template <typename T>
constexpr void for_each_public_member(const auto& lambda)
{
    const auto describe_access = boost::describe::mod_public;
    using members = boost::describe::describe_members<T, describe_access>;
    boost::mp11::mp_for_each<members>(lambda);
}

constexpr void pack(
    described_integral_pod auto boost_described,
    vector_type& descriptor_data
)
{
    const auto to_le_bytes = [&](auto member_descriptor) {
        const auto number = boost_described.*member_descriptor.pointer;
        for (std::size_t i = 0; i < sizeof(number); ++i) {
            const auto byte = packing_type((number >> 8 * i) & 0xff);
            descriptor_data.push_back(byte);
        }
    };

    using T = decltype(boost_described);
    for_each_public_member<T>(to_le_bytes);
}

constexpr auto packed_size(described_integral_pod auto boost_described)
{
    auto length = std::uint8_t{};
    const auto sum_lengths = [&](auto member_descriptor) {
        length += sizeof(boost_described.*member_descriptor.pointer);
    };

    using T = decltype(boost_described);
    for_each_public_member<T>(sum_lengths);

    return length;
}

constexpr void stream_out(
    described_integral_pod auto boost_described,
    std::ostream& os
)
{
    const auto to_stream = [&](auto member_descriptor) {
        auto number = boost_described.*member_descriptor.pointer;

        using member_type = std::remove_reference_t<decltype(number)>;
        static_assert(std::is_integral_v<member_type>);

        io::text::stream::out(os, number);
    };

    using T = decltype(boost_described);
    for_each_public_member<T>(to_stream);
}

constexpr void stream_in(
    described_integral_pod auto& boost_described,
    std::istream& is
)
{
    const auto from_stream = [&](auto member_descriptor) {
        using member_type = std::remove_cvref_t<
            decltype(boost_described.*member_descriptor.pointer)>;

        static_assert(std::is_integral_v<member_type>);
        io::text::stream::in(is, boost_described.*member_descriptor.pointer);
    };

    using T = decltype(boost_described);
    for_each_public_member<std::remove_reference_t<T>>(from_stream);
}

void pack(
    libusb_endpoint_descriptor ep_descriptor,
    vector_type& descriptor_data
)
{
    if (is_audio(ep_descriptor)) {
        pack(
            static_cast<audio_endpoint_descriptor>(ep_descriptor),
            descriptor_data
        );
    } else {
        pack(static_cast<endpoint_descriptor>(ep_descriptor), descriptor_data);
    }
}

auto packed_size(libusb_endpoint_descriptor ep_descriptor)
{
    if (is_audio(ep_descriptor)) {
        auto descriptor = static_cast<audio_endpoint_descriptor>(ep_descriptor);
        return packed_size(descriptor);
    }

    auto descriptor = static_cast<endpoint_descriptor>(ep_descriptor);
    return packed_size(descriptor);
}

template <typename T, attribute extra_attr>
auto libusb_wrap<T, extra_attr>::extra() const -> extra_type
{
    static_assert(extra_attr == attribute::with_extra);
    return extra_vector_;
}

template <typename T, attribute extra_attr>
auto libusb_wrap<T, extra_attr>::extra_length() const -> extra_type::size_type
{
    static_assert(extra_attr == attribute::with_extra);
    return std::size(extra_vector_);
}

template <typename T, attribute extra_attr>
void libusb_wrap<T, extra_attr>::fill_extra(const extra_type& extra)
{
    static_assert(extra_attr == attribute::with_extra);
    extra_vector_ = extra;
}

template <typename T, attribute extra_attr>
void libusb_wrap<T, extra_attr>::stream_out_wrapped(std::ostream& os) const
{
    usb::descriptor::stream_out(wrapped(), os);
}

template <typename T, attribute extra_attr>
void libusb_wrap<T, extra_attr>::stream_in_wrapped(std::stringstream& is)
{
    usb::descriptor::stream_in(wrapped(), is);
}

template <typename T, attribute extra_attr>
void libusb_wrap<T, extra_attr>::pack_wrapped(vector_type& out) const
{
    usb::descriptor::pack(wrapped(), out);
}

template <typename T, attribute extra_attr>
void libusb_wrap<T, extra_attr>::stream_in_extra(std::stringstream& is)
{
    static_assert(extra_attr == attribute::with_extra);

    auto size = extra_type::size_type{};
    io::text::stream::in(is, size);
    for (extra_type::size_type i = 0; i < size; ++i) {
        auto element = extra_type::value_type{};
        io::text::stream::in(is, element);
        extra_vector_.push_back(element);
    }
}

template <typename T, attribute extra_attr>
void libusb_wrap<T, extra_attr>::stream_out_extra(std::ostream& os) const
{
    static_assert(extra_attr == attribute::with_extra);

    io::text::stream::out(os, std::size(extra_vector_));
    std::ranges::for_each(extra_vector_, [&os](const auto extra_byte) {
        io::text::stream::out(os, extra_byte);
    });
}

template <typename T, attribute extra_attr>
void libusb_wrap<T, extra_attr>::pack_extra(vector_type& out) const
{
    static_assert(extra_attr == attribute::with_extra);

    std::ranges::transform(
        extra_vector_,
        std::back_inserter(out),
        [](const auto extra_byte) {
            return vector_type::value_type{extra_byte};
        }
    );
}

template <typename R, typename... Args>
auto packer::member_fn_lambda(R (packer::*member_fn)(Args...))
{
    return [this, member_fn](Args&&... args) {
        return (this->*member_fn)(std::forward<Args>(args)...);
    };
}

// clang-format off
template <typename T>
concept holds_wrapped_type =
    std::is_base_of_v<
        libusb_wrap<typename T::underlying_type, attribute::without_extra>, T
    > ||
    std::is_base_of_v<
        libusb_wrap<typename T::underlying_type, attribute::with_extra>, T
    >;
// clang-format on

template <
    typename W,
    attribute wrapped,
    attribute extra,
    typename K,
    typename V>
void basic_descriptor<W, wrapped, extra, K, V>::stream_out(
    std::ostream& os
) const
{
    if constexpr (wrapped == attribute::with_wrapped) {
        libusb_wrap<W, extra>::stream_out_wrapped(os);
    }

    const auto stream = [&os](const auto& p) {
        using T =
            typename std::remove_reference_t<decltype(p.vec())>::value_type;
        io::text::stream::out(os, std::size(p.vec()));
        std::ranges::for_each(p.vec(), [&os](const auto& e) {
            if constexpr (holds_wrapped_type<T>) {
                e.stream_out(os);
            } else {
                io::text::stream::out(os, e);
            }
        });
    };

    viu::vector::plugin<K, V>::for_each(stream);

    if constexpr (extra == attribute::with_extra) {
        libusb_wrap<W, extra>::stream_out_extra(os);
    }
}

template <
    typename W,
    attribute wrapped,
    attribute extra,
    typename K,
    typename V>
void basic_descriptor<W, wrapped, extra, K, V>::stream_in(std::stringstream& is)
{
    if constexpr (wrapped == attribute::with_wrapped) {
        libusb_wrap<W, extra>::stream_in_wrapped(is);
    }

    viu::vector::plugin<K, V>::for_each([&is](auto& p) {
        using vector_type = std::remove_reference_t<decltype(p.vec())>;
        using T = typename vector_type::value_type;
        auto size = typename vector_type::size_type{};
        io::text::stream::in(is, size);
        for (typename vector_type::size_type i = 0; i < size; ++i) {
            auto e = T{};
            if constexpr (holds_wrapped_type<T>) {
                e.stream_in(is);
            } else {
                io::text::stream::in(is, e);
            }
            p.vec().push_back(e);
        }
    });

    if constexpr (extra == attribute::with_extra) {
        libusb_wrap<W, extra>::stream_in_extra(is);
    }
}

template <
    typename W,
    attribute wrapped,
    attribute extra,
    typename K,
    typename V>
void basic_descriptor<W, wrapped, extra, K, V>::pack(vector_type& out) const
{
    if constexpr (wrapped == attribute::with_wrapped) {
        libusb_wrap<W, extra>::pack_wrapped(out);
    }

    if constexpr (extra == attribute::with_extra) {
        libusb_wrap<W, extra>::pack_extra(out);
    }

    viu::vector::plugin<K, V>::for_each([&out](const auto& p) {
        using T =
            typename std::remove_reference_t<decltype(p.vec())>::value_type;
        if constexpr (holds_wrapped_type<T>) {
            std::ranges::for_each(p.vec(), [&out](const auto& e) {
                e.pack(out);
            });
        } else {
            std::ranges::transform(
                p.vec(),
                std::back_inserter(out),
                [](const auto b) { return vector_type::value_type{b}; }
            );
        }
    });
}

void packer::pack(const libusb_device_descriptor& device_descriptor)
{
    usb::descriptor::pack(device_descriptor, packed_data_);
}

void packer::pack(const libusb_config_descriptor& config_descriptor)
{
    usb::descriptor::pack(config_descriptor, packed_data_);
    pack_extra(config_descriptor);
    pack_interfaces(config_descriptor);
}

void packer::pack(const usb::descriptor::config& wrapped_config_descriptor)
{
    wrapped_config_descriptor.pack(packed_data_);
}

void packer::pack(const libusb_bos_descriptor* const bos_descriptor)
{
    viu::_assert(bos_descriptor != nullptr);
    usb::descriptor::pack(*bos_descriptor, packed_data_);

    auto dev_caps = viu::format::unsafe::vectorize(
        bos_descriptor->dev_capability,
        bos_descriptor->bNumDeviceCaps
    );

    for (auto dev_cap_desc : dev_caps) {
        viu::_assert(dev_cap_desc != nullptr);
        pack_bos_dev_capability_descriptor(dev_cap_desc);
    }
}

void packer::pack(const usb::descriptor::bos& wrapped_bos_descriptor)
{
    wrapped_bos_descriptor.pack(packed_data_);
}

void packer::pack_extra(const descriptor_with_extra auto& descriptor)
{
    if (descriptor.extra_length == 0) {
        return;
    }

    const auto extra = viu::format::unsafe::vectorize(
        descriptor.extra,
        descriptor.extra_length
    );

    std::ranges::transform(
        extra,
        std::back_inserter(packed_data_),
        [](const std::integral auto b) { return packing_type{b}; }
    );
}

void packer::pack_endpoint_descriptor(
    const libusb_endpoint_descriptor& ep_descriptor
)
{
    usb::descriptor::pack(ep_descriptor, packed_data_);
    pack_extra(ep_descriptor);
}

void packer::pack_interface_descriptor(
    const libusb_interface_descriptor& interface_descriptor
)
{
    usb::descriptor::pack(interface_descriptor, packed_data_);
    pack_extra(interface_descriptor);

    const auto endpoints = viu::format::unsafe::vectorize(
        interface_descriptor.endpoint,
        interface_descriptor.bNumEndpoints
    );

    std::ranges::for_each(
        endpoints,
        member_fn_lambda(&packer::pack_endpoint_descriptor)
    );
}

void packer::pack_interface(const libusb_interface& interface)
{
    const auto altsettings = viu::format::unsafe::vectorize(
        interface.altsetting,
        interface.num_altsetting
    );

    std::ranges::for_each(
        altsettings,
        member_fn_lambda(&packer::pack_interface_descriptor)
    );
}

void packer::pack_interfaces(const libusb_config_descriptor& config_descriptor)
{
    const auto ifaces = viu::format::unsafe::vectorize(
        config_descriptor.interface,
        config_descriptor.bNumInterfaces
    );

    std::ranges::for_each(ifaces, member_fn_lambda(&packer::pack_interface));
}

void packer::pack_bos_dev_capability_descriptor(
    const libusb_bos_dev_capability_descriptor* dev_cap_desc
)
{
    viu::_assert(dev_cap_desc != nullptr);

    usb::descriptor::pack(*dev_cap_desc, packed_data_);

    const auto cap_data_start = dev_cap_desc->dev_capability_data;
    const auto cap_data_size = dev_cap_desc->bLength -
                               usb::descriptor::packed_size(*dev_cap_desc);

    const auto cap_data =
        viu::format::unsafe::vectorize(cap_data_start, cap_data_size);

    std::ranges::transform(
        cap_data,
        std::back_inserter(packed_data_),
        [](const std::integral auto b) { return packing_type{b}; }
    );
}

namespace streamer {

struct out {
    explicit out(const std::filesystem::path& path)
        : os_{path, std::ios_base::binary}
    {
    }

    void stream(const config& device_config)
    {
        viu::_assert(os_.is_open());
        device_config.stream_out(os_);
    }

    void stream(const libusb_device_descriptor& device_descriptor)
    {
        viu::_assert(os_.is_open());
        usb::descriptor::stream_out(device_descriptor, os_);
    }

    void stream(const string_descriptor_map& string_desc)
    {
        viu::_assert(os_.is_open());

        const auto stream_out_vector = [this](const auto& v) {
            io::text::stream::out(os_, std::size(v));
            std::ranges::for_each(v, [this](const auto e) {
                io::text::stream::out(os_, e);
            });
        };

        io::text::stream::out(os_, string_desc.size());
        for (const auto& p : string_desc) {
            io::text::stream::out(os_, p.first);
            io::text::stream::out(os_, std::size(p.second));
            std::ranges::for_each(p.second, stream_out_vector);
        }
    }

    void stream(const std::vector<std::uint8_t>& report_descriptor)
    {
        viu::_assert(os_.is_open());
        io::text::stream::out(os_, std::size(report_descriptor));
        std::ranges::for_each(report_descriptor, [this](const auto e) {
            io::text::stream::out(os_, e);
        });
    }

    void stream(const bos& bos_descriptor)
    {
        viu::_assert(os_.is_open());
        bos_descriptor.stream_out(os_);
    }

private:
    std::ofstream os_{};
};

struct in {
    explicit in(const std::filesystem::path& path)
    {
        if (path.extension() == ".json") {
            from_json(path);
        } else {
            auto in = std::ifstream{path, std::ios_base::binary};
            viu::_assert(in.is_open());
            is_ << in.rdbuf();
        }
    }

    void stream(config& config_desc) { config_desc.stream_in(is_); }
    void stream(bos& bos_desc) { bos_desc.stream_in(is_); }

    void stream(string_descriptor_map& string_descs)
    {
        using map_size_type = string_descriptor_map::size_type;
        using vector_size_type = std::vector<std::uint8_t>::size_type;

        auto map_size = map_size_type{};
        io::text::stream::in(is_, map_size);

        const auto stream_in_vector = [this]() -> std::vector<std::uint8_t> {
            auto v = std::vector<std::uint8_t>{};
            auto vector_size = vector_size_type{};
            io::text::stream::in(is_, vector_size);
            for (auto i = vector_size_type{0}; i < vector_size; ++i) {
                auto e = std::uint8_t{};
                io::text::stream::in(is_, e);
                v.push_back(e);
            }

            return v;
        };

        for (auto i = map_size_type{0}; i < map_size; ++i) {
            auto sd = string_descriptor_type{};
            auto lang_id = string_descriptor_map::key_type{};
            io::text::stream::in(is_, lang_id);
            auto size = vector_size_type{};
            io::text::stream::in(is_, size);
            for (auto j = vector_size_type{0}; j < size; ++j) {
                const auto str_descs = stream_in_vector();
                sd.push_back(str_descs);
            }

            string_descs.insert(std::make_pair(lang_id, sd));
        }
    }

    void stream(std::vector<std::uint8_t>& report_descriptor)
    {
        using vector_size_type = std::vector<std::uint8_t>::size_type;
        auto vector_size = vector_size_type{};
        io::text::stream::in(is_, vector_size);
        for (auto i = vector_size_type{0}; i < vector_size; ++i) {
            auto e = std::uint8_t{};
            io::text::stream::in(is_, e);
            report_descriptor.push_back(e);
        }
    }

    void stream(libusb_device_descriptor& device_descriptor)
    {
        usb::descriptor::stream_in(device_descriptor, is_);
    }

private:
    void parse_json(boost::property_tree::ptree const& pt)
    {
        using boost::property_tree::ptree;

        std::for_each(
            std::cbegin(pt),
            std::cend(pt),
            [this](const ptree::const_iterator::value_type& it) {
                if (!it.second.get_value<std::string>().empty()) {
                    auto f = it.second.get_value<std::string>();
                    if (format::is_hex(f)) {
                        f = std::to_string(
                            format::integral<std::uint64_t>::from_hex(f)
                        );
                    }

                    is_ << f << " ";
                }

                parse_json(it.second);
            }
        );
    }

    void from_json(const std::filesystem::path& path)
    {
        try {
            std::stringstream ss;
            std::ifstream file(path);

            std::string data(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>()
            );

            viu::json::parser{is_}.parse(data);
        } catch (std::exception const& e) {
            std::println(std::cerr, "Failed to parse json: {}", e.what());
        }
    }

    std::stringstream is_{};
};

} // namespace streamer

tree::tree(
    libusb_device_descriptor device_desc,
    const config_descriptor_pointer& config_desc,
    string_descriptor_map string_descs,
    const bos_descriptor_pointer& bos_desc,
    std::vector<std::uint8_t> report_desc
)
    : device_desc_{std::move(device_desc)},
      string_descs_{std::move(string_descs)},
      report_desc_{std::move(report_desc)}
{
    build(config_desc);
    build(bos_desc);
}

void tree::save(const std::filesystem::path& path) const
{
    auto os = streamer::out{path};
    os.stream(device_descriptor());
    os.stream(device_config());
    os.stream(string_descriptors());
    os.stream(report_descriptor());
    os.stream(bos_descriptor());
}

void tree::load(const std::filesystem::path& path)
{
    auto is = streamer::in{path};
    is.stream(device_desc_);
    is.stream(wrapped_config_desc_);
    is.stream(string_descs_);
    is.stream(report_desc_);
    is.stream(wrapped_bos_desc_);
}

auto tree::vector_of_extra(const descriptor_with_extra auto& desc)
{
    return viu::format::unsafe::vectorize(desc.extra, desc.extra_length);
}

auto tree::build(const libusb_endpoint_descriptor& ep)
{
    auto wrapped_ep = endpoint{};
    wrapped_ep.fill_extra(vector_of_extra(ep));
    wrapped_ep.wrap(ep);
    return wrapped_ep;
}

auto tree::build(const libusb_interface_descriptor iface_desc)
{
    auto wrapped_iface_desc = interface{};
    wrapped_iface_desc.fill_extra(vector_of_extra(iface_desc));

    const auto eps = viu::format::unsafe::vectorize(
        iface_desc.endpoint,
        iface_desc.bNumEndpoints
    );

    auto ep_vector = std::vector<endpoint>{};
    std::ranges::for_each(eps, [this, &ep_vector](const auto& ep) {
        ep_vector.push_back(build(ep));
    });

    wrapped_iface_desc.fill(usb::descriptor::key::ep, ep_vector);
    wrapped_iface_desc.wrap(iface_desc);

    return wrapped_iface_desc;
}

auto tree::build(const libusb_interface& usb_iface)
{
    auto wrapped_usb_iface = usb_interface{};
    const auto altsettings = viu::format::unsafe::vectorize(
        usb_iface.altsetting,
        usb_iface.num_altsetting
    );

    auto altsetting_vector = std::vector<interface>{};
    std::ranges::for_each(
        altsettings,
        [this, &altsetting_vector](const auto& iface_desc) {
            altsetting_vector.push_back(build(iface_desc));
        }
    );

    wrapped_usb_iface.fill(usb::descriptor::key::altsetting, altsetting_vector);
    wrapped_usb_iface.wrap(usb_iface);

    return wrapped_usb_iface;
}

void tree::build(const config_descriptor_pointer& config_desc)
{
    viu::_assert(config_desc != nullptr);
    wrapped_config_desc_.fill_extra(vector_of_extra(*config_desc));

    viu::_assert(config_desc->interface != nullptr);
    const auto ifaces = viu::format::unsafe::vectorize(
        config_desc->interface,
        config_desc->bNumInterfaces
    );

    auto interface_vector = std::vector<usb_interface>{};
    std::ranges::for_each(ifaces, [this, &interface_vector](const auto& iface) {
        interface_vector.push_back(build(iface));
    });

    wrapped_config_desc_.fill(key::interface, interface_vector);
    wrapped_config_desc_.wrap(*config_desc);
}

auto tree::build(const libusb_bos_dev_capability_descriptor* const dev_cap_desc)
{
    viu::_assert(dev_cap_desc != nullptr);

    auto dev_cap = bos_dev_capability_descriptor{};

    dev_cap.fill(
        key::dev_capability_data,
        viu::format::unsafe::vectorize(
            dev_cap_desc->dev_capability_data,
            dev_cap_desc->bLength - 3
        )
    );

    dev_cap.wrap(*dev_cap_desc);
    return dev_cap;
}

void tree::build(const bos_descriptor_pointer& bos_desc)
{
    if (bos_desc == nullptr) {
        return;
    }

    const auto dev_caps = viu::format::unsafe::vectorize(
        bos_desc->dev_capability,
        bos_desc->bNumDeviceCaps
    );

    auto dev_cap_vector = std::vector<bos_dev_capability_descriptor>{};
    std::ranges::for_each(
        dev_caps,
        [this, &dev_cap_vector](const auto& dev_cap_desc) {
            dev_cap_vector.push_back(build(dev_cap_desc));
        }
    );

    wrapped_bos_desc_.fill(key::dev_capability, dev_cap_vector);
    wrapped_bos_desc_.wrap(*bos_desc);
}

} // namespace viu::usb::descriptor
