export module viu.daemon;

import std;

import viu.boost;
import viu.cli;
import viu.device.mock;
import viu.device.proxy;
import viu.error;
import viu.plugin.catalog;
import viu.plugin.loader;
import viu.tickable;
import viu.usb.descriptors;

export namespace viu::daemon {

namespace args {

struct device_id {
    device_id() = default;
    device_id(const std::uint32_t vid, const std::uint32_t pid) noexcept
        : vid_{vid}, pid_{pid}
    {
    }

    [[nodiscard]] auto vid() const noexcept { return vid_; }
    [[nodiscard]] auto pid() const noexcept { return pid_; }

    friend auto operator<<(std::ostream& os, const device_id& id)
        -> std::ostream&;
    friend auto operator>>(std::istream& in, device_id& id) -> std::istream&;

private:
    std::uint32_t vid_{0};
    std::uint32_t pid_{0};
};

auto operator<<(std::ostream& os, const device_id& id) -> std::ostream&;
auto operator>>(std::istream& in, device_id& id) -> std::istream&;

} // namespace args

class service {
public:
    static auto runtime_dir() -> std::filesystem::path;
    static auto socket_path() -> std::filesystem::path;
    static auto is_running() -> bool;
    static auto is_service_start() -> bool;

    auto run() -> int;
    auto execute_from_argv(int argc, const char* argv[]) -> viu::response;

private:
    auto get_subcommand(const std::span<const char*>& args) -> std::string;
    auto parse_command(
        const std::span<const char*>& args,
        const boost::program_options::options_description& desc
    ) -> boost::program_options::variables_map;
    auto app_proxy(
        const std::uint32_t vid,
        const std::uint32_t pid,
        const std::filesystem::path& catalog_path
    ) -> viu::response;
    auto app_save_config(
        const std::uint32_t vid,
        const std::uint32_t pid,
        const std::filesystem::path& path
    ) -> viu::response;
    auto app_save_hid_report(
        const std::uint32_t vid,
        const std::uint32_t pid,
        const std::filesystem::path& path
    ) -> viu::response;
    auto app_mock(
        const std::filesystem::path& device_config_path,
        const std::filesystem::path& catalog_path
    ) -> viu::response;
    auto app_version() -> viu::response;

    void handle_accept(
        std::function<void()>& do_accept,
        std::shared_ptr<boost::asio::local::stream_protocol::socket> socket,
        const boost::system::error_code& ec,
        boost::asio::local::stream_protocol::acceptor& acceptor,
        const std::filesystem::path& path
    );

    auto run_proxydev_command(const std::span<const char*>& args)
        -> viu::response;
    auto run_save_command(const std::span<const char*>& args) -> viu::response;
    auto run_save_hid_report(const std::span<const char*>& args)
        -> viu::response;
    auto run_mock_command(const std::span<const char*>& args) -> viu::response;
    auto run_version_command(const std::span<const char*>& args)
        -> viu::response;

    auto check_cli_params(
        const boost::program_options::variables_map& vm,
        const boost::program_options::options_description& desc,
        std::initializer_list<std::string_view> params
    ) -> viu::result<void>;

    // TODO: Make them desctruction order independent
    viu::device::plugin::virtual_device_manager virtual_device_manager_{};
    std::vector<std::unique_ptr<viu::device::mock>> mock_devices_{};
    std::vector<std::unique_ptr<viu::device::proxy>> proxy_devices_{};
    viu::tick_service tick_service_{};
};

} // namespace viu::daemon
