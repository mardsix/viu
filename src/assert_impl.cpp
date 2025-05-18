module viu.assert;

import std;

import viu.boost;

namespace viu {

namespace {

void log_location(const std::source_location& loc)
{
    std::clog << "file: " << loc.file_name() << '(' << loc.line() << ':'
              << loc.column() << ") `" << loc.function_name() << "`: "
              << "assertion failed\n";
}

void log_stacktrace()
{
    const auto strace = boost::stacktrace::stacktrace();
    std::clog << "stacktrace:\n" << strace << '\n';
}

} // namespace

void _assert(const bool exp, const std::source_location location)
{
    if (!exp) [[unlikely]] {
        log_location(location);
        log_stacktrace();
        std::abort();
    }
}

} // namespace viu
