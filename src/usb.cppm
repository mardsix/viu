module;

#include <libusb.h>

export module viu.usb;

import std;

import viu.error;
import viu.tickable;
import viu.transfer;
import viu.types;
import viu.usb.descriptors;

namespace viu::usb {
template <typename T>
concept string_unit = std::same_as<T, std::uint8_t> ||
                      std::same_as<T, std::uint16_t>;

export struct device_id {
    std::uint32_t vid;
    std::uint32_t pid;
};

enum class error : std::uint8_t {
    no_string_descriptor,
    no_report_descriptor,
    ep_get_transfer_type_failed,
    io_failed
};

constexpr auto error_category_of(error /*unused*/) noexcept
{
    return viu::error_category::usb;
};

export class device {
public:
    using vector_type = usb::descriptor::vector_type;
    using context_pointer = viu::type::unique_pointer_t<libusb_context>;
    using device_handle_pointer =
        viu::type::unique_pointer_t<libusb_device_handle>;

    struct interface : public viu::tickable {
        virtual ~interface() = default;

        virtual void on_transfer_request(transfer::control xfer) = 0;

        virtual auto on_control_setup(
            const libusb_control_setup& setup,
            const std::vector<std::uint8_t>& data
        ) -> std::expected<std::vector<std::uint8_t>, int> = 0;

        virtual auto on_set_configuration(std::uint8_t index) -> int = 0;

        virtual auto on_set_interface(
            std::uint8_t interface,
            std::uint8_t alt_setting
        ) -> int = 0;
    };

    device() = default;

    device(
        std::uint32_t vid,
        std::uint32_t pid,
        std::shared_ptr<interface> xfer_iface = nullptr
    );

    virtual ~device();

    device(const device&) = delete;
    device(device&&) = delete;
    auto operator=(const device&) -> device& = delete;
    auto operator=(device&&) -> device& = delete;

    [[nodiscard]] auto pack_device_descriptor() const -> vector_type;

    [[nodiscard]] auto pack_config_descriptor(std::uint8_t index) const
        -> vector_type;

    [[nodiscard]] auto pack_bos_descriptor() const -> vector_type;
    [[nodiscard]] auto pack_report_descriptor() const -> vector_type;

    [[nodiscard]] auto pack_string_descriptor(
        std::uint16_t language_id,
        std::uint8_t index
    ) const -> vector_type;

    [[nodiscard]] virtual auto set_configuration(std::uint8_t index) -> int;
    [[nodiscard]] virtual auto is_self_powered() const -> bool;
    [[nodiscard]] auto speed() const noexcept -> std::uint16_t;

    [[nodiscard]] auto ep_transfer_type(std::uint8_t ep_address) const
        -> std::expected<libusb_endpoint_transfer_type, error>;

    auto set_interface(std::uint8_t interface, std::uint8_t alt_setting) -> int;

    [[nodiscard]] auto current_altsetting(std::uint8_t interface)
        -> std::uint8_t;

    void submit_bulk_transfer(const transfer::info& transfer_info);
    void submit_interrupt_transfer(const transfer::info& transfer_info);
    void submit_iso_transfer(const transfer::info& transfer_info);

    [[nodiscard]] virtual auto submit_control_setup(
        const libusb_control_setup& setup,
        const std::vector<std::uint8_t>& data = {}
    ) -> std::expected<std::vector<std::uint8_t>, int>;

    auto save_config(const std::filesystem::path& path) const -> viu::response;
    auto save_hid_report(const std::filesystem::path& path) const
        -> viu::response;

    [[nodiscard]] virtual auto handle_events(
        const std::chrono::milliseconds& timeout,
        int* completed
    ) -> int;

    void cancel_transfers();

    auto libusb_ctx() /*const*/ -> context_pointer& { return libusb_context_; }

private:
    [[nodiscard]] virtual auto has_valid_handle() const noexcept -> bool;
    [[nodiscard]] virtual auto underlying_handle() const
        -> libusb_device_handle*;

    [[nodiscard]] virtual auto on_set_interface(
        std::uint8_t interface,
        std::uint8_t alt_setting
    ) -> int;

    [[nodiscard]] auto open_cloned_libusb_device(libusb_device* dev) -> int;
    [[nodiscard]] auto count_interfaces() const -> std::uint8_t;
    [[nodiscard]] auto release_interfaces();
    [[nodiscard]] auto claim_interfaces();
    [[nodiscard]] auto make_list() const;
    void close();
    [[nodiscard]] auto make_context();
    [[nodiscard]] auto make_handle(libusb_device* dev);

    [[nodiscard]] auto device_descriptor() const noexcept
        -> libusb_device_descriptor;

    [[nodiscard]] auto config_descriptor(
        std::optional<std::uint8_t> index = std::nullopt
    ) const -> usb::descriptor::config_descriptor_pointer;

    [[nodiscard]] auto bos_descriptor() const -> std::
        expected<viu::usb::descriptor::bos_descriptor_pointer, libusb_error>;

    [[nodiscard]] auto report_descriptor() const
        -> std::expected<std::vector<std::uint8_t>, error>;

    [[nodiscard]] auto string_descriptors() const
        -> usb::descriptor::string_descriptor_map;

    template <string_unit T>
    [[nodiscard]] auto string_descriptor(
        std::uint16_t language_id,
        std::uint8_t index
    ) const -> std::expected<std::vector<T>, error>;

    context_pointer libusb_context_{};
    device_handle_pointer device_handle_{};
    usb::device_id device_id_{};
    std::map<const std::uint8_t, const std::uint8_t> alt_settings_{};
    usb::transfer::callback cb_{};

protected:
    std::shared_ptr<interface> xfer_iface_{};
    usb::descriptor::tree descriptor_tree_{};
};

static_assert(!std::copyable<device>);

export class mock : public device {
public:
    using interface = device::interface;

    explicit mock(
        usb::descriptor::tree descriptor_tree,
        std::shared_ptr<interface> xfer_cb
    )
    {
        descriptor_tree_ = std::move(descriptor_tree);
        xfer_iface_ = xfer_cb;
    }

    [[nodiscard]] auto set_configuration(std::uint8_t index) -> int override;
    [[nodiscard]] auto is_self_powered() const -> bool override { return true; }

    [[nodiscard]] auto submit_control_setup(
        const libusb_control_setup& setup,
        const std::vector<std::uint8_t>& data = {}
    ) -> std::expected<std::vector<std::uint8_t>, int> override;

    [[nodiscard]] auto handle_events(
        const std::chrono::milliseconds& timeout,
        [[maybe_unused]] int* completed
    ) -> int override
    {
        std::this_thread::sleep_for(timeout);
        return LIBUSB_SUCCESS;
    }

private:
    [[nodiscard]] auto has_valid_handle() const noexcept -> bool override
    {
        return true;
    }

    [[nodiscard]] auto underlying_handle() const
        -> libusb_device_handle* override
    {
        return nullptr;
    };

    auto on_set_interface(std::uint8_t interface, std::uint8_t alt_setting)
        -> int override;
};

} // namespace viu::usb
