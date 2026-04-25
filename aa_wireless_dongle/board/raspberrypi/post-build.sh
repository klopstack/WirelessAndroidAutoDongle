#!/bin/bash

set -u
set -e
set -x

mv ${TARGET_DIR}/etc/aawgd.conf ${BINARIES_DIR}/aawgd.conf
ln -sf /boot/aawgd.conf ${TARGET_DIR}/etc/aawgd.conf

# Disable Buildroot-installed services that we manage ourselves from
# S30wifi_mode / S41wifi_services. Without this, dnsmasq starts at S80
# unconditionally (because /etc/dnsmasq.conf exists) and spams DHCP errors
# in STA mode.
rm -f "${TARGET_DIR}/etc/init.d/S80dnsmasq"

source board/raspberrypi/post-build.sh
