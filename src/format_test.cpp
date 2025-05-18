#include <gmock/gmock.h>
#include <gtest/gtest.h>

import std;

import viu.format;

namespace viu::test {

class format_test : public testing::Test {
protected:
    std::vector<std::variant<
        std::int8_t,
        std::uint8_t,
        std::int16_t,
        std::uint16_t,
        std::int32_t,
        std::uint32_t,
        std::int64_t,
        std::uint64_t>>
        integrals_{
            std::int8_t{0x70},
            std::uint8_t{0x70},
            std::uint16_t{0x7170},
            std::int16_t{0x7170},
            std::int32_t{0x73727170},
            std::uint32_t{0x73727170},
            std::int64_t{0x7776757473727170},
            std::uint64_t{0x7776757473727170}
        };
};

TEST_F(format_test, make_string)
{
    EXPECT_EQ(format::make_string(1, 2, 3, 'a', "bc"), "1 2 3 97 bc");
    EXPECT_EQ(
        format::make_string("one,", "two,", 3, "five", 6),
        "one, two, 3 five 6"
    );
}

TEST_F(format_test, is_hex)
{
    EXPECT_TRUE(format::is_hex("0xff"));
    EXPECT_TRUE(format::is_hex("0x0"));
    EXPECT_TRUE(format::is_hex("0xffffffffffffffff"));
    EXPECT_FALSE(format::is_hex("0x"));
    EXPECT_FALSE(format::is_hex("fa"));
    EXPECT_FALSE(format::is_hex("0"));
    EXPECT_FALSE(format::is_hex("128"));

    EXPECT_FALSE(
        format::is_hex(
            std::string{"0x1"} + std::string(2 * sizeof(std::uintmax_t), 'f')
        )
    );
}

TEST_F(format_test, vectorize)
{
    const auto visitor = [](std::integral auto i) {
        using format::unsafe::vectorize;
        using T = decltype(i);

        const auto expect = std::array<T, 6>{0, 1, 2, 3, 4, 5};
        const auto size = std::size(expect);
        const auto vec = vectorize(expect.data(), size);
        EXPECT_THAT(vec, ::testing::ElementsAreArray(expect));

        using value_type = typename decltype(vec)::value_type;
        EXPECT_TRUE((std::is_same_v<T, value_type>));

        const T* null{};
        const auto empty_vec = vectorize(null, size);
        EXPECT_EQ(std::size(empty_vec), 0);

        using empty_vec_value_type = typename decltype(empty_vec)::value_type;
        EXPECT_TRUE((std::is_same_v<T, empty_vec_value_type>));

        using size_type = typename decltype(expect)::size_type;
        for (auto i = size_type{0}; i < std::size(expect); ++i) {
            const auto first_n = std::span{std::begin(expect), i};
            const auto vec_n = vectorize(first_n.data(), std::size(first_n));
            EXPECT_THAT(vec_n, ::testing::ElementsAreArray(first_n));
            EXPECT_EQ(std::size(vec_n), i);

            const auto empty_vec_n = vectorize(null, i);
            EXPECT_EQ(std::size(empty_vec_n), 0);
        }
    };

    for (const auto i : integrals_) {
        std::visit(visitor, i);
    }
}

TEST_F(format_test, endianess_0)
{
    EXPECT_EQ(format::endian::to_little(0), 0);
    EXPECT_EQ(format::endian::to_big(0), 0);
    EXPECT_EQ(format::endian::from_little(0), 0);
    EXPECT_EQ(format::endian::from_big(0), 0);
}

TEST_F(format_test, endianess_little)
{
    if constexpr (std::endian::native == std::endian::big) {
        GTEST_SKIP();
    }

    const auto visitor = [](const std::integral auto i) {
        EXPECT_EQ(format::endian::to_little(i), i);
        EXPECT_EQ(format::endian::to_big(i), std::byteswap(i));
        EXPECT_EQ(format::endian::from_little(i), i);
        EXPECT_EQ(format::endian::from_big(i), std::byteswap(i));
        EXPECT_EQ(format::endian::from_big(format::endian::to_big(i)), i);
        EXPECT_EQ(format::endian::to_big(format::endian::from_big(i)), i);
    };

    for (const auto i : integrals_) {
        std::visit(visitor, i);
    }
}

TEST_F(format_test, int_at)
{
    const auto expected_at = []<typename T>(std::uint8_t position) {
        using common_type =
            std::common_type_t<decltype(sizeof(T)), decltype(position)>;
        auto size = std::min(common_type{sizeof(T) - 1}, common_type{position});
        auto pattern = T{0x70} + position;
        const auto bits = std::numeric_limits<std::uint8_t>::digits;
        auto expected = pattern << (size * bits);

        while (size != 0) {
            expected |= --pattern << (--size * bits);
        }

        return expected;
    };

    const auto at = [&]<typename T, std::uint8_t first, std::uint8_t last>(
                        this const auto& self,
                        std::integral auto i
                    ) {
        if constexpr (first < last) {
            const auto val = format::integral<T>::template at<first>(i);
            const auto expected = expected_at.template operator()<T>(first);
            EXPECT_EQ(val, expected);
            EXPECT_TRUE((std::is_same_v<T, decltype(val)>));
            self.template operator()<T, first + 1, last>(i);
        }
    };

    const auto value_visitor = [&](const std::integral auto v) {
        const auto type_visitor = [&](const std::integral auto t) {
            using from_t = decltype(t);
            if constexpr (sizeof(from_t) <= sizeof(v)) {
                at.template operator()<from_t, 0, sizeof(v)>(v);
            }
        };

        for (const auto t : integrals_) {
            std::visit(type_visitor, t);
        }
    };

    for (const auto v : integrals_) {
        std::visit(value_visitor, v);
    }
}

} // namespace viu::test
