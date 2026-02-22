# viu - A virtual USB device and proxy tool

**viu** is a Linux tool for USB device emulation and analysis. It allows you to
create virtual USB devices entirely in software or proxy existing physical
devices. These devices appear in lsusb just like real hardware.

**viu** is designed to be highly extensible through plugins, which can implement
new virtual device types, record traffic, inject faults, or perform custom USB
analysis. This makes it ideal for testing, development, and debugging.

See the [examples](./examples) directory or
[`usb_mock_test.cpp`](./src/usb_mock_test.cpp) for usage examples.

## Scope & platform

- **Platform:** Linux only
- **Kernel interaction:** Uses USBIP

This project is **not** intended as a general USB gadget framework, but as a
programmable, extensible USB device emulation environment.

---

## Architecture overview

viu consists of:

 - A **daemon** (viud) responsible for managing virtual and proxied USB
   devices and handling USB transfers.
 - **Plugins/extensions** that implement concrete virtual USB devices
   (e.g., HID, mass storage) or provide additional behaviors such as traffic
   recording, fault injection, or custom analysis.

The daemon handles the core device lifecycle and transfer mechanics, while
plugins provide flexibility to extend functionality and implement custom
USB behaviors.

Virtual devices are implemented as shared libraries and loaded dynamically by
the daemon.

---

## Dependencies

For a complete list of dependencies, refer to the
[Dockerfile](./Dockerfile).

Notable components:
- LLVM / libc++
- USBIP (kernel driver required)
- Boost

viu relies on USBIP to attach virtual USB devices.

Before using viu, ensure that the USBIP VHCI driver (`vhci_hcd`) is available
and loaded on your system. Many Linux distributions already ship this driver.
If it is not present, refer to the USBIP driver instructions:
[`external/usbip/README.md`](./external/usbip/README.md).

---

## How to build

### Fetch external git submodules
```sh
git submodule update --init --recursive
```

### Build the Docker image
```sh
podman build -t viu .
```

### Bootstrap
```sh
podman run --rm -i -v "$PWD":"$PWD" -w "$PWD" viu ./viu bootstrap
```

### Build viu library and daemon
```sh
podman run --rm -i -v "$PWD":"$PWD" -w "$PWD" viu ./viu build
```

### Install the daemon
```sh
./viu install
```

### Run USB mouse emulation example
```sh
viud mock \
    -c $(pwd)/examples/mouse/device.config \
    -m $(pwd)/out/build/examples/mouse/libviumouse-mock.so
```
