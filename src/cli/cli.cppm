export module viu.cli;

import std;

namespace viu::cli {

export struct deserialized_args {
    int argc{};
    std::vector<char*> argv_storage;
    std::vector<std::string> string_storage;
};

export auto serialize_argv(int argc, const char* const argv[])
    -> std::vector<char>;

export auto deserialize_argv(const char* data, std::size_t size)
    -> deserialized_args;

} // namespace viu::cli
