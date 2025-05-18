export module viu.cli;

import std;

export namespace viu {

class cli {
public:
    struct deserialized_args {
        int argc;
        std::vector<char*> argv_storage;
        std::vector<std::string> string_storage;
    };

    static auto serialize_argv(int argc, const char* const argv[])
        -> std::vector<char>;

    static auto deserialize_argv(const char* data, std::size_t size)
        -> deserialized_args;
};

} // namespace viu
