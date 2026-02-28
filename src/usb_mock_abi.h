#ifndef VIU_USB_MOCK_ABI_H
#define VIU_USB_MOCK_ABI_H

#include <libusb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct viu_usb_mock_transfer_control_opaque {
    void* ctx;
    void (*complete)(void* ctx);
    bool (*is_in)(void* ctx);
    bool (*is_out)(void* ctx);
    void (*fill)(void* ctx, const uint8_t* data, size_t size);
    void (*read)(void* ctx, uint8_t* data, uint32_t size);
    int (*size)(void* ctx);
    unsigned char (*type)(void* ctx);
    uint8_t (*ep)(void* ctx);
    void (*read_iso_packet_descriptors)(
        void* ctx,
        struct libusb_iso_packet_descriptor* out_descriptors,
        size_t out_count
    );
    size_t (*iso_packet_descriptor_count)(void* ctx);
    void (*fill_iso_packet_descriptors)(
        void* ctx,
        const struct libusb_iso_packet_descriptor* data,
        size_t size
    );
};

struct viu_usb_mock_opaque {
    void* ctx;
    void (*on_transfer_request)(
        void* ctx,
        struct viu_usb_mock_transfer_control_opaque xfer
    );
    int (*on_control_setup)(
        void* ctx,
        struct libusb_control_setup setup,
        uint8_t* data,
        size_t data_size,
        int result
    );
    int (*on_set_configuration)(void* ctx, uint8_t index);
    int (*on_set_interface)(void* ctx, uint8_t interface, uint8_t alt_setting);
    void (*on_transfer_complete)(
        void* ctx,
        struct viu_usb_mock_transfer_control_opaque xfer
    );
    void (*destroy)(struct viu_usb_mock_opaque* self);
};

typedef struct viu_usb_mock_opaque* (*device_factory_fn)(void);

struct plugin_catalog_api {
    void* ctx;
    void (*set_name)(void* ctx, const char* name);
    void (*set_version)(void* ctx, const char* version);
    void (*register_device)(
        void* ctx,
        const char* device_name,
        device_factory_fn factory
    );
};

#ifdef __cplusplus
}
#endif

#endif /* VIU_USB_MOCK_ABI_H */
