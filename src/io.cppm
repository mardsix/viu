module;

import std;

import viu.boost;
import viu.types;

export module viu.io;

namespace viu::io {

namespace text::stream {

using char_holder_type = std::int32_t;

export void out(std::ostream& os, const std::integral auto& v)
{
    using T = std::remove_cvref_t<decltype(v)>;
    if constexpr (viu::type::numeric::is_char_v<T>) {
        os << static_cast<char_holder_type>(v) << " ";
    } else {
        os << v << " ";
    }
}

export void in(std::istream& is, std::integral auto& v)
{
    auto number = std::string{};
    is >> number;
    using T = std::remove_cvref_t<decltype(v)>;
    if constexpr (viu::type::numeric::is_char_v<T>) {
        v = static_cast<T>(boost::lexical_cast<char_holder_type>(number));
    } else {
        v = boost::lexical_cast<T>(number);
    }
}

} // namespace text::stream

namespace bin::file {

export template <typename T>
bool save(const std::filesystem::path& path, const std::vector<T>& data)
{
    static_assert(
        std::is_trivially_copyable_v<T>,
        "T must be trivially copyable"
    );

    auto file = std::ofstream{path, std::ios::binary};
    if (!file) {
        return false;
    }

    file.write(
        reinterpret_cast<const char*>(data.data()),
        static_cast<std::streamsize>(data.size() * sizeof(T))
    );

    return file.good();
}

} // namespace bin::file

} // namespace viu::io
