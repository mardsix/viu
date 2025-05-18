#include <gtest/gtest.h>

import std;

import viu.types;

namespace viu::test {

class type_traits_test : public testing::Test {
protected:
    template <typename...>
    struct typelist {};
};

TEST_F(type_traits_test, unique_pointer)
{
    using pointee_type = std::uint32_t;

    EXPECT_TRUE((std::is_same_v<
                 viu::type::unique_pointer_t<pointee_type>,
                 viu::type::unique_pointer<pointee_type>::type>));
    EXPECT_TRUE((std::is_same_v<
                 std::function<void(pointee_type*)>,
                 viu::type::unique_pointer<pointee_type>::deleter_type>));
}

TEST_F(type_traits_test, numeric)
{
    using types = typelist<
        char,
        unsigned char,
        int,
        unsigned int,
        long,
        unsigned long,
        long long,
        unsigned long long,
        std::int8_t,
        std::uint8_t,
        std::int16_t,
        std::uint16_t,
        std::int32_t,
        std::uint32_t,
        std::int64_t,
        std::uint64_t>;

    constexpr auto compare_equal =
        []<template <typename...> typename L, typename... T>(L<T...>) {
            using viu::type::numeric::max;
            using array_t = std::array<bool, sizeof...(T)>;
            const auto eq = array_t{max<T> == std::numeric_limits<T>::max()...};
            return std::all_of(std::begin(eq), std::end(eq), [](auto b) {
                return b;
            });
        }(types{});

    EXPECT_TRUE(compare_equal);

    const auto is_u8_char = viu::type::numeric::is_char<std::uint8_t>::value;
    EXPECT_EQ(is_u8_char, viu::type::numeric::is_char_v<std::uint8_t>);
    EXPECT_TRUE(is_u8_char);

    const auto is_u16_char = viu::type::numeric::is_char<std::uint16_t>::value;
    EXPECT_FALSE(is_u16_char);

    EXPECT_TRUE(viu::type::numeric::is_char_v<char>);
    EXPECT_TRUE(viu::type::numeric::is_char_v<unsigned char>);
    EXPECT_TRUE(viu::type::numeric::is_char_v<const char>);
    EXPECT_TRUE(viu::type::numeric::is_char_v<const unsigned char>);
    EXPECT_TRUE(viu::type::numeric::is_char_v<char&>);
    EXPECT_TRUE(viu::type::numeric::is_char_v<unsigned char&>);
    EXPECT_TRUE(viu::type::numeric::is_char_v<const char&>);
    EXPECT_TRUE(viu::type::numeric::is_char_v<const unsigned char&>);
}

} // namespace viu::test
