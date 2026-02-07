module;

#include "libusb.h"

#include <boost/asio.hpp>

#include <sys/stat.h>
#include <unistd.h>

module viu.daemon;

import std;

import viu.assert;
import viu.boost;
import viu.cli;
import viu.error;
import viu.device.mock;
import viu.device.proxy;
import viu.io;
import viu.plugin.loader;
import viu.tickable;
import viu.usb;
import viu.usb.descriptors;
import viu.version;

namespace viu::daemon {

enum class error : std::uint8_t { invalid_argument };

constexpr auto error_category_of(error /*unused*/) noexcept
{
    return viu::error_category::cli;
};

namespace args {

auto operator<<(std::ostream& os, const device_id& id) -> std::ostream&
{
    return os << id.vid_ << ":" << id.pid_;
}

auto operator>>(std::istream& in, device_id& id) -> std::istream&
{
    auto from_hex = std::stringstream{};
    from_hex << std::hex << std::string{std::istreambuf_iterator<char>(in), {}};
    auto column = char{};
    from_hex >> id.vid_ >> column >> id.pid_;
    return in;
}

} // namespace args

using boost::asio::local::stream_protocol;

auto service::runtime_dir() -> std::filesystem::path
{
    const auto dir = std::filesystem::path{"/tmp/viud"};
    std::filesystem::create_directories(dir);
    return dir;
}

auto service::socket_path() -> std::filesystem::path
{
    return runtime_dir() / "viud.sock";
}

auto service::is_running() -> bool
{
    return std::filesystem::exists(socket_path());
}

auto service::is_service_start() -> bool
{
    return !is_running() && std::filesystem::exists(runtime_dir());
}

auto service::get_subcommand(const std::span<const char*>& args) -> std::string
{
    if (std::size(args) > 1) {
        return std::string{args[1]};
    }
    return std::string{};
}

auto service::parse_command(
    const std::span<const char*>& args,
    const boost::program_options::options_description& desc
) -> boost::program_options::variables_map
{
    namespace po = boost::program_options;
    auto vm = po::variables_map{};
    try {
        po::store(
            po::parse_command_line(std::size(args), args.data(), desc),
            vm
        );
        po::notify(vm);
    } catch (boost::program_options::unknown_option) {
        std::println(std::cerr, "Unknown option");
    }
    return vm;
}

auto service::check_cli_params(
    const boost::program_options::variables_map& vm,
    const boost::program_options::options_description& desc,
    std::initializer_list<std::string_view> params
) -> viu::result<void>
{
    auto ss = std::stringstream{};
    auto missing = false;
    for (auto p : params) {
        if (vm.count(std::string(p)) == 0) {
            std::println(ss, "--{} is required", p);
            missing = true;
        }
    }

    if (missing) {
        std::println(ss, "Usage:");
        desc.print(ss);
        return viu::make_error(error::invalid_argument, ss.str());
    }

    return {};
}

auto service::app_proxy(const std::uint32_t vid, const std::uint32_t pid)
    -> viu::response
{
    const auto device = std::make_shared<viu::usb::device>(vid, pid);
    proxy_devices_.emplace_back(std::make_unique<viu::device::proxy>(device));

    return viu::response::success("Proxy device created successfully");
}

auto service::app_save_config(
    const std::uint32_t vid,
    const std::uint32_t pid,
    const std::filesystem::path& path
) -> viu::response
{
    const auto device = std::make_shared<viu::usb::device>(vid, pid);
    const auto proxy_usb_device = viu::device::proxy{device};
    const auto device_desc = proxy_usb_device.device_descriptor();
    const auto config_desc = proxy_usb_device.config_descriptor();
    const auto str_descs = proxy_usb_device.string_descriptors();
    const auto bos_desc = proxy_usb_device.bos_descriptor().value_or(
        viu::usb::descriptor::bos_descriptor_pointer{
            nullptr,
            [](libusb_bos_descriptor*) {}
        }
    );
    const auto report_desc = proxy_usb_device.report_descriptor().value_or(
        std::vector<std::uint8_t>{}
    );

    auto desc = viu::usb::descriptor::tree{
        device_desc,
        config_desc,
        str_descs,
        bos_desc,
        report_desc
    };

    desc.save(path);

    return viu::response::success(
        "Device configuration saved to " + path.string()
    );
}

auto service::app_save_hid_report(
    const std::uint32_t vid,
    const std::uint32_t pid,
    const std::filesystem::path& path
) -> viu::response
{
    const auto device = std::make_shared<viu::usb::device>(vid, pid);
    const auto proxy_usb_device = viu::device::proxy{device};
    const auto report_desc = proxy_usb_device.report_descriptor().value_or(
        std::vector<std::uint8_t>{}
    );

    if (!viu::io::bin::file::save(path, report_desc)) {
        return viu::response::failure(
            "Failed to save HID report to " + path.string(),
            viu::make_error(error::invalid_argument, "Invalid argument").error()
        );
    }

    return viu::response::success("HID report saved to " + path.string());
}

auto service::app_mock(
    const std::filesystem::path& device_config_path,
    const std::filesystem::path& catalog_path
) -> viu::response
{
    auto dev_desc = viu::usb::descriptor::tree{};
    dev_desc.load(device_config_path);

    const auto plugin_factory = virtual_device_manager_.register_catalog(
        catalog_path.string()
    );
    viu::_assert(plugin_factory != nullptr);

    auto ss = std::stringstream{};
    std::println(ss, "Catalog Information:");
    std::println(ss, "  Name: {}", plugin_factory->name());
    std::println(ss, "  Version: {}", plugin_factory->version());
    std::println(
        ss,
        "  Number of devices: {}",
        plugin_factory->number_of_devices()
    );
    std::println(
        ss,
        "Devices exported by '{}' catalog:",
        plugin_factory->name()
    );

    for (std::size_t n = 0; n < plugin_factory->number_of_devices(); n++) {
        std::println(ss, " Name: {}", plugin_factory->device_name(n));

        auto vd = virtual_device_manager_.device(
            catalog_path.string(),
            plugin_factory->device_name(n)
        );

        viu::_assert(vd && *vd != nullptr);

        mock_devices_.emplace_back(
            std::make_unique<viu::device::mock>(dev_desc, *vd)
        );

        auto tickable_ptr = std::static_pointer_cast<viu::tickable>(*vd);
        tick_service_.add(tickable_ptr);
    }

    std::println(ss, "Mock devices started successfully");
    return viu::response::success(ss.str());
}

auto service::app_version() -> viu::response
{
    auto ss = std::stringstream{};
    std::println(ss, "Service version: {}", version::app::full());
    std::println(ss, "Library version: {}", version::lib::full());
    return viu::response::success(ss.str());
}

auto service::run_proxydev_command(const std::span<const char*>& args)
    -> viu::response
{
    namespace po = boost::program_options;
    auto desc = po::options_description{"Proxy usb connection"};
    auto device = ::viu::daemon::args::device_id{};
    // clang-format off
    desc.add_options()
    ("help,h", "Show this message")
    (
        "device,d",
        po::value<::viu::daemon::args::device_id>(&device),
        "Device id as vid:pid"
    );
    // clang-format on
    const auto vm = parse_command(args, desc);
    if (vm.count("help")) {
        auto ss = std::stringstream{};
        desc.print(ss);
        return viu::response::success(ss.str());
    }

    if (auto res = check_cli_params(vm, desc, {"device"}); !res) {
        return viu::response::failure(
            std::string(res.error().message()),
            res.error()
        );
    }

    return app_proxy(device.vid(), device.pid());
}

auto service::run_save_command(const std::span<const char*>& args)
    -> viu::response
{
    namespace po = boost::program_options;
    auto desc = po::options_description{"Save device configuration to file"};
    auto device = ::viu::daemon::args::device_id{};
    auto path = std::filesystem::path{};
    // clang-format off
    desc.add_options()
    ("help,h", "Show this message")
    (
       "device,d",
       po::value<::viu::daemon::args::device_id>(&device),
       "Device id as vid:pid"
    )
    (
        "file,f",
        po::value<std::filesystem::path>(&path),
        "Configuration path"
    );
    // clang-format on

    const auto vm = parse_command(args, desc);
    if (vm.count("help")) {
        auto ss = std::stringstream{};
        desc.print(ss);
        return viu::response::success(ss.str());
    }

    if (auto res = check_cli_params(vm, desc, {"device", "file"}); !res) {
        return viu::response::failure(
            std::string(res.error().message()),
            res.error()
        );
    }

    return app_save_config(device.vid(), device.pid(), path);
}

auto service::run_save_hid_report(const std::span<const char*>& args)
    -> viu::response
{
    namespace po = boost::program_options;
    auto desc = po::options_description{"Save HID report to file"};
    auto device = ::viu::daemon::args::device_id{};
    auto path = std::filesystem::path{};
    // clang-format off
    desc.add_options()
    ("help,h", "Show this message")
    (
       "device,d",
       po::value<::viu::daemon::args::device_id>(&device),
       "Device id as vid:pid"
    )
    (
        "file,f",
        po::value<std::filesystem::path>(&path),
        "HID report path"
    );
    // clang-format on

    const auto vm = parse_command(args, desc);
    if (vm.count("help")) {
        auto ss = std::stringstream{};
        desc.print(ss);
        return viu::response::success(ss.str());
    }

    if (auto res = check_cli_params(vm, desc, {"device", "file"}); !res) {
        return viu::response::failure(
            std::string(res.error().message()),
            res.error()
        );
    }

    return app_save_hid_report(device.vid(), device.pid(), path);
}

auto service::run_mock_command(const std::span<const char*>& args)
    -> viu::response
{
    namespace po = boost::program_options;
    auto desc = po::options_description{"Mock devices from a catalog"};
    auto device_config_path = std::filesystem::path{};
    auto catalog_path = std::filesystem::path{};
    // clang-format off
    desc.add_options()
    ("help,h", "Show this message")
    (
       "config,c",
       po::value<std::filesystem::path>(&device_config_path),
       "Path to a device configuration"
    )
    (
        "catalog,m",
        po::value<std::filesystem::path>(&catalog_path),
        "Path to a device catalog"
    );
    // clang-format on

    const auto vm = parse_command(args, desc);
    if (vm.count("help")) {
        auto ss = std::stringstream{};
        desc.print(ss);
        return viu::response::success(ss.str());
    }

    if (auto res = check_cli_params(vm, desc, {"config", "catalog"}); !res) {
        return viu::response::failure(
            std::string(res.error().message()),
            res.error()
        );
    }

    return app_mock(device_config_path, catalog_path);
}

auto service::run_version_command(const std::span<const char*>& args)
    -> viu::response
{
    return app_version();
}

auto service::execute_from_argv(int argc, const char* argv[]) -> viu::response
{
    namespace po = boost::program_options;

    using parse_func_type =
        std::function<viu::response(const std::span<const char*>&)>;
    const auto subcommands = std::map<std::string, parse_func_type>{
        {"proxydev",
         [this](const std::span<const char*>& args) {
             return run_proxydev_command(args);
         }},
        {"save",
         [this](const std::span<const char*>& args) {
             return run_save_command(args);
         }},
        {"save-hid-report",
         [this](const std::span<const char*>& args) {
             return run_save_hid_report(args);
         }},
        {"mock",
         [this](const std::span<const char*>& args) {
             return run_mock_command(args);
         }},
        {"version", [this](const std::span<const char*>& args) {
             return run_version_command(args);
         }}
    };

    const auto arguments = std::span{argv, static_cast<std::size_t>(argc)};
    if (const auto func = subcommands.find(get_subcommand(arguments));
        func != std::end(subcommands)) {
        return func->second(
            std::span{&argv[1], static_cast<std::size_t>(argc - 1)}
        );
    }

    auto desc = po::options_description{"Virtual USB device CLI"};
    desc.add_options()("help", "Show this message");

    auto vm = parse_command(arguments, desc);
    auto ss = std::stringstream{};

    const auto print_usage = [&]() {
        desc.print(ss);
        std::println(ss, "List of subcommands:");
        for (auto& p : subcommands) {
            std::println(ss, "  {}", p.first);
        }
    };

    if (vm.count("help")) {
        print_usage();
        return viu::response::success(ss.str());
    }

    std::println(ss, "Invalid or no subcommand provided\nUsage:");
    print_usage();
    return viu::response::failure(
        ss.str(),
        viu::make_error(error::invalid_argument, "Invalid argument").error()
    );
}

void service::handle_accept(
    std::function<void()>& do_accept,
    std::shared_ptr<boost::asio::local::stream_protocol::socket> socket,
    const boost::system::error_code& ec,
    boost::asio::local::stream_protocol::acceptor& acceptor,
    const std::filesystem::path& path
)
{
    if (!ec) {
        auto size = std::uint32_t{};
        boost::asio::read(*socket, boost::asio::buffer(&size, sizeof(size)));

        auto payload = std::vector<char>(size);
        boost::asio::read(
            *socket,
            boost::asio::buffer(payload.data(), payload.size())
        );

        if (!ec) {
            try {
                auto args = viu::cli::deserialize_argv(
                    static_cast<const char*>(payload.data()),
                    payload.size()
                );

                auto response = execute_from_argv(
                    args.argc,
                    (const char**)(args.argv_storage.data())
                );

                std::string serialized = response.serialize();

                auto size = static_cast<std::uint32_t>(std::size(serialized));
                boost::asio::write(
                    *socket,
                    boost::asio::buffer(&size, sizeof(size))
                );

                boost::asio::write(*socket, boost::asio::buffer(serialized));

            } catch (const std::exception& ex) {
                std::println(
                    std::cerr,
                    "Failed to execute command: {}",
                    ex.what()
                );
            }
        } else {
            std::println(std::cerr, "Daemon error: {}", ec.message());
        }
    }

    if (acceptor.is_open()) {
        do_accept();
    }
}

auto service::run() -> int
{
    auto path = socket_path();

    auto io = boost::asio::io_context{};
    auto acceptor =
        stream_protocol::acceptor{io, stream_protocol::endpoint{path}};
    chmod(path.c_str(), 0777);

    boost::asio::signal_set signals(io, SIGTERM, SIGINT);
    signals.async_wait([&](const boost::system::error_code&, int sig) {
        std::println("Received signal {}. Shutting down", sig);
        unlink(path.c_str());
        acceptor.close();
    });

    auto io_ptr = &io;
    std::function<void()> do_accept;
    do_accept = [this, io_ptr, &acceptor, &do_accept, &path]() {
        auto socket = std::make_shared<stream_protocol::socket>(*io_ptr);
        acceptor.async_accept(
            *socket,
            [this, &do_accept, socket, &acceptor, &path](
                boost::system::error_code ec
            ) { handle_accept(do_accept, socket, ec, acceptor, path); }
        );
    };

    do_accept();
    io.run();

    return 0;
}

} // namespace viu::daemon
