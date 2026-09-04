#!/usr/bin/env bash
# Flash projects/<name>/build/merged.hex via J-Link inside WSL (usbipd-attached probe).
# Usage (from Windows):
#   wsl -d Ubuntu-24.04 -- bash /mnt/d/desktopsrc/TY/nRF54L15/scripts/wsl_flash.sh demo
# Prerequisites: usbipd attach --wsl --busid <J-Link BUSID>

set -euo pipefail

PROJECT="${1:-demo}"
REPO_WIN_ROOT="${REPO_WIN_ROOT:-/mnt/d/desktopsrc/TY/nRF54L15}"
HEX="${REPO_WIN_ROOT}/projects/${PROJECT}/build/merged.hex"

if [[ ! -f "$HEX" ]]; then
	echo "missing hex: $HEX (build on Windows first: .\\nrf.cmd -Project ${PROJECT} build)" >&2
	exit 1
fi

SCRIPT=$(mktemp /tmp/nrf54_flash.XXXXXX.jlink)
cleanup() { rm -f "$SCRIPT"; }
trap cleanup EXIT

cat >"$SCRIPT" <<EOF
SelectInterface SWD
Speed 4000
Device nRF54L15_M33
Connect
halt
loadfile ${HEX}
r
g
exit
EOF

echo "WSL flash: $HEX"
JLinkExe -nogui 1 -CommanderScript "$SCRIPT"
echo "WSL flash done."
