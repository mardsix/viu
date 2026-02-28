#ifndef VIU_USB_MOCK_ABI_HPP
#define VIU_USB_MOCK_ABI_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "usb_mock_abi.h"

namespace viu::detail {

template <typename T>
concept has_on_transfer_request_member =
    requires(T t, viu_usb_mock_transfer_control_opaque x) {
        { t.on_transfer_request(x) } -> std::same_as<void>;
    };

template <typename T>
concept has_on_transfer_request_static = requires(
    viu_usb_mock_transfer_control_opaque x
) {
    { T::on_transfer_request(x) } -> std::same_as<void>;
};

template <typename T>
concept has_on_transfer_complete_member =
    requires(T t, viu_usb_mock_transfer_control_opaque x) {
        { t.on_transfer_complete(x) } -> std::same_as<void>;
    };

template <typename T>
concept has_on_transfer_complete_static = requires(
    viu_usb_mock_transfer_control_opaque x
) {
    { T::on_transfer_complete(x) } -> std::same_as<void>;
};

template <typename T>
concept has_on_control_setup_member = requires(
    T t,
    libusb_control_setup s,
    uint8_t* data,
    size_t data_size,
    int result
) {
    { t.on_control_setup(s, data, data_size, result) } -> std::same_as<int>;
};

template <typename T>
concept has_on_control_setup_static = requires(
    libusb_control_setup s,
    const uint8_t* data,
    size_t data_size,
    int result
) {
    { T::on_control_setup(s, data, data_size, result) } -> std::same_as<int>;
};

template <typename T>
concept has_on_set_configuration_member = requires(T t, uint8_t i) {
    { t.on_set_configuration(i) } -> std::same_as<int>;
};

template <typename T>
concept has_on_set_configuration_static = requires(uint8_t i) {
    { T::on_set_configuration(i) } -> std::same_as<int>;
};

template <typename T>
concept has_on_set_interface_member = requires(T t, uint8_t i, uint8_t a) {
    { t.on_set_interface(i, a) } -> std::same_as<int>;
};

template <typename T>
concept has_on_set_interface_static = requires(uint8_t i, uint8_t a) {
    { T::on_set_interface(i, a) } -> std::same_as<int>;
};

template <typename T>
inline void dispatch_transfer_request(
    viu_usb_mock_opaque* mock,
    viu_usb_mock_transfer_control_opaque* xfer
) noexcept
{
    try {
        if constexpr (has_on_transfer_request_static<T>) {
            T::on_transfer_request(*xfer);
        } else if constexpr (has_on_transfer_request_member<T>) {
            auto* impl = static_cast<T*>(mock->ctx);
            impl->on_transfer_request(*xfer);
        }
    } catch (...) {
    }
}

template <typename T>
inline void dispatch_transfer_compete(
    viu_usb_mock_opaque* mock,
    viu_usb_mock_transfer_control_opaque* xfer
) noexcept
{
    try {
        if constexpr (has_on_transfer_complete_static<T>) {
            T::on_transfer_complete(*xfer);
        } else if constexpr (has_on_transfer_complete_member<T>) {
            auto* impl = static_cast<T*>(mock->ctx);
            impl->on_transfer_complete(*xfer);
        }
    } catch (...) {
    }
}

template <typename T>
inline int dispatch_control_setup(
    viu_usb_mock_opaque* mock,
    libusb_control_setup s,
    uint8_t* data,
    size_t data_size,
    int result
) noexcept
{
    try {
        if constexpr (has_on_control_setup_static<T>) {
            return T::on_control_setup(s, data, data_size, result);
        } else if constexpr (has_on_control_setup_member<T>) {
            return static_cast<T*>(mock->ctx)
                ->on_control_setup(s, data, data_size, result);
        }
    } catch (...) {
        return LIBUSB_ERROR_OTHER;
    }

    return LIBUSB_ERROR_NOT_SUPPORTED;
}

template <typename T>
inline int dispatch_set_configuration(
    viu_usb_mock_opaque* mock,
    uint8_t index
) noexcept
{
    try {
        if constexpr (has_on_set_configuration_static<T>) {
            return T::on_set_configuration(index);
        } else if constexpr (has_on_set_configuration_member<T>) {
            return static_cast<T*>(mock->ctx)->on_set_configuration(index);
        }
    } catch (...) {
        return LIBUSB_ERROR_OTHER;
    }

    return LIBUSB_ERROR_NOT_SUPPORTED;
}

template <typename T>
inline int dispatch_set_interface(
    viu_usb_mock_opaque* mock,
    uint8_t iface,
    uint8_t alt
) noexcept
{
    try {
        if constexpr (has_on_set_interface_static<T>) {
            return T::on_set_interface(iface, alt);
        } else if constexpr (has_on_set_interface_member<T>) {
            return static_cast<T*>(mock->ctx)->on_set_interface(iface, alt);
        }
    } catch (...) {
        return LIBUSB_ERROR_OTHER;
    }

    return LIBUSB_ERROR_NOT_SUPPORTED;
}

template <typename T>
inline void destroy_impl(viu_usb_mock_opaque* self) noexcept
{
    try {
        delete static_cast<T*>(self->ctx);
        delete self;
    } catch (...) {
    }
}

} // namespace viu::detail

#define REGISTER_USB_MOCK(Name, Type, ...)                                     \
                                                                               \
    extern "C" void Name##_on_transfer_request(                                \
        viu_usb_mock_opaque* mock,                                             \
        viu_usb_mock_transfer_control_opaque* xfer                             \
    ) noexcept                                                                 \
    {                                                                          \
        viu::detail::dispatch_transfer_request<Type>(mock, xfer);              \
    }                                                                          \
                                                                               \
    extern "C" void Name##_on_transfer_complete(                               \
        viu_usb_mock_opaque* mock,                                             \
        viu_usb_mock_transfer_control_opaque* xfer                             \
    ) noexcept                                                                 \
    {                                                                          \
        viu::detail::dispatch_transfer_compete<Type>(mock, xfer);              \
    }                                                                          \
                                                                               \
    extern "C" int Name##_on_control_setup(                                    \
        viu_usb_mock_opaque* mock,                                             \
        libusb_control_setup s,                                                \
        uint8_t* data,                                                         \
        size_t data_size,                                                      \
        int result                                                             \
    ) noexcept                                                                 \
    {                                                                          \
        return viu::detail::dispatch_control_setup<Type>(                      \
            mock,                                                              \
            s,                                                                 \
            data,                                                              \
            data_size,                                                         \
            result                                                             \
        );                                                                     \
    }                                                                          \
                                                                               \
    extern "C" int Name##_on_set_configuration(                                \
        viu_usb_mock_opaque* mock,                                             \
        uint8_t index                                                          \
    ) noexcept                                                                 \
    {                                                                          \
        return viu::detail::dispatch_set_configuration<Type>(mock, index);     \
    }                                                                          \
                                                                               \
    extern "C" int Name##_on_set_interface(                                    \
        viu_usb_mock_opaque* mock,                                             \
        uint8_t iface,                                                         \
        uint8_t alt                                                            \
    ) noexcept                                                                 \
    {                                                                          \
        return viu::detail::dispatch_set_interface<Type>(mock, iface, alt);    \
    }                                                                          \
                                                                               \
    extern "C" void Name##_destroy(viu_usb_mock_opaque* self) noexcept         \
    {                                                                          \
        delete static_cast<Type*>(self->ctx);                                  \
        delete self;                                                           \
    }                                                                          \
                                                                               \
    extern "C" viu_usb_mock_opaque* Name##_create() noexcept                   \
    {                                                                          \
        auto* self = new viu_usb_mock_opaque{};                                \
        self->ctx = new Type{__VA_ARGS__};                                     \
        self->destroy = &Name##_destroy;                                       \
        self->on_transfer_request = &Name##_on_transfer_request;               \
        self->on_transfer_complete = &Name##_on_transfer_complete;             \
        self->on_control_setup = &Name##_on_control_setup;                     \
        self->on_set_configuration = &Name##_on_set_configuration;             \
        self->on_set_interface = &Name##_on_set_interface;                     \
        return self;                                                           \
    }

#endif /* VIU_USB_MOCK_ABI_HPP */
