# viu — A virtual USB device library

viu is a Linux USB device emulation library that allows you to create virtual
USB devices entirely in software. These emulated devices appear in `lsusb` just
like physical devices and can be interacted with in the same way, making viu
useful for testing, development, and debugging.

See the [examples](./examples) directory or
[`usb_mock_test.cpp`](./src/usb_mock_test.cpp) for usage examples.

---

## Scope & platform

- **Platform:** Linux only
- **Kernel interaction:** Uses USBIP

This project is **not** intended as a general USB gadget framework, but as a
programmable, extensible USB device emulation environment.

---

## Architecture overview

viu consists of:
- a **core library** that defines USB device and transfer abstractions
- a **daemon (`viud`)** responsible for device lifecycle and USBIP integration
- **plugins/extensions** that implement concrete virtual USB devices (e.g. HID)

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
# In the Docker container, may need to:
# sudo mkdir -p /lib/share/libc++
# sudo ln -s /usr/lib/llvm-21/share/libc++/v1 /lib/share/libc++/v1

podman run --rm -i -v "$PWD":"$PWD" -w "$PWD" viu ./viud bootstrap
```

### Build viu library and daemon
```sh
podman run --rm -i -v "$PWD":"$PWD" -w "$PWD" viu ./viud build
```

### Install the daemon
```sh
./viud install
```

### Run USB mouse emulation example
```sh
./cli mock \
    -c $(pwd)/examples/mouse/device.config \
    -m $(pwd)/out/build/examples/mouse/libviumouse-mock.so
```
