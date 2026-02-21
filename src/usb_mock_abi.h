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
        const uint8_t* data,
        size_t data_size,
        uint8_t* out,
        size_t* out_size
    );
    int (*on_set_configuration)(void* ctx, uint8_t index);
    int (*on_set_interface)(void* ctx, uint8_t interface, uint8_t alt_setting);
    uint64_t (*tick_interval)(void* ctx);
    void (*tick)(void* ctx);
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
