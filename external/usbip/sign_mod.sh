#!/bin/bash

kmodsign sha512 mod_signing_key.priv mod_signing_key.der vhci-hcd.ko
hexdump -Cv vhci-hcd.ko | tail -n 5
modinfo vhci-hcd.ko
