#!/bin/bash

exec env SUDO_ASKPASS="sudo_ask_pass.sh" sudo -A /usr/bin/gdb $@
