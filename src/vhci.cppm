module;

#include <libusb.h>
#include <systemd/sd-device.h>

export module viu.vhci;

import std;

import viu.boost;
import viu.transfer;
import viu.usb.descriptors;
import viu.usbip.socket;

// https://github.com/torvalds/linux/blob/master/drivers/usb/usbip/usbip_common.h
export const auto USBIP_CMD_SUBMIT = std::uint32_t{0x0001};
export const auto USBIP_CMD_UNLINK = std::uint32_t{0x0002};
export const auto USBIP_RET_SUBMIT = std::uint32_t{0x0003};
export const auto USBIP_RET_UNLINK = std::uint32_t{0x0004};

const auto USBIP_DIR_OUT = std::uint32_t{0x00};
const auto USBIP_DIR_IN = std::uint32_t{0x01};

namespace viu::usbip {

struct usbip_header_basic {
    std::uint32_t command;
    std::uint32_t seqnum;
    std::uint32_t devid;
    std::uint32_t direction;
    std::uint32_t ep;
} __attribute__((packed));

struct usbip_header_cmd_submit {
    std::uint32_t transfer_flags;
    std::int32_t transfer_buffer_length;
    std::int32_t start_frame;
    std::int32_t number_of_packets;
    std::int32_t interval;
    std::uint8_t setup[8];
} __attribute__((packed));

struct usbip_header_ret_submit {
    std::int32_t status;
    std::int32_t actual_length;
    std::int32_t start_frame;
    std::int32_t number_of_packets;
    std::int32_t error_count;
} __attribute__((packed));

struct usbip_header_cmd_unlink {
    std::uint32_t seqnum;
} __attribute__((packed));

struct usbip_header_ret_unlink {
    std::int32_t status;
} __attribute__((packed));

struct usbip_header {
    usbip_header_basic base;
    union {
        usbip_header_cmd_submit cmd_submit;
        usbip_header_ret_submit ret_submit;
        usbip_header_cmd_unlink cmd_unlink;
        usbip_header_ret_unlink ret_unlink;
    };
} __attribute__((packed));

export struct command {
    using payload_type = std::vector<std::uint8_t>;
    using payload_shared_ptr = std::shared_ptr<payload_type>;

    [[nodiscard]] auto header() const noexcept { return header_; }
    [[nodiscard]] auto& payload() noexcept { return payload_; }
    [[nodiscard]] auto& header() noexcept { return header_; }

    [[nodiscard]] auto request() const noexcept
    {
        return header().base.command;
    }

    [[nodiscard]] auto direction() const noexcept
    {
        return header().base.direction;
    }

    [[nodiscard]] auto is_in() const noexcept
    {
        return direction() == USBIP_DIR_IN;
    }

    [[nodiscard]] auto is_out() const noexcept
    {
        return direction() == USBIP_DIR_OUT;
    }

    [[nodiscard]] auto is_iso() const
    {
        const auto num_of_packets = iso_packet_count();
        return (num_of_packets != 0) && (num_of_packets != 0xffffffff);
    }

    [[nodiscard]] auto is_unlink() const noexcept
    {
        return request() == USBIP_CMD_UNLINK;
    }

    [[nodiscard]] auto is_submit() const noexcept
    {
        return request() == USBIP_CMD_SUBMIT;
    }

    [[nodiscard]] auto payload() const { return payload_; }

    [[nodiscard]] auto iso_descriptor_size() const -> std::size_t
    {
        return iso_packet_count() * usb::descriptor::iso_descriptor_size();
    }

    [[nodiscard]] auto payload_size() -> int
    {
        if (is_unlink()) {
            return 0;
        }

        auto payload_size = 0;

        if (is_out()) {
            payload_size = transfer_buffer_size();
        }

        if (is_iso()) {
            payload_size += iso_descriptor_size();
        }

        return payload_size;
    }

    [[nodiscard]] auto ep() const noexcept { return header().base.ep; }
    [[nodiscard]] auto seqnum() const noexcept { return header().base.seqnum; }
    [[nodiscard]] auto devid() const noexcept { return header().base.devid; }

    [[nodiscard]] static constexpr auto header_size() noexcept
    {
        return sizeof(usbip_header);
    }

    [[nodiscard]] auto ep_address() const noexcept -> std::uint8_t;
    [[nodiscard]] auto transfer_buffer_size() const -> std::size_t;
    [[nodiscard]] auto iso_packet_count() const -> std::int32_t;
    [[nodiscard]] auto unlink_seqnum() const -> std::uint32_t;
    [[nodiscard]] auto reply_header() const noexcept
        -> usbip::usbip_header_basic;
    [[nodiscard]] auto control_setup() const -> libusb_control_setup;
    [[nodiscard]] auto config_index() const -> std::uint8_t;
    [[nodiscard]] auto recipient() const -> std::uint8_t;
    [[nodiscard]] auto request_type() const -> std::uint8_t;
    [[nodiscard]] static auto from_big_endian(boost::asio::streambuf& buffer)
        -> command;

    [[nodiscard]] auto make_ret_submit_header(
        std::size_t len,
        std::int32_t status,
        std::int32_t error_count
    ) const noexcept -> usbip::usbip_header_ret_submit;

    [[nodiscard]] auto make_ret_unlink_header(
        std::int32_t status
    ) const noexcept -> usbip::usbip_header_ret_unlink;

private:
    [[nodiscard]] auto start_frame() const;

    usbip_header header_{};
    payload_type payload_{};
};

} // namespace viu::usbip

// https://github.com/torvalds/linux/blob/master/include/uapi/linux/usbip.h
enum usbip_device_status {
    /* sdev is available. */
    SDEV_ST_AVAILABLE = 0x01,
    /* sdev is now used. */
    SDEV_ST_USED,
    /* sdev is unusable because of a fatal error. */
    SDEV_ST_ERROR,

    /* vdev does not connect a remote device. */
    VDEV_ST_NULL,
    /* vdev is used, but the USB address is not assigned yet */
    VDEV_ST_NOTASSIGNED,
    VDEV_ST_USED,
    VDEV_ST_ERROR
};

// https://github.com/torvalds/linux/blob/master/include/uapi/linux/usb/ch9.h
enum class usb_device_speed : std::uint8_t {
    USB_SPEED_UNKNOWN = 0, /* enumerating */
    USB_SPEED_LOW,
    USB_SPEED_FULL,       /* usb 1.1 */
    USB_SPEED_HIGH,       /* usb 2.0 */
    USB_SPEED_WIRELESS,   /* wireless (usb 2.5) */
    USB_SPEED_SUPER,      /* usb 3.0 */
    USB_SPEED_SUPER_PLUS, /* usb 3.1 */
};

namespace viu::vhci {

// https://github.com/torvalds/linux/blob/master/drivers/usb/usbip/vhci.h
enum class hub_speed : std::uint8_t {
    HUB_SPEED_HIGH = 0,
    HUB_SPEED_SUPER,
};

struct virtual_device {
    hub_speed hub;
    std::uint8_t port;
    std::uint32_t status;
    std::uint32_t devid;
    std::uint8_t busnum;
    std::uint8_t devnum;
};

enum class error : std::uint8_t { no_free_port };

export class driver {
public:
    driver();
    driver(const driver&) = delete;
    driver(driver&&) = delete;
    auto operator=(const driver&) -> driver& = delete;
    auto operator=(driver&&) -> driver& = delete;

    void attach(std::uint32_t speed, std::uint8_t device_id);
    void read(boost::asio::streambuf& buffer, std::size_t size);
    void write(boost::asio::streambuf& buffer, std::size_t size);
    void request_stop();
    [[nodiscard]] static auto to_speed_enum(const std::uint16_t bcd_version)
        -> ::usb_device_speed
    {
        switch (bcd_version) {
            case 0x0100:
            case 0x0101:
            case 0x0110:
                return ::usb_device_speed::USB_SPEED_FULL;
            case 0x0200:
            case 0x201:
            case 0x210:
                return ::usb_device_speed::USB_SPEED_HIGH;
            case 0x0300:
                return ::usb_device_speed::USB_SPEED_SUPER;
            case 0x0310:
            case 0x0320:
                return ::usb_device_speed::USB_SPEED_SUPER_PLUS;
        }
        throw std::runtime_error(
            std::format("Unsupported bcd usb version: {:x}", bcd_version)
        );
    }

private:
    using device_deleter_type = std::function<void(sd_device*)>;
    using device_pointer = std::unique_ptr<sd_device, device_deleter_type>;

    void write_sysfs_attribute(
        const std::string& attr_path,
        const std::string& attribute_value
    ) const;
    [[nodiscard]] auto get_number_of_ports(
        sd_device* host_controller_device
    ) const;
    [[nodiscard]] auto count_controllers() const;
    [[nodiscard]] auto parse_status(const char* value);
    [[nodiscard]] auto refresh_status();
    [[nodiscard]] auto open();
    [[nodiscard]] auto get_free_port(usb_device_speed speed) const
        -> std::expected<std::uint8_t, vhci::error>;
    [[nodiscard]] auto attach_device(
        std::uint8_t port,
        int sockfd,
        std::uint32_t devid,
        std::uint32_t speed
    ) const;
    [[nodiscard]] auto make_device() const;
    [[nodiscard]] auto underlying_device() const noexcept
    {
        return host_controller_device_.get();
    }

    device_pointer host_controller_device_{};
    int number_of_controllers_{};
    std::vector<virtual_device> devices_{};
    usbip::socket usbip_socket_{};
};

static_assert(!std::copyable<driver>);

} // namespace viu::vhci
