export module viu.format;

import std;

import viu.boost;

namespace viu::format {

template <typename T>
auto to_string(T&& arg) -> std::string
{
    if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
        return std::to_string(std::forward<T>(arg));
    } else {
        return std::forward<T>(arg);
    }
}

export template <typename... T>
auto make_string(T&&... args)
{
    const auto trim_right = [](std::string& s,
                               const char* t = " \t\n\r\f\v") -> auto {
        if (auto pos = s.find_last_not_of(t); pos != std::string::npos) {
            s.erase(pos + 1);
        }

        return s;
    };

    std::string text{};
    text += ((to_string(std::forward<T>(args)) + " ") + ...);
    return trim_right(text);
};

export auto is_hex(const std::string& s)
{
    if (s.starts_with("0x") && s.size() != 2) {
        try {
            constexpr auto base = 16;
            std::stoull(s, nullptr, base);
            return true;
        } catch ([[maybe_unused]] const std::invalid_argument& _) {
        } catch ([[maybe_unused]] const std::out_of_range& _) {
        }
    }

    return false;
}

namespace unsafe {

export template <typename T>
auto vectorize(const T* const begin, const std::size_t count)
{
    if (begin == nullptr || count == 0) {
        return std::vector<T>{};
    }

    return std::vector<T>{begin, begin + count};
}

} // namespace unsafe

namespace endian {

using boost::endian::big_to_native;
using boost::endian::little_to_native;
using boost::endian::native_to_big;
using boost::endian::native_to_little;

export auto to_big(const auto value) { return native_to_big(value); }
export auto from_big(const auto value) { return big_to_native(value); }
export auto from_little(const auto value) { return little_to_native(value); }
export auto to_little(const auto value) { return native_to_little(value); }

} // namespace endian

export template <std::integral T>
struct integral {
    template <std::uint8_t position>
    static constexpr auto at(std::integral auto from) -> T
    {
        using F = decltype(from);
        static_assert(sizeof(F) >= sizeof(T));
        static_assert(position < sizeof(F));

        const auto byte_left_shift_count = sizeof(F) - (position + 1);

        using common_type =
            std::common_type_t<decltype(sizeof(T)), decltype(position)>;
        const auto byte_right_shift_count =
            sizeof(F) -
            std::min(common_type{sizeof(T)}, common_type{position + 1});

        const auto bits = std::numeric_limits<std::uint8_t>::digits;

        from <<= (byte_left_shift_count * bits);
        from >>= (byte_right_shift_count * bits);

        return static_cast<T>(from);
    }

    static auto from_hex(const std::string& s) -> T
    {
        return boost::lexical_cast<from_hex_>(s);
    }

private:
    struct from_hex_ {
        T value;
        operator T() const { return value; }
        friend auto operator>>(std::istream& in, from_hex_& out)
            -> std::istream&
        {
            in >> std::hex >> out.value;
            return in;
        }
    };
};

} // namespace viu::format
