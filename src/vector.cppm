export module viu.vector;

import std;

namespace viu::vector {

export template <typename...>
struct type_list {};

export template <const std::string_view&...>
struct key_list {};

export struct empty_list {};

export template <typename K, typename V>
struct plugin;

export template <>
struct plugin<empty_list, empty_list> {
    template <typename U>
    void fill(const std::string_view& key, const std::vector<U>& in)
    {
        static_assert(false, "Cannot fill empty vector plugin");
    }
    template <typename U>
    void read(const std::string_view& key, std::vector<U>& out) const
    {
        static_assert(false, "Cannot read from empty vector plugin");
    }
    void for_each([[maybe_unused]] const auto& visitor) {}
    void for_each([[maybe_unused]] const auto& visitor) const {}
};

enum class flow_control : std::uint8_t { break_, continue_, loop };

template <typename T>
struct member_entry {
    member_entry(const std::string_view& key) : key_{key} {}
    auto key() const { return key_; }
    auto vec() noexcept -> auto& { return vector_; }
    auto vec() const noexcept -> const auto& { return vector_; }

private:
    std::string key_;
    std::vector<T> vector_{};
};

struct check_keys {
protected:
    template <const std::string_view& X, const std::string_view& Y>
    struct not_equal {
        static constexpr auto value = (X != Y);
    };

    template <const std::string_view& T, const std::string_view&... L>
    struct keys_are_unique
        : std::conjunction<not_equal<T, L>..., keys_are_unique<L...>> {};

    template <const std::string_view& T>
    struct keys_are_unique<T> : std::true_type {};

    template <const std::string_view& T, const std::string_view&... L>
    static constexpr auto keys_are_unique_v = keys_are_unique<T, L...>::value;
};

struct filter_types {
protected:
    template <typename T, typename... L>
    struct unique : std::type_identity<T> {};

    template <typename... T, typename H, typename... L>
    struct unique<std::variant<T...>, H, L...>
        : std::conditional_t<
              std::disjunction_v<std::is_same<H, T>...>,
              unique<std::variant<T...>, L...>,
              unique<std::variant<T..., H>, L...>> {};

    template <typename... T>
    using unique_types = unique<std::variant<>, T...>::type;
};

export template <const std::string_view&... K, typename... V>
struct plugin<key_list<K...>, type_list<V...>> : check_keys, filter_types {
public:
    using member_type = unique_types<member_entry<V>...>;
    using type = std::vector<member_type>;

    // c++26
    // static_assert(std::size(std::set<std::string_view>{K...}) ==
    // sizeof...(K));
    static_assert(keys_are_unique_v<K...>, "Keys must be unique");

    plugin() = default;

    explicit plugin(const std::vector<V>&... vectors)
    {
        (fill(K, vectors), ...);
    }

    template <typename U>
    void fill(const std::string_view& key, const std::vector<U>& in)
    {
        const auto predicate = [&key](const auto& entry) -> auto {
            return entry.key() == key;
        };

        const auto fill_visitor = [&in, &predicate](auto&& entry) -> auto {
            using value_type = typename std::remove_reference_t<
                decltype(entry.vec())>::value_type;
            if constexpr (std::is_same_v<U, value_type>) {
                if (predicate(entry)) {
                    entry.vec() = in;
                    return flow_control::break_;
                }
            }

            return flow_control::loop;
        };

        for (auto& elem : vector_) {
            const auto flow_ctrl = std::visit(fill_visitor, elem);

            if (flow_ctrl == flow_control::break_) {
                break;
            }
        }
    }

    template <typename U>
    void read(const std::string_view& key, std::vector<U>& out) const
    {
        const auto predicate = [&key](const auto& entry) -> auto {
            return entry.key() == key;
        };

        const auto read_visitor = [&out, &predicate](auto&& entry) -> auto {
            using value_type = typename std::remove_reference_t<
                decltype(entry.vec())>::value_type;
            if constexpr (std::is_same_v<U, value_type>) {
                if (predicate(entry)) {
                    out = entry.vec();
                    return flow_control::break_;
                }
            }

            return flow_control::loop;
        };

        for (auto& elem : vector_) {
            const auto flow_ctrl = std::visit(read_visitor, elem);

            if (flow_ctrl == flow_control::break_) {
                break;
            }
        }
    }

    void for_each(const auto& visitor)
    {
        for (auto& elem : vector_) {
            std::visit(visitor, elem);
        }
    }

    void for_each(const auto& visitor) const
    {
        for (const auto& elem : vector_) {
            std::visit(visitor, elem);
        }
    }

protected:
    type vector_{member_entry<V>{K}...};
};

} // namespace viu::vector
