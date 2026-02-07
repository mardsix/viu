module viu.assert;

import std;

import viu.boost;

template <>
struct std::formatter<boost::stacktrace::stacktrace>
    : std::formatter<std::string> {
    auto format(
        const boost::stacktrace::stacktrace& st,
        std::format_context& ctx
    ) const
    {
        auto oss = std::ostringstream{};
        oss << st;
        return std::formatter<std::string>::format(oss.str(), ctx);
    }
};

namespace viu {

namespace {

void log_location(const std::source_location& loc)
{
    std::println(
        "Assertion failed at {}:{} in function {}",
        loc.file_name(),
        loc.line(),
        loc.function_name()
    );
}

void log_stacktrace()
{
    std::println("Stacktrace:\n{}", boost::stacktrace::stacktrace{});
}

} // namespace

void _assert(const bool exp, const std::source_location location)
{
    if (!exp) [[unlikely]] {
        log_location(location);
        log_stacktrace();
#if defined(VIU_ASSERT_ABORT)
        std::abort();
#endif
    }
}

} // namespace viu
