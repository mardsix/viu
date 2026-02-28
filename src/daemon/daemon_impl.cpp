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

void service::create_mock_device_from_catalog(
    const std::filesystem::path& catalog_path,
    const std::string& device_name,
    const viu::usb::descriptor::tree& dev_desc
)
{
    auto vd =
        virtual_device_manager_.device(catalog_path.string(), device_name);

    viu::_assert(vd && *vd != nullptr);

    const auto vid = dev_desc.device_descriptor().idVendor;
    const auto pid = dev_desc.device_descriptor().idProduct;
    const auto id = device_id_counter_.fetch_add(1, std::memory_order_relaxed);
    virtual_devices_.emplace(
        id,
        device_info{
            vid,
            pid,
            std::make_unique<viu::device::mock>(dev_desc, *vd)
        }
    );
}

void service::create_proxy_device_from_catalog(
    std::uint32_t vid,
    std::uint32_t pid,
    const std::filesystem::path& catalog_path,
    const std::string& device_name
)
{
    auto vd =
        virtual_device_manager_.device(catalog_path.string(), device_name);
    viu::_assert(vd && *vd != nullptr);

    const auto device = std::make_shared<viu::usb::device>(vid, pid, *vd);
    const auto id = device_id_counter_.fetch_add(1, std::memory_order_relaxed);
    virtual_devices_.emplace(
        id,
        device_info{vid, pid, std::make_unique<viu::device::proxy>(device)}
    );
}

auto service::app_proxy(
    std::uint32_t vid,
    std::uint32_t pid,
    const std::filesystem::path& catalog_path
) -> viu::response
{
    if (catalog_path.empty()) {
        const auto device = std::make_shared<viu::usb::device>(vid, pid);
        const auto id =
            device_id_counter_.fetch_add(1, std::memory_order_relaxed);
        virtual_devices_.emplace(
            id,
            device_info{vid, pid, std::make_unique<viu::device::proxy>(device)}
        );

        return viu::response::success("Proxy device created successfully");
    }

    const auto register_result = virtual_device_manager_.register_catalog(
        catalog_path.string()
    );

    if (!register_result) {
        return viu::response::failure(
            std::string(register_result.error().message()),
            register_result.error()
        );
    }

    const auto plugin_factory = *register_result;
    viu::_assert(plugin_factory != nullptr);

    auto ss = std::stringstream{};
    viu::device::plugin::print_catalog_info(ss, plugin_factory);

    // TODO: support multiple devices
    viu::_assert(plugin_factory->number_of_devices() == 1);

    create_proxy_device_from_catalog(
        vid,
        pid,
        catalog_path,
        plugin_factory->device_name(0)
    );

    std::println(
        ss,
        "Proxy device created successfully using '{}' interface",
        plugin_factory->name()
    );

    return viu::response::success(ss.str());
}

auto service::app_save_config(
    std::uint32_t vid,
    std::uint32_t pid,
    const std::filesystem::path& path
) -> viu::response
{
    const auto device = std::make_shared<viu::usb::device>(vid, pid);
    const auto proxy_usb_device = viu::device::proxy{device};
    return proxy_usb_device.save_config(path);
}

auto service::app_save_hid_report(
    std::uint32_t vid,
    std::uint32_t pid,
    const std::filesystem::path& path
) -> viu::response
{
    const auto device = std::make_shared<viu::usb::device>(vid, pid);
    const auto proxy_usb_device = viu::device::proxy{device};
    return proxy_usb_device.save_hid_report(path);
}

auto service::app_mock(
    const std::filesystem::path& device_config_path,
    const std::filesystem::path& catalog_path
) -> viu::response
{
    auto dev_desc = viu::usb::descriptor::tree{};
    dev_desc.load(device_config_path);

    const auto register_result = virtual_device_manager_.register_catalog(
        catalog_path.string()
    );

    if (!register_result) {
        return viu::response::failure(
            std::string(register_result.error().message()),
            register_result.error()
        );
    }

    const auto plugin_factory = *register_result;
    viu::_assert(plugin_factory != nullptr);

    auto ss = std::stringstream{};
    viu::device::plugin::print_catalog_info(ss, plugin_factory);

    for (std::size_t n = 0; n < plugin_factory->number_of_devices(); n++) {
        create_mock_device_from_catalog(
            catalog_path,
            plugin_factory->device_name(n),
            dev_desc
        );
    }

    std::println(ss, "Mock devices started successfully");
    return viu::response::success(ss.str());
}

auto service::app_list_catalogs() -> viu::response
{
    auto ss = std::stringstream{};
    virtual_device_manager_.list_catalogs(ss);
    return viu::response::success(ss.str());
}

auto service::app_plug(
    const std::filesystem::path& config_path,
    const std::filesystem::path& catalog_path,
    const std::string& device_name
) -> viu::response
{
    auto dev_desc = viu::usb::descriptor::tree{};
    dev_desc.load(config_path);

    create_mock_device_from_catalog(catalog_path, device_name, dev_desc);

    auto ss = std::stringstream{};
    std::println(ss, "Device '{}' plugged successfully", device_name);

    return viu::response::success(ss.str());
}

auto service::app_version() -> viu::response
{
    auto ss = std::stringstream{};
    std::println(ss, "{}", version::app::full());
    return viu::response::success(ss.str());
}

auto service::app_list() -> viu::response
{
    auto ss = std::stringstream{};
    std::println(ss, "Connected Devices:");

    if (virtual_devices_.empty()) {
        std::println(ss, "  No devices connected");
    } else {
        for (const auto& [id, info] : virtual_devices_) {
            std::println(ss, "  id: {}, {:04x}:{:04x}", id, info.vid, info.pid);
        }
    }

    return viu::response::success(ss.str());
}

auto service::app_unplug(std::uint64_t device_id) -> viu::response
{
    auto it = virtual_devices_.find(device_id);
    if (it == virtual_devices_.end()) {
        auto ss = std::stringstream{};
        std::println(ss, "Device with id {} not found", device_id);
        return viu::response::failure(
            ss.str(),
            viu::make_error(error::invalid_argument, "Device not found").error()
        );
    }

    virtual_devices_.erase(it);
    return viu::response::success("Device unplugged successfully");
}

auto service::run_proxydev_command(const std::span<const char*>& args)
    -> viu::response
{
    namespace po = boost::program_options;
    auto desc = po::options_description{"Proxy usb connection"};
    auto device = ::viu::daemon::args::device_id{};
    auto catalog_path = std::filesystem::path{};
    // clang-format off
    desc.add_options()
    ("help,h", "Show this message")
    (
        "device,d",
        po::value<::viu::daemon::args::device_id>(&device),
        "Device id as vid:pid"
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

    if (auto res = check_cli_params(vm, desc, {"device"}); !res) {
        return viu::response::failure(
            std::string(res.error().message()),
            res.error()
        );
    }

    return app_proxy(device.vid(), device.pid(), catalog_path);
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

auto service::run_list_catalogs_command(const std::span<const char*>& args)
    -> viu::response
{
    namespace po = boost::program_options;
    auto desc = po::options_description{"List registered catalogs"};
    // clang-format off
    desc.add_options()
    ("help,h", "Show this message");
    // clang-format on

    const auto vm = parse_command(args, desc);
    if (vm.count("help")) {
        auto ss = std::stringstream{};
        desc.print(ss);
        return viu::response::success(ss.str());
    }

    return app_list_catalogs();
}

auto service::run_plug_command(const std::span<const char*>& args)
    -> viu::response
{
    namespace po = boost::program_options;
    auto desc = po::options_description{"Plug a device from a catalog"};
    auto config_path = std::filesystem::path{};
    auto catalog_path = std::filesystem::path{};
    auto device_name = std::string{};
    // clang-format off
    desc.add_options()
    ("help,h", "Show this message")
    (
        "config,c",
        po::value<std::filesystem::path>(&config_path),
        "Path to a device configuration"
    )
    (
        "catalog,m",
        po::value<std::filesystem::path>(&catalog_path),
        "Path to a device catalog"
    )
    (
        "device-name,n",
        po::value<std::string>(&device_name),
        "Name of the device to plug"
    );
    // clang-format on

    const auto vm = parse_command(args, desc);
    if (vm.count("help")) {
        auto ss = std::stringstream{};
        desc.print(ss);
        return viu::response::success(ss.str());
    }

    if (auto res =
            check_cli_params(vm, desc, {"config", "catalog", "device-name"});
        !res) {
        return viu::response::failure(
            std::string(res.error().message()),
            res.error()
        );
    }

    return app_plug(config_path, catalog_path, device_name);
}

auto service::run_version_command(const std::span<const char*>& args)
    -> viu::response
{
    return app_version();
}

auto service::run_list_command(const std::span<const char*>& args)
    -> viu::response
{
    namespace po = boost::program_options;
    auto desc = po::options_description{"List connected devices"};
    // clang-format off
    desc.add_options()
    ("help,h", "Show this message");
    // clang-format on

    const auto vm = parse_command(args, desc);
    if (vm.count("help")) {
        auto ss = std::stringstream{};
        desc.print(ss);
        return viu::response::success(ss.str());
    }

    return app_list();
}

auto service::run_unplug_command(const std::span<const char*>& args)
    -> viu::response
{
    namespace po = boost::program_options;
    auto desc = po::options_description{"Unplug a virtual device"};
    auto device_id = std::uint64_t{0};
    // clang-format off
    desc.add_options()
    ("help,h", "Show this message")
    (
        "device-id,i",
        po::value<std::uint64_t>(&device_id),
        "Device id to unplug"
    );
    // clang-format on

    const auto vm = parse_command(args, desc);
    if (vm.count("help")) {
        auto ss = std::stringstream{};
        desc.print(ss);
        return viu::response::success(ss.str());
    }

    if (auto res = check_cli_params(vm, desc, {"device-id"}); !res) {
        return viu::response::failure(
            std::string(res.error().message()),
            res.error()
        );
    }

    return app_unplug(device_id);
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
        {"list-catalogs",
         [this](const std::span<const char*>& args) {
             return run_list_catalogs_command(args);
         }},
        {"plug",
         [this](const std::span<const char*>& args) {
             return run_plug_command(args);
         }},
        {"version",
         [this](const std::span<const char*>& args) {
             return run_version_command(args);
         }},
        {"list",
         [this](const std::span<const char*>& args) {
             return run_list_command(args);
         }},
        {"unplug", [this](const std::span<const char*>& args) {
             return run_unplug_command(args);
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
