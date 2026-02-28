export module viu.error;

import std;

import viu.boost;

namespace viu {

export enum class error_category : std::uint8_t { none, cli, usb, catalog };

template <typename T>
concept enum_type = std::is_enum_v<T>;

export struct error final {
    error() = default;

    error(
        error_category category,
        enum_type auto code,
        std::string message = {},
        std::source_location where = std::source_location::current()
    )
        : category_(category), code_(static_cast<int>(code)),
          message_(std::move(message)), file_(where.file_name()),
          line_(where.line()), function_(where.function_name()),
          stacktrace_(
              boost::stacktrace::to_string(boost::stacktrace::stacktrace())
          )
    {
    }

    [[nodiscard]] auto category() const noexcept { return category_; }
    [[nodiscard]] auto code() const noexcept { return code_; }

    [[nodiscard]] auto message() const noexcept -> std::string_view
    {
        return message_;
    }

    [[nodiscard]] auto file() const noexcept -> std::string_view
    {
        return file_;
    }

    [[nodiscard]] auto line() const noexcept { return line_; }

    [[nodiscard]] auto function() const noexcept -> std::string_view
    {
        return function_;
    }

    [[nodiscard]] auto stacktrace() const noexcept -> std::string_view
    {
        return stacktrace_;
    }

    [[nodiscard]] auto describe() const -> std::string
    {
        auto oss = std::ostringstream{};

        std::println(
            oss,
            "Error (category = {}, code = {}): {}\n  at {}:{} in "
            "{}\nStacktrace:\n{}",
            static_cast<std::uint32_t>(category_),
            code_,
            message_,
            file_,
            line_,
            function_,
            stacktrace_
        );

        return oss.str();
    }

    auto serialize() const -> std::string
    {
        auto oss = std::ostringstream{std::ios::binary};
        auto ar = boost::archive::binary_oarchive{oss};

        ar << *this;
        return oss.str();
    }

    static auto deserialize(const std::string& buffer) -> error
    {
        auto iss = std::istringstream{buffer, std::ios::binary};
        auto ar = boost::archive::binary_iarchive{iss};
        auto result = error{};

        ar >> result;
        return result;
    }

private:
    error_category category_{error_category::none};
    int code_{0};
    std::string code_name_;
    std::string message_;
    std::string file_;
    std::uint32_t line_{0};
    std::string function_;
    std::string stacktrace_;

    friend class boost::serialization::access;

    template <typename Archive>
    void serialize(Archive& ar, const unsigned int /*version*/)
    {
        ar & category_;
        ar & code_;
        ar & code_name_;
        ar & message_;
        ar & file_;
        ar & line_;
        ar & function_;
        ar & stacktrace_;
    }
};

export constexpr auto make_error(
    enum_type auto e,
    std::string message = {},
    const std::source_location where = std::source_location::current()
) -> std::unexpected<error>
{
    const auto category = [&]() -> auto {
        if constexpr (requires { error_category_of(e); }) {
            return error_category_of(e);
        } else {
            return error_category::none;
        }
    };

    return std::unexpected{error{category(), e, std::move(message), where}};
}

export constexpr auto make_error(const error& err) -> std::unexpected<error>
{
    return std::unexpected{err};
}

export struct response final {
    response() = default;

    explicit response(std::string message) : message_(std::move(message)) {}

    response(std::string message, error err)
        : message_(std::move(message)), error_(std::move(err))
    {
    }

    const std::string_view message() const noexcept { return message_; }
    const std::optional<error>& error_value() const noexcept { return error_; }
    auto has_error() const noexcept { return error_.has_value(); }
    auto is_success() const noexcept { return !has_error(); }

    static response success(std::string message)
    {
        return response{std::move(message)};
    }

    static response failure(std::string message, error err)
    {
        return response{std::move(message), std::move(err)};
    }

    auto describe() const -> std::string
    {
        auto oss = std::ostringstream{};

        if (!error_) {
            std::println(oss, "[success]\n{}", message_);
        } else {
            std::println(
                oss,
                "[failure]\n{}\n{}",
                message_,
                error_->describe()
            );
        }

        return oss.str();
    }

    auto serialize() const -> std::string
    {
        auto oss = std::ostringstream{std::ios::binary};
        auto ar = boost::archive::binary_oarchive{oss};
        ar << *this;
        return oss.str();
    }

    static auto deserialize(const std::string& buffer) -> response
    {
        auto iss = std::istringstream{buffer, std::ios::binary};
        auto ar = boost::archive::binary_iarchive{iss};
        auto result = response{};

        ar >> result;
        return result;
    }

private:
    std::string message_;
    std::optional<error> error_;

    friend class boost::serialization::access;

    template <typename Archive>
    void serialize(Archive& ar, const unsigned int /*version*/)
    {
        ar & message_;
        ar & error_;
    }
};

export template <typename T>
using result = std::expected<T, error>;

} // namespace viu
