module;

#include "libusb.h"

#include <cassert>

module viu.usb;

import std;

import viu.assert;
import viu.format;
import viu.transfer;
import viu.usb.descriptors;

using viu::usb::device;

const auto self_powered_mask = std::uint8_t{0b01000000};
const auto ep_transfer_type_mask = std::uint8_t{0b00000011};

auto device::make_list() const
{
    using deleter_type = std::function<void(libusb_device**)>;
    using device_list_pointer = std::unique_ptr<libusb_device*, deleter_type>;

    auto device_count{0};
    const auto create = [&device_count, this]() {
        libusb_device** usb_device_list{nullptr};
        device_count =
            libusb_get_device_list(libusb_context_.get(), &usb_device_list);
        if (device_count == 0) {
            device_count = LIBUSB_ERROR_NO_DEVICE;
        }

        return usb_device_list;
    };

    const auto deleter = [](libusb_device** device_list) {
        libusb_free_device_list(device_list, 1);
    };

    return std::make_tuple(
        device_list_pointer{create(), deleter},
        device_count
    );
}

auto device::make_context()
{
    auto libusb_result = int{LIBUSB_ERROR_OTHER};
    const auto create = [&libusb_result]() {
        libusb_context* context{};
        libusb_result = libusb_init(&context);
        return context;
    };

    const auto deleter = [](libusb_context* context) { libusb_exit(context); };

    return std::make_tuple(
        device::context_pointer{create(), deleter},
        libusb_result
    );
}

device::device(std::uint32_t vid, std::uint32_t pid)
    : device_id_{.vid = vid, .pid = pid}
{
    auto [context, libusb_result] = make_context();

    if (libusb_result == LIBUSB_SUCCESS) {
        libusb_context_ = std::move(context);
        const auto [usb_device_list, device_count] = make_list();

        if (device_count < 0) {
            libusb_result = device_count;
        }

        if (libusb_result == LIBUSB_SUCCESS) {
            auto usb_devices = viu::format::unsafe::vectorize(
                usb_device_list.get(),
                device_count
            );

            const auto pair_with_descriptor = [](const auto& device) {
                auto device_descriptor = libusb_device_descriptor{};
                auto libusb_result =
                    libusb_get_device_descriptor(device, &device_descriptor);
                viu::_assert(libusb_result == LIBUSB_SUCCESS);
                return std::make_tuple(device, device_descriptor);
            };

            const auto vid_and_pid_match = [this](const auto& pair) {
                const auto& descriptor = std::get<1>(pair);
                return device_id_.vid == descriptor.idVendor &&
                       device_id_.pid == descriptor.idProduct;
            };

            auto matched = usb_devices |
                           std::views::transform(pair_with_descriptor) |
                           std::views::filter(vid_and_pid_match);

            // TODO: support multiple devices with same vid:pid
            viu::_assert(
                std::distance(std::begin(matched), std::end(matched)) == 1
            );

            for (const auto& pair : matched) {
                const auto& usb_device = std::get<0>(pair);
                const auto descriptor = std::get<1>(pair);
                // descriptor.idProduct++;

                libusb_result = open_cloned_libusb_device(usb_device);
                if (libusb_result == LIBUSB_SUCCESS) {
                    device_descriptor_ = descriptor;
                    break;
                }
            }
        }
    }

    if (LIBUSB_SUCCESS != libusb_result) {
        throw std::runtime_error(
            viu::format::make_string(
                "Failed to create usb device:",
                libusb_result
            )
        );
    }
}

auto device::underlying_handle() const
{
    viu::_assert(device_handle_ != nullptr);
    return device_handle_.get();
}

auto device::release_interfaces()
{
    auto libusb_result = int{LIBUSB_ERROR_OTHER};

    for (auto ifc = std::uint8_t{0}; ifc < count_interfaces(); ++ifc) {
        libusb_result = libusb_release_interface(underlying_handle(), ifc);
        if (libusb_result != LIBUSB_SUCCESS) {
            break;
        }
    }

    return libusb_result;
}

device::~device() { close(); }

auto device::claim_interfaces()
{
    auto libusb_result = int{LIBUSB_ERROR_OTHER};

    for (auto ifc = std::uint8_t{0}; ifc < count_interfaces(); ++ifc) {
        libusb_result = libusb_claim_interface(underlying_handle(), ifc);
        if (libusb_result != LIBUSB_SUCCESS) {
            break;
        }
    }

    return libusb_result;
}

void device::close()
{
    if (has_valid_handle()) {
        const auto result = release_interfaces();
        if (result != LIBUSB_SUCCESS) {
            std::println(std::cerr, "Failed to release interfaces: {}", result);
        }
    }
}

auto device::config_descriptor(std::optional<std::uint8_t> index) const
    -> descriptor::config_descriptor_pointer
{
    const auto create = [this, index]() {
        libusb_config_descriptor* config_descriptor{nullptr};
        const auto usb_device = libusb_get_device(underlying_handle());
        viu::_assert(usb_device != nullptr);

        auto result = int{LIBUSB_ERROR_OTHER};
        if (!index.has_value()) {
            result = libusb_get_active_config_descriptor(
                usb_device,
                &config_descriptor
            );
        } else {
            result = libusb_get_config_descriptor(
                usb_device,
                *index,
                &config_descriptor
            );
        }

        viu::_assert(result == LIBUSB_SUCCESS);
        viu::_assert(config_descriptor != nullptr);
        return config_descriptor;
    };

    const auto deleter = [](libusb_config_descriptor* d) {
        libusb_free_config_descriptor(d);
    };

    return usb::descriptor::config_descriptor_pointer{create(), deleter};
}

auto device::count_interfaces() const -> std::uint8_t
{
    const auto current_config_descriptor = config_descriptor();
    viu::_assert(current_config_descriptor != nullptr);
    return current_config_descriptor->bNumInterfaces;
}

auto device::ep_transfer_type(std::uint8_t ep_address) const
    -> std::expected<libusb_endpoint_transfer_type, error>
{
    using viu::format::unsafe::vectorize;

    const auto current_config_descriptor = config_descriptor();
    viu::_assert(current_config_descriptor->interface != nullptr);

    const auto ifaces = vectorize(
        current_config_descriptor->interface,
        current_config_descriptor->bNumInterfaces
    );

    constexpr auto flatten_from = [](auto op) {
        return std::views::transform(op) | std::views::join;
    };

    const auto vector_of_altsettings = [](auto interface) {
        return vectorize(interface.altsetting, interface.num_altsetting);
    };

    const auto vector_of_endpoints = [](auto altsetting) {
        return vectorize(altsetting.endpoint, altsetting.bNumEndpoints);
    };

    auto endpoints = ifaces | flatten_from(vector_of_altsettings) |
                     flatten_from(vector_of_endpoints) |
                     std::views::filter([ep_address](auto ep) {
                         return ep.bEndpointAddress == ep_address;
                     });

    for (const auto& ep : endpoints) {
        // TODO: return the type for the current altsetting
        const auto xfer_type = ep.bmAttributes & ep_transfer_type_mask;
        return static_cast<libusb_endpoint_transfer_type>(xfer_type);
    }

    return std::unexpected(error::ep_get_transfer_type_failed);
}

auto device::pack_device_descriptor() const -> vector_type
{
    auto desc_packer = usb::descriptor::packer{};
    desc_packer.pack(device_descriptor());

    const auto current_config_descriptor = config_descriptor();
    viu::_assert(current_config_descriptor != nullptr);

    desc_packer.pack(*current_config_descriptor);
    return desc_packer.data();
}

auto device::set_configuration(std::uint8_t index) -> int
{
    auto current_index = int{-1};
    auto result = libusb_get_configuration(underlying_handle(), &current_index);
    viu::_assert(result == LIBUSB_SUCCESS);

    if (std::cmp_not_equal(index, current_index)) {
        result = libusb_set_auto_detach_kernel_driver(underlying_handle(), 0);
        viu::_assert(result == LIBUSB_SUCCESS);

        result = release_interfaces();
        viu::_assert(result == LIBUSB_SUCCESS);

        result = libusb_set_configuration(underlying_handle(), index);
        viu::_assert(result == LIBUSB_SUCCESS);

        result = libusb_set_auto_detach_kernel_driver(underlying_handle(), 1);
        viu::_assert(result == LIBUSB_SUCCESS);

        result = claim_interfaces();
        viu::_assert(result == LIBUSB_SUCCESS);
    }

    return result;
}

auto device::pack_config_descriptor(std::uint8_t index) const -> vector_type
{
    viu::_assert(has_valid_handle());
    viu::_assert(index < device_descriptor().bNumConfigurations);

    const auto config_desc = config_descriptor(index);
    viu::_assert(config_desc != nullptr);

    auto desc_packer = usb::descriptor::packer{};
    desc_packer.pack(*config_desc);
    return desc_packer.data();
}

auto device::bos_descriptor() const
    -> std::expected<usb::descriptor::bos_descriptor_pointer, libusb_error>
{
    auto libusb_result = LIBUSB_ERROR_OTHER;
    const auto create = [this, &libusb_result]() {
        libusb_bos_descriptor* bos_descriptor{nullptr};
        libusb_result = static_cast<libusb_error>(
            libusb_get_bos_descriptor(underlying_handle(), &bos_descriptor)
        );
        return bos_descriptor;
    };

    const auto deleter = [](libusb_bos_descriptor* d) {
        libusb_free_bos_descriptor(d);
    };

    const auto bos_desc = create();
    if (libusb_result != LIBUSB_SUCCESS) {
        return std::unexpected{libusb_result};
    }

    return usb::descriptor::bos_descriptor_pointer{bos_desc, deleter};
}

auto device::pack_bos_descriptor() const -> vector_type
{
    viu::_assert(has_valid_handle());

    const auto bos_desc = bos_descriptor();
    viu::_assert(bos_desc.has_value());

    auto desc_packer = usb::descriptor::packer{};
    desc_packer.pack(bos_desc->get());
    return desc_packer.data();
}

template <viu::usb::string_unit T>
auto device::string_descriptor(
    std::uint16_t language_id,
    std::uint8_t index
) const -> std::expected<std::vector<T>, error>
{
    viu::_assert(has_valid_handle());

    using data_type = std::uint8_t;
    using value_type = T;
    using size_type = std::vector<T>::size_type;
    constexpr auto numeric_limit = viu::type::numeric::max<data_type>;
    constexpr auto max_size = size_type{numeric_limit + 1};

    auto string_desc = std::vector<value_type>(max_size / sizeof(T));
    const auto libusb_result = libusb_get_string_descriptor(
        underlying_handle(),
        index,
        language_id,
        static_cast<unsigned char*>(static_cast<void*>(string_desc.data())),
        string_desc.size() * sizeof(T)
    );

    if (libusb_result <= 0) {
        return std::unexpected{error::no_string_descriptor};
    }

    string_desc.resize(libusb_result / sizeof(T));
    return string_desc;
}

auto device::string_descriptors() const
    -> usb::descriptor::string_descriptor_map
{
    using string_vec_type = std::vector<std::uint8_t>;
    auto string_map = usb::descriptor::string_descriptor_map{};

    auto supported_langs = std::vector<string_vec_type>{};
    const auto supported_langs_desc = string_descriptor<std::uint8_t>(0, 0);

    if (!supported_langs_desc.has_value()) {
        return {};
    }

    supported_langs.push_back(*supported_langs_desc);
    string_map.insert(std::make_pair(0, supported_langs));

    const auto pair_with_string_desc = [this](const auto lang_id) {
        const auto size_of = [](const auto string_desc) {
            if (!string_desc.has_value()) {
                return 0;
            }

            if ((*string_desc).size() < 2) {
                return 0;
            }

            return *string_desc->begin() - 2;
        };

        auto strings = std::vector<string_vec_type>{};
        using index_type = std::uint8_t;
        for (auto i = index_type{1}; i < viu::type::numeric::max<index_type>;
             ++i) {
            auto string_desc = string_descriptor<std::uint8_t>(lang_id, i);
            if (size_of(string_desc) != 0) {
                strings.push_back(*string_desc);
            } else {
                break;
            }
        }

        return std::make_pair(lang_id, strings);
    };

    const auto lang_ids =
        string_descriptor<usb::descriptor::language_id_type>(0, 0);
    viu::_assert(lang_ids.has_value());
    viu::_assert(lang_ids->size() > 1);

    const auto string_descs = *lang_ids | std::views::drop(1) |
                              std::views::transform(pair_with_string_desc);

    for (const auto& string_desc : string_descs) {
        if (!string_desc.second.empty()) {
            string_map.insert(string_desc);
        }
    }

    return string_map;
}

auto device::pack_string_descriptor(
    std::uint16_t language_id,
    std::uint8_t index
) const -> vector_type
{
    viu::_assert(has_valid_handle());
    auto string_desc = string_descriptor<std::uint8_t>(language_id, index);
    viu::_assert(string_desc.has_value());

    auto descriptor = vector_type{};
    usb::descriptor::packer::to_packing_type(*string_desc, descriptor);
    return descriptor;
}

auto device::report_descriptor() const
    -> std::expected<std::vector<std::uint8_t>, error>
{
    viu::_assert(has_valid_handle());
    constexpr auto max_len = 4096;
    auto hid_report_descriptor = std::vector<std::uint8_t>(max_len);

    const auto ep_direction = std::uint8_t{LIBUSB_ENDPOINT_IN};
    const auto request_type = std::uint8_t{LIBUSB_REQUEST_TYPE_STANDARD};
    const auto recipient = std::uint8_t{LIBUSB_RECIPIENT_INTERFACE};
    const auto descriptor_size = libusb_control_transfer(
        underlying_handle(),
        ep_direction | request_type | recipient,
        LIBUSB_REQUEST_GET_DESCRIPTOR,
        LIBUSB_DT_REPORT << 8,
        0,
        hid_report_descriptor.data(),
        hid_report_descriptor.size(),
        0
    );

    if (descriptor_size < 0) {
        return std::unexpected{error::no_report_descriptor};
    }

    viu::_assert(descriptor_size < max_len);
    hid_report_descriptor.resize(descriptor_size);

    return hid_report_descriptor;
}

auto device::pack_report_descriptor() const -> vector_type
{
    const auto hid_report_descriptor = report_descriptor();

    if (!hid_report_descriptor.has_value()) {
        return {};
    }

    viu::_assert(std::size(*hid_report_descriptor) > 0);

    auto report = vector_type{};
    usb::descriptor::packer::to_packing_type(*hid_report_descriptor, report);
    return report;
}

auto device::is_self_powered() const -> bool
{
    auto config_desc = config_descriptor();
    viu::_assert(config_desc != nullptr);
    return (config_desc->bmAttributes & self_powered_mask) != 0;
}

auto device::make_handle(libusb_device* const dev)
{
    auto libusb_result = int{LIBUSB_ERROR_OTHER};
    const auto create = [&libusb_result, dev]() {
        libusb_device_handle* handle{};
        libusb_result = libusb_open(dev, &handle);
        return handle;
    };

    const auto deleter = [this](libusb_device_handle* handle) {
        close();
        libusb_close(handle);
    };

    return std::make_tuple(
        device::device_handle_pointer{create(), deleter},
        libusb_result
    );
}

auto device::has_valid_handle() const noexcept -> bool
{
    return device_handle_ != nullptr;
}

auto device::open_cloned_libusb_device(libusb_device* dev) -> int
{
    auto [handle, libusb_result] = make_handle(dev);

    if (libusb_result == LIBUSB_SUCCESS) {
        device_handle_ = std::move(handle);
        libusb_result =
            libusb_set_auto_detach_kernel_driver(underlying_handle(), 1);

        viu::_assert(libusb_result == LIBUSB_SUCCESS);
        libusb_result = claim_interfaces();
    }

    return libusb_result;
}

auto device::on_set_interface(std::uint8_t interface, std::uint8_t alt_setting)
    -> int
{
    viu::_assert(has_valid_handle());
    return libusb_set_interface_alt_setting(
        underlying_handle(),
        interface,
        alt_setting
    );
}

auto device::set_interface(std::uint8_t interface, std::uint8_t alt_setting)
    -> int
{
    const auto result = on_set_interface(interface, alt_setting);
    if (result == LIBUSB_SUCCESS) {
        alt_settings_.insert({interface, alt_setting});
    }

    return result;
}

auto device::current_altsetting(std::uint8_t interface) -> std::uint8_t
{
    // TODO: Get the setting from libusb when map is empty
    if (alt_settings_.empty()) {
        return 0;
    }

    const auto altsetting = alt_settings_.find(interface);
    if (altsetting == std::end(alt_settings_)) {
        return 0;
    }

    return altsetting->second;
}

void device::submit_bulk_transfer(const usb::transfer::info& transfer_info)
{
    auto transfer_control =
        usb::transfer::fill_bulk(transfer_info, underlying_handle());
    transfer_control.attach(transfer_info.callback, cb_);
    transfer_control.submit(libusb_ctx(), cb_);
}

void device::submit_interrupt_transfer(const usb::transfer::info& transfer_info)
{
    auto transfer_control =
        usb::transfer::fill_interrupt(transfer_info, underlying_handle());
    transfer_control.attach(transfer_info.callback, cb_);
    transfer_control.submit(libusb_ctx(), cb_);
}

void device::submit_iso_transfer(const usb::transfer::info& transfer_info)
{
    auto transfer_control =
        usb::transfer::fill_iso(transfer_info, underlying_handle());
    transfer_control.attach(transfer_info.callback, cb_);
    transfer_control.submit(libusb_ctx(), cb_);
}

auto device::submit_control_setup(
    const libusb_control_setup& setup,
    const std::vector<std::uint8_t>& data
) -> std::expected<std::vector<std::uint8_t>, int>
{
    auto setup_data = std::vector<std::uint8_t>{};

    if (std::size(data) != 0) {
        std::ranges::copy(data, std::back_inserter(setup_data));
        viu::_assert(std::size(setup_data) == setup.wLength);
    }

    setup_data.resize(setup.wLength);

    const auto result = libusb_control_transfer(
        underlying_handle(),
        setup.bmRequestType,
        setup.bRequest,
        setup.wValue,
        setup.wIndex,
        setup_data.data(),
        setup.wLength,
        0
    );

    if (result > 0) {
        setup_data.resize(result);
        return setup_data;
    }

    return std::unexpected(result);
}

auto device::handle_events(
    const std::chrono::milliseconds& timeout,
    int* completed
) -> int
{
    using std::chrono::duration_cast;
    const auto micros = duration_cast<std::chrono::microseconds>(timeout);
    auto tv = timeval{.tv_sec = 0, .tv_usec = micros.count()};

    auto result = libusb_handle_events_timeout_completed(
        libusb_ctx().get(),
        &tv,
        completed
    );

    return result;
}

void device::cancel_transfers() { cb_.cancel(); }

using viu::usb::mock;

auto mock::has_valid_handle() const noexcept -> bool { return true; }

auto mock::pack_device_descriptor() const -> vector_type
{
    auto desc_packer = usb::descriptor::packer{};
    desc_packer.pack(device_descriptor());
    desc_packer.pack(descriptor_tree_.device_config());
    return desc_packer.data();
}

auto mock::pack_config_descriptor(std::uint8_t index) const -> vector_type
{
    auto desc_packer = usb::descriptor::packer{};
    desc_packer.pack(descriptor_tree_.device_config());
    return desc_packer.data();
}

auto mock::pack_bos_descriptor() const -> vector_type
{
    auto desc_packer = usb::descriptor::packer{};
    desc_packer.pack(descriptor_tree_.bos_descriptor());
    return desc_packer.data();
}

auto mock::pack_report_descriptor() const -> vector_type
{
    auto hid_report_descriptor = descriptor_tree_.report_descriptor();
    auto report = vector_type{};
    usb::descriptor::packer::to_packing_type(hid_report_descriptor, report);
    return report;
}

auto mock::pack_string_descriptor(
    std::uint16_t language_id,
    std::uint8_t index
) const -> vector_type
{
    const auto string_descriptors = descriptor_tree_.string_descriptors();
    const auto desc_vector = string_descriptors.find(language_id);
    if (desc_vector == std::end(string_descriptors)) {
        return {};
    }

    if (index > std::size(desc_vector->second)) {
        return {};
    }

    auto descriptor = vector_type{};
    usb::descriptor::packer::to_packing_type(
        desc_vector->second.at(std::cmp_equal(index, 0) ? 0 : index - 1),
        descriptor
    );

    return descriptor;
}

auto mock::set_configuration(std::uint8_t index) -> int
{
    viu::_assert(xfer_iface_ != nullptr);
    return xfer_iface_->on_set_configuration(index);
}

auto mock::on_set_interface(std::uint8_t interface, std::uint8_t alt_setting)
    -> int
{
    viu::_assert(xfer_iface_ != nullptr);
    return xfer_iface_->on_set_interface(interface, alt_setting);
}

auto mock::ep_transfer_type(std::uint8_t ep_address) const
    -> std::expected<libusb_endpoint_transfer_type, error>
{
    constexpr auto flatten_from = [](auto op) {
        return std::views::transform(op) | std::views::join;
    };

    auto interfaces = std::vector<usb::descriptor::usb_interface>{};
    descriptor_tree_.device_config().read(
        usb::descriptor::key::interface,
        interfaces
    );

    const auto vector_of_altsettings = [](const auto& interface) {
        auto v = std::vector<usb::descriptor::interface>{};
        interface.read(usb::descriptor::key::altsetting, v);
        return v;
    };

    const auto vector_of_endpoints = [](const auto& altsetting) {
        auto v = std::vector<usb::descriptor::endpoint>{};
        altsetting.read(usb::descriptor::key::ep, v);
        return v;
    };

    auto endpoints = interfaces | flatten_from(vector_of_altsettings) |
                     flatten_from(vector_of_endpoints) |
                     std::views::filter([ep_address](const auto& ep) {
                         return ep.address() == ep_address;
                     });

    for (const auto& ep : endpoints) {
        // TODO: return the type for the current altsetting
        const auto xfer_type = ep.attributes() & ep_transfer_type_mask;
        return static_cast<libusb_endpoint_transfer_type>(xfer_type);
    }

    return std::unexpected(error::ep_get_transfer_type_failed);
}

void mock::complete_transfer(const usb::transfer::control& usb_transfer) const
{
    viu::_assert(xfer_iface_ != nullptr);
    xfer_iface_->on_transfer_request(usb_transfer);
}

void mock::submit_bulk_transfer(const usb::transfer::info& transfer_info)
{
    auto transfer_control = usb::transfer::fill_bulk(transfer_info, nullptr);
    transfer_control.attach(transfer_info.callback, cb_);
    transfer_control.submit(libusb_ctx(), cb_);
    complete_transfer(transfer_control);
}

void mock::submit_interrupt_transfer(const usb::transfer::info& transfer_info)
{
    auto transfer_control =
        usb::transfer::fill_interrupt(transfer_info, nullptr);
    transfer_control.attach(transfer_info.callback, cb_);
    transfer_control.submit(libusb_ctx(), cb_);
    complete_transfer(transfer_control);
}

void mock::submit_iso_transfer(const usb::transfer::info& transfer_info)
{
    auto transfer_control = usb::transfer::fill_iso(transfer_info, nullptr);
    transfer_control.attach(transfer_info.callback, cb_);
    transfer_control.submit(libusb_ctx(), cb_);
    complete_transfer(transfer_control);
}

auto mock::submit_control_setup(
    const libusb_control_setup& setup,
    const std::vector<std::uint8_t>& data
) -> std::expected<std::vector<std::uint8_t>, int>
{
    viu::_assert(xfer_iface_ != nullptr);
    return xfer_iface_->on_control_setup(setup, data);
}

void mock::cancel_transfers() { cb_.cancel(); }
