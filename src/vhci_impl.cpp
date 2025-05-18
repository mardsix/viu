module;

// TODO: remove
// 21.1.3 (++20250923093437+74cb34a6f51a-1~exp1~20250923213555.35)
// fails to find operator== for std::filesystem::path with import std;
#include <filesystem>

#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/spirit/home/x3.hpp>

#include <libusb.h>
#include <systemd/sd-device.h>

namespace ast {

struct vhci_hcd_status {
    std::string hub;
    int port;
    int sta;
    int spd;
    int dev;
    int sockfd;
    std::string local_buisid;
};

using boost::fusion::operator<<;
using vhci_hcd_status_vector = std::vector<vhci_hcd_status>;

} // namespace ast

BOOST_FUSION_ADAPT_STRUCT(
    ast::vhci_hcd_status,
    hub,
    port,
    sta,
    spd,
    dev,
    sockfd,
    local_buisid
);

module viu.vhci;

import std;

import viu.assert;
import viu.boost;
import viu.format;

namespace client::parser {

namespace x3 = boost::spirit::x3;
namespace ascii = x3::ascii;

using iterator_type = std::string::const_iterator;
using context_type = x3::phrase_parse_context<ascii::space_type>::type;
using vhci_hcd_vector_type = x3::rule<class s, ast::vhci_hcd_status_vector>;

BOOST_SPIRIT_DECLARE(vhci_hcd_vector_type);
BOOST_SPIRIT_INSTANTIATE(vhci_hcd_vector_type, iterator_type, context_type);

const vhci_hcd_vector_type vhci_hcd_status_parser = "vhci_hcd_status_parser";

using ascii::char_;
using x3::int_;
using x3::lexeme;
using x3::lit;

// clang-format off
auto const string_ = +(char_ - (char_(' ') | '\n'));
auto const separator_ = +lit(' ');

auto const vhci_hcd_status_parser_def = +lexeme[
    string_ >> separator_
    >> int_ >> separator_
    >> int_ >> separator_
    >> int_ >> separator_
    >> int_ >> separator_
    >> int_ >> separator_
    >> string_ >> *lit("\n")
];
// clang-format on

BOOST_SPIRIT_DEFINE(vhci_hcd_status_parser);

} // namespace client::parser

namespace client {

auto vhci_hcd_status_parser() -> parser::vhci_hcd_vector_type
{
    return parser::vhci_hcd_status_parser;
}

auto parse_status(
    const char* const status_string,
    ast::vhci_hcd_status_vector& result
)
{
    viu::_assert(status_string != nullptr);
    auto str = std::string{status_string};

    if (const auto pos = str.find('\n'); pos != std::string::npos) {
        str.erase(0, pos + 1);
    }

    auto iter = std::cbegin(str);
    const auto parsing_succeeded = boost::spirit::x3::phrase_parse(
        iter,
        std::cend(str),
        vhci_hcd_status_parser(),
        boost::spirit::x3::ascii::space,
        result
    );

    return parsing_succeeded && iter == std::cend(str);
}

} // namespace client

using viu::usbip::command;

auto command::ep_address() const noexcept -> std::uint8_t
{
    return static_cast<std::uint8_t>(
        ep() | (is_in() ? LIBUSB_ENDPOINT_IN : LIBUSB_ENDPOINT_OUT)
    );
}

auto command::transfer_buffer_size() const -> std::size_t
{
    viu::_assert(is_submit());
    viu::_assert(header().cmd_submit.transfer_buffer_length >= 0);
    const auto size = header().cmd_submit.transfer_buffer_length;
    return static_cast<std::size_t>(size);
}

auto command::iso_packet_count() const -> std::int32_t
{
    viu::_assert(is_submit());
    return header().cmd_submit.number_of_packets;
}

auto command::start_frame() const
{
    viu::_assert(is_submit());
    return header().cmd_submit.start_frame;
}

auto command::unlink_seqnum() const -> std::uint32_t
{
    viu::_assert(is_unlink());
    return header().cmd_unlink.seqnum;
}

auto command::reply_header() const noexcept -> viu::usbip::usbip_header_basic
{
    using namespace viu::format;

    const auto comamnd = is_submit() ? USBIP_RET_SUBMIT : USBIP_RET_UNLINK;
    return viu::usbip::usbip_header_basic{
        .command = endian::to_big(comamnd),
        .seqnum = endian::to_big(seqnum()),
        .devid = endian::to_big(devid()),
        .direction = endian::to_big(direction()),
        .ep = endian::to_big(ep()),
    };
}

auto command::control_setup() const -> libusb_control_setup
{
    using boost::endian::load_little_u16;
    using namespace viu::format;

    auto control_setup = libusb_control_setup{};
    control_setup.bmRequestType = header().cmd_submit.setup[0];
    control_setup.bRequest = header().cmd_submit.setup[1];

    auto hdr = header();
    const auto value = load_little_u16(&hdr.cmd_submit.setup[2]);
    control_setup.wValue = endian::from_little(value);

    const auto index = load_little_u16(&hdr.cmd_submit.setup[4]);
    control_setup.wIndex = endian::from_little(index);

    const auto length = load_little_u16(&hdr.cmd_submit.setup[6]);
    control_setup.wLength = endian::from_little(length);

    return control_setup;
}

auto command::config_index() const -> std::uint8_t
{
    return format::integral<std::uint8_t>::at<0>(control_setup().wValue);
}

auto command::recipient() const -> std::uint8_t
{
    const auto request_type = control_setup().bmRequestType;
    return request_type & 0b00011111;
}

auto command::request_type() const -> std::uint8_t
{
    const auto request_type = control_setup().bmRequestType;
    return (request_type & 0b01100000);
}

auto command::from_big_endian(boost::asio::streambuf& buffer) -> command
{
    auto cmd = viu::usbip::command{};
    buffer.sgetn((char*)&cmd.header(), sizeof(cmd.header()));

    using namespace viu::format;

    cmd.header().base = {
        .command = endian::from_big(cmd.request()),
        .seqnum = endian::from_big(cmd.seqnum()),
        .devid = endian::from_big(cmd.devid()),
        .direction = endian::from_big(cmd.direction()),
        .ep = endian::from_big(cmd.ep()),
    };

    switch (cmd.request()) {
        case USBIP_CMD_SUBMIT: {
            auto& submit_hdr = cmd.header().cmd_submit;

            submit_hdr.transfer_flags = endian::from_big(
                submit_hdr.transfer_flags
            );
            submit_hdr.transfer_buffer_length = endian::from_big(
                submit_hdr.transfer_buffer_length
            );
            submit_hdr.start_frame = endian::from_big(submit_hdr.start_frame);
            submit_hdr.number_of_packets = endian::from_big(
                submit_hdr.number_of_packets
            );
            submit_hdr.interval = endian::from_big(submit_hdr.interval);
        } break;

        case USBIP_CMD_UNLINK:
            cmd.header().cmd_unlink = {
                endian::from_big(cmd.unlink_seqnum()),
            };
            break;

        default:
            throw std::runtime_error(
                viu::format::make_string(
                    "Invalid usbip command:",
                    cmd.request()
                )
            );
    }

    return cmd;
}

auto command::make_ret_submit_header(
    const std::size_t len,
    const std::int32_t status,
    const std::int32_t error_count
) const noexcept -> viu::usbip::usbip_header_ret_submit
{
    using namespace viu::format;

    viu::_assert(is_submit());

    return viu::usbip::usbip_header_ret_submit{
        .status = endian::to_big(status),
        .actual_length = endian::to_big(static_cast<std::int32_t>(len)),
        .start_frame = endian::to_big(start_frame()),
        .number_of_packets = endian::to_big(iso_packet_count()),
        .error_count = endian::to_big(error_count),
    };
}

auto command::make_ret_unlink_header(const std::int32_t status) const noexcept
    -> viu::usbip::usbip_header_ret_unlink
{
    viu::_assert(is_unlink());
    return viu::usbip::usbip_header_ret_unlink{
        viu::format::endian::to_big(status)
    };
}

using viu::vhci::driver;

void driver::read(boost::asio::streambuf& buffer, const std::size_t size)
{
    usbip_socket_.read(buffer, size);
}

void driver::write(boost::asio::streambuf& buffer, const std::size_t size)
{
    usbip_socket_.write(buffer, size);
}

void driver::request_stop() { usbip_socket_.close(); }

void driver::write_sysfs_attribute(
    const std::string& attr_path,
    const std::string& attribute_value
) const
{
    std::ofstream sysfs_attribute_stream{attr_path};
    sysfs_attribute_stream << attribute_value;
}

auto driver::get_number_of_ports(sd_device* host_controller_device) const
{
    const char* number_of_ports{};
    sd_device_get_sysattr_value(
        host_controller_device,
        "nports",
        &number_of_ports
    );

    if (number_of_ports == nullptr) {
        return 0;
    }

    try {
        return std::stoi(std::string{number_of_ports});
    } catch (std::invalid_argument& invalid_argument_ex) {
        std::println(std::cerr, "{}", invalid_argument_ex.what());
        return 0;
    } catch (std::out_of_range& out_of_range_ex) {
        std::println(std::cerr, "{}", out_of_range_ex.what());
        return 0;
    }
}

auto driver::count_controllers() const
{
    sd_device* platform{};
    sd_device_get_parent(underlying_device(), &platform);

    if (platform == nullptr) {
        return std::filesystem::directory_iterator::difference_type{};
    }

    const char* system_path{};
    sd_device_get_syspath(platform, &system_path);
    viu::_assert(system_path != nullptr);

    const auto& path = std::filesystem::path{system_path};

    return std::ranges::count_if(
        std::filesystem::directory_iterator{path},
        [](const auto& entry) {
            return entry.path().stem().string() == "vhci_hcd";
        }
    );
}

auto driver::parse_status(const char* value)
{
    viu::_assert(value != nullptr);

    ast::vhci_hcd_status_vector vhci_hcd_status{};

    if (client::parse_status(value, vhci_hcd_status)) {
        for (const auto& status : vhci_hcd_status) {
            auto& virtual_device = devices_.at(status.port);

            if (status.hub == "hs") {
                virtual_device.hub = hub_speed::HUB_SPEED_HIGH;
            } else {
                virtual_device.hub = hub_speed::HUB_SPEED_SUPER;
            }

            virtual_device.port = status.port;
            virtual_device.status = status.sta;
            virtual_device.devid = status.dev;
            virtual_device.busnum = (status.dev >> 16);
            virtual_device.devnum = (status.dev & 0x0000ffff);

            if (virtual_device.status != VDEV_ST_NULL &&
                virtual_device.status != VDEV_ST_NOTASSIGNED) {
                // viu::_assert(false);
            }
        }
    }

    return 0;
}

auto driver::refresh_status()
{
    for (int i = 0; i < number_of_controllers_; ++i) {
        const std::string status = i > 0 ? std::format("status.{}", i)
                                         : "status";
        const char* attr_status{};
        sd_device_get_sysattr_value(
            underlying_device(),
            status.c_str(),
            &attr_status
        );

        if (attr_status == nullptr) {
            return -1;
        }

        if (const auto r = parse_status(attr_status); r != 0) {
            return r;
        }
    }

    return 0;
}

auto driver::make_device() const
{
    const auto create = []() {
        sd_device* dev{};
        sd_device_new_from_subsystem_sysname(&dev, "platform", "vhci_hcd.0");
        return dev;
    };
    const auto deleter = [](sd_device* device) { sd_device_unref(device); };
    return driver::device_pointer{create(), deleter};
}

auto driver::open()
{
    host_controller_device_ = make_device();
    if (host_controller_device_ == nullptr) {
        return -1;
    }

    devices_.resize(get_number_of_ports(underlying_device()));
    viu::_assert(devices_.size() != 0);

    number_of_controllers_ = count_controllers();
    if (number_of_controllers_ <= 0) {
        return -1;
    }

    return refresh_status();
}

auto driver::get_free_port(const usb_device_speed speed) const
    -> std::expected<std::uint8_t, viu::vhci::error>
{
    const auto hub_speed{
        (speed == usb_device_speed::USB_SPEED_SUPER ||
         speed == usb_device_speed::USB_SPEED_SUPER_PLUS)
            ? hub_speed::HUB_SPEED_SUPER
            : hub_speed::HUB_SPEED_HIGH
    };

    const auto free_device =
        std::ranges::find_if(devices_, [&](const auto& device) {
            return (device.hub == hub_speed) && (device.status == VDEV_ST_NULL);
        });

    if (free_device != std::cend(devices_)) {
        return (*free_device).port;
    }

    return std::unexpected{viu::vhci::error::no_free_port};
}

auto driver::attach_device(
    const std::uint8_t port,
    const int sockfd,
    const std::uint32_t devid,
    const std::uint32_t speed
) const
{
    const char* path{};
    sd_device_get_syspath(underlying_device(), &path);
    if (path == nullptr) {
        return -1;
    }

    const auto attach_attr_path = std::format("{}/attach", path);
    const auto attribute_value = viu::format::make_string(
        port,
        sockfd,
        devid,
        std::min(
            static_cast<std::uint32_t>(usb_device_speed::USB_SPEED_SUPER),
            speed
        )
    );

    write_sysfs_attribute(attach_attr_path, attribute_value);

    return 0;
}

driver::driver()
{
    auto usbip_result = open();
    viu::_assert(usbip_result == 0);
}

void driver::attach(const std::uint32_t speed, const std::uint8_t device_id)
{
    while (true) {
        const auto speed_enum = driver::to_speed_enum(speed);
        const auto usbip_port = get_free_port(speed_enum);
        viu::_assert(usbip_port.has_value());

        auto usbip_result = attach_device(
            usbip_port.value(),
            usbip_socket_.fd(),
            device_id,
            static_cast<std::uint32_t>(speed_enum)
        );

        if (usbip_result < 0) {
            viu::_assert(errno == EBUSY);
            continue;
        }

        break;
    }
}
