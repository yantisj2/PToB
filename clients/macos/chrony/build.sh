#!/bin/sh
# Local tools only. Never runs make install or registers a system service.
set -eu
umask 077
here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
root="$here/.build/chrony"
mkdir -p "$root"
chmod 700 "$here/.build" "$root"
archive="$root/chrony-4.9.tar.gz"
expected=4924c6f530105bcd5b9e9e33c48a2ae1bfd889222c8480bc41601110efc864d0
if [ ! -f "$archive" ]; then
    curl --fail --location --max-time 60 https://chrony-project.org/releases/chrony-4.9.tar.gz -o "$archive.part"
    mv "$archive.part" "$archive"
fi
actual=$(shasum -a 256 "$archive" | cut -d ' ' -f 1)
[ "$actual" = "$expected" ] || { echo "chrony source SHA256 mismatch" >&2; exit 1; }
work=$(mktemp -d "$root/build.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
tar -xzf "$archive" -C "$work"
cd "$work/chrony-4.9"
./configure --disable-readline --disable-nts --without-nettle --without-gnutls --without-nss --without-tomcrypt > "$root/build.log" 2>&1
make -j4 >> "$root/build.log" 2>&1
mkdir -p "$root/bin"
cp chronyd chronyc "$root/bin/"
printf 'chrony4.9 source SHA256 %s\n' "$expected" > "$root/SOURCE.txt"
"$root/bin/chronyd" -v
shasum -a 256 "$root/bin/chronyd" "$root/bin/chronyc"

python3 "$here/chrony/build_receipt.py"
