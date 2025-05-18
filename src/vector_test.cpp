#include <gmock/gmock.h>
#include <gtest/gtest.h>

import std;

import viu.vector;

namespace viu::test {

class vector_test : public testing::Test {};

namespace key {

constexpr auto u8 = std::string_view{"std::uint8_t"};
constexpr auto i8 = std::string_view{"std::int8_t"};
constexpr auto u16 = std::string_view{"std::uint16_t"};
constexpr auto i16 = std::string_view{"std::int16_t"};
constexpr auto u32 = std::string_view{"std::uint32_t"};
constexpr auto i32 = std::string_view{"std::int32_t"};
constexpr auto u64 = std::string_view{"std::uint64_t"};
constexpr auto i64 = std::string_view{"std::int64_t"};
constexpr auto int_ = std::string_view{"int"};
constexpr auto long_ = std::string_view{"long"};
constexpr auto int_duplicate = std::string_view{"int_duplicate"};
constexpr auto long_duplicate = std::string_view{"long_duplicate"};

} // namespace key

TEST_F(vector_test, plugin_fill)
{
    using keys = viu::vector::key_list<
        key::int_duplicate,
        key::long_duplicate,
        key::u8,
        key::u16,
        key::u32,
        key::u64,
        key::int_,
        key::long_>;
    using types = viu::vector::type_list<
        int,
        long,
        std::uint8_t,
        std::uint16_t,
        std::uint32_t,
        std::uint64_t,
        int,
        long>;

    []<const std::string_view&... K, typename... V>(
        [[maybe_unused]] viu::vector::key_list<K...> k,
        [[maybe_unused]] viu::vector::type_list<V...> v
    ) {
        using array_of_vectors = std::array<
            std::variant<
                std::vector<int>,
                std::vector<long>,
                std::vector<std::uint8_t>,
                std::vector<std::uint16_t>,
                std::vector<std::uint32_t>,
                std::vector<std::uint64_t>>,
            sizeof...(K)>;
        std::array<std::string_view, sizeof...(K)> keys{K...};

        auto vector_plugin = viu::vector::plugin<decltype(k), decltype(v)>{};
        auto expected = array_of_vectors{std::vector<V>{0, 1, 2, 3, 4, 5}...};
        (vector_plugin.fill(K, std::vector<V>{0, 1, 2, 3, 4, 5}), ...);

        auto key_index = 0;
        for (const auto& v : expected) {
            std::visit(
                [&](const auto& vec) {
                    auto read_back_vec = decltype(vec){};
                    vector_plugin.read(keys.at(key_index++), read_back_vec);
                    EXPECT_EQ(read_back_vec, vec);
                },
                v
            );
        }
    }(keys{}, types{});
}

TEST_F(vector_test, for_each)
{
    using keys = viu::vector::
        key_list<key::i8, key::i16, key::i32, key::i64, key::int_, key::long_>;
    using types = viu::vector::type_list<
        std::int8_t,
        std::int16_t,
        std::int32_t,
        std::int64_t,
        int,
        long>;

    []<const std::string_view&... K, typename... V>(
        [[maybe_unused]] viu::vector::key_list<K...> k,
        [[maybe_unused]] viu::vector::type_list<V...> v
    ) {
        auto vector_plugin = viu::vector::plugin<decltype(k), decltype(v)>{};
        const auto expected = std::array<std::intmax_t, 6>{0, 2, 4, 6, 8, 10};
        (vector_plugin.fill(K, std::vector<V>{0, 1, 2, 3, 4, 5}), ...);

        vector_plugin.for_each([](auto& entry) {
            std::ranges::transform(
                entry.vec(),
                std::begin(entry.vec()),
                std::bind_front(std::multiplies<>{}, 2)
            );
        });

        const auto compare_to_double = [&expected](const auto& entry) {
            const auto& vec = entry.vec();
            EXPECT_THAT(vec, ::testing::ElementsAreArray(expected));
        };

        vector_plugin.for_each(compare_to_double);

        const auto const_vector_plugin =
            viu::vector::plugin<decltype(k), decltype(v)>{
                std::vector<V>{0, 2, 4, 6, 8, 10}...
            };

        const_vector_plugin.for_each(compare_to_double);
    }(keys{}, types{});
}

} // namespace viu::test
