export module viu.types;

import std;

namespace viu::type {

export template <typename T>
struct unique_pointer {
    using deleter_type = std::function<void(T*)>;
    using type = std::unique_ptr<T, deleter_type>;
};

export template <typename T>
using unique_pointer_t = unique_pointer<T>::type;

namespace numeric {

export template <typename T>
requires std::is_arithmetic_v<T>
constexpr auto max = std::numeric_limits<T>::max();

export template <typename T>
struct is_char {
    using type = std::remove_cvref_t<T>;
    static constexpr auto value = std::is_same_v<char, type> ||
                                  std::is_same_v<unsigned char, type>;
};

export template <typename T>
constexpr auto is_char_v = is_char<T>::value;

} // namespace numeric

} // namespace viu::type
