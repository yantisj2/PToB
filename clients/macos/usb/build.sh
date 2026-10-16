#!/bin/sh
set -eu
umask 077
cd "$(dirname "$0")"
mkdir -p ../.build/usb
chmod 700 ../.build ../.build/usb
source_sha=$(cat USBReferenceProbe.m build.sh | shasum -a 256 | cut -d ' ' -f 1)
xcrun clang -std=c11 -Wall -Wextra -Werror -fobjc-arc -fblocks -O2 \
  "-DUSB_PROBE_SOURCE_SHA=\"$source_sha\"" \
  USBReferenceProbe.m -framework Foundation -framework IOKit -framework IOUSBHost \
  -o ../.build/usb/reference-probe
codesign --force --sign - --identifier org.ptob.usb-reference-probe ../.build/usb/reference-probe
shasum -a 256 ../.build/usb/reference-probe
