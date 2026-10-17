#!/bin/sh
set -eu
umask 077
caller_dir=$(pwd)
cd "$(dirname "$0")"
registry="../../registry/example.json"
if [ "$#" -gt 0 ]; then
    [ "$#" -eq 2 ] && [ "$1" = "--registry" ] || { echo "usage: build.sh [--registry FILE]" >&2; exit 2; }
    case "$2" in /*) registry="$2" ;; *) registry="$caller_dir/$2" ;; esac
fi
sh usb/build.sh >&2
mkdir -p .build
chmod 700 .build
build_dir=$(mktemp -d .build/build.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
sources='USBMain.swift USBClock.swift USBContinuousEstimate.swift USBFrameBridge.swift PreciseClock.swift PreciseClockTests.swift FineChrony.swift FineChronyTests.swift SOFMapTests.swift PrecisionReplay.swift ObserverQueue.swift ClockComparison.swift ClockComparisonTests.swift USBObservation.swift USBObservationTests.swift TrialReport.swift InstrumentView.swift UnicastTiming.swift TimeMath.swift Chrony.swift ChronyTests.swift ChronyView.swift ChronyViewTests.swift ChronySession.swift DisplayHealth.swift'
python3 registry.swift.py "$registry" "$build_dir/Registry.swift"
cp "$registry" "$build_dir/registry.json"
build_hash=$(cat $sources "$build_dir/Registry.swift" registry.swift.py build_receipt.py ../../registry/registry.py Info.plist build.sh ptob usb/USBReferenceProbe.m usb/build.sh | shasum -a 256 | cut -d ' ' -f 1)
printf 'let buildSourceSHA256 = "%s"\n' "$build_hash" > "$build_dir/Build.swift"
xcrun swiftc -module-cache-path .build/ModuleCache -parse-as-library -O -swift-version 5 $sources "$build_dir/Build.swift" "$build_dir/Registry.swift" -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker Info.plist -o "$build_dir/ptob-receiver"
codesign --force --sign - --identifier org.ptob.usb-clock-instrument "$build_dir/ptob-receiver"
mv "$build_dir/ptob-receiver" .build/ptob-receiver
mv "$build_dir/registry.json" .build/registry.json
python3 build_receipt.py
shasum -a 256 .build/ptob-receiver
