module;

module viu.cli;

import std;

namespace viu {

auto cli::serialize_argv(int argc, const char* const argv[])
    -> std::vector<char>
{
    auto buffer = std::vector<char>{};
    auto append_u32 = [&](std::uint32_t v) {
        auto offset = std::size(buffer);
        buffer.resize(offset + sizeof(std::uint32_t));
        std::memcpy(buffer.data() + offset, &v, sizeof(std::uint32_t));
    };

    append_u32(static_cast<std::uint32_t>(argc));

    for (auto i = 0; i < argc; ++i) {
        auto len = static_cast<std::uint32_t>(std::strlen(argv[i]));
        append_u32(len);

        auto offset = std::size(buffer);
        buffer.resize(offset + len);
        std::memcpy(buffer.data() + offset, argv[i], len);
    }

    return buffer;
}

auto cli::deserialize_argv(const char* data, std::size_t size)
    -> cli::deserialized_args
{
    auto offset = std::size_t{};
    auto read_u32 = [&](std::size_t size) -> std::uint32_t {
        if (offset + sizeof(std::uint32_t) > size) {
            throw std::runtime_error("Malformed argv payload");
        }

        auto v = std::uint32_t{};
        std::memcpy(&v, data + offset, sizeof(std::uint32_t));
        offset += sizeof(std::uint32_t);
        return v;
    };

    auto result = cli::deserialized_args{};
    result.argc = static_cast<int>(read_u32(size));
    result.argv_storage.reserve(result.argc + 1);
    result.string_storage.reserve(result.argc);

    for (auto i = 0; i < result.argc; ++i) {
        auto len = std::uint32_t{read_u32(size)};

        if (offset + len > size) {
            throw std::runtime_error("Malformed argv payload");
        }

        result.string_storage.emplace_back(data + offset, len);
        result.argv_storage.push_back(result.string_storage.back().data());

        offset += len;
    }

    result.argv_storage.push_back(nullptr);
    return result;
}

} // namespace viu
