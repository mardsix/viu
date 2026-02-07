#!/bin/bash

exec env SUDO_ASKPASS="./.vscode/.sudo_ask_pass.sh" sudo -A /usr/bin/gdb $@
