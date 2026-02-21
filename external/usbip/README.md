# VHCI (USBIP) driver

viu uses the USBIP VHCI kernel driver (vhci_hcd) to attach virtual USB
devices.

**Important:**
Many Linux distributions already include this driver as a prebuilt
kernel module. If that’s the case, you don’t need to build it yourself - simply
follow the [Install](#install) instructions.

## Check if the driver is already available

If the VHCI driver is already installed, the following path should exist:

```sh
ls /sys/devices/platform/ | grep vhci
```

You can also check whether the module is present:

```sh
modinfo vhci_hcd
```

If either of these succeeds, you can skip directly to the
[Install](#install) step.

## Build the driver (optional)

Only follow these steps if your distribution does not ship USBIP/VHCI or if
you need a custom build.

### Get source

The USBIP driver must match your running kernel version.
Fetch the source corresponding to your kernel with:

```sh
./viu fetch-usbip --tag <kernel-version>
```

You can check your kernel version with:

```sh
uname -r
```

### Build
```sh
cd external/kernel/drivers/usb/usbip/
make -f Makefile.viu
```

### Module signing (Secure Boot only)

If Secure Boot is enabled, the kernel module must be signed.

#### Generate and import signing key
```sh
openssl_generate_mod_sign_key.sh
sign_mod.sh
sudo mokutil --import mod_signing_key.der
```

Reboot and follow the MOK enrollment instructions. After successful enrollment,
the key should appear in:

```sh
sudo cat /proc/keys
```

### Locate the kernel module

To find the installed .ko file:

```sh
find /lib/modules/$(uname -r) -name "*vhci-hcd*"
```

## Install

If the driver is shipped by your distribution or built manually, load it
using:

```sh
sudo modprobe vhci_hcd
```

After loading, the VHCI device should appear under:

```sh
/sys/devices/platform/vhci_hcd.*
```
