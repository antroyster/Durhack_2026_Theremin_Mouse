#!/usr/bin/env bash
# flash.sh - build one firmware variant and flash it over the one USB cable.
#   ./bench/flash.sh SIM     scripted sensors (nothing needs to be wired)
#   ./bench/flash.sh HW      real sensors
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VARIANT="${1:-}"
case "$VARIANT" in
    SIM) BUILD="$ROOT/firmware/build-sim" ;;
    HW)  BUILD="$ROOT/firmware/build-hw" ;;
    *)   echo "usage: $0 SIM|HW"; exit 2 ;;
esac
UF2="$BUILD/theremin_mouse.uf2"

find_port() { ls /dev/serial/by-id/usb-Ant_Theremin_Mouse_*-if00 /dev/serial/by-id/usb-Raspberry_Pi_Pico_*-if00 2>/dev/null | head -1; }
in_bootsel() { lsusb -d 2e8a:0003 >/dev/null 2>&1; }
wait_for() { # wait_for SECONDS COMMAND...  (polls every 0.1 s)
    local n=$(( $1 * 10 )); shift
    while [ "$n" -gt 0 ]; do "$@" && return 0; sleep 0.1; n=$((n - 1)); done
    return 1
}

echo "[flash] building $VARIANT firmware"
# Configure every time: it is quick, and it refreshes the git hash the firmware reports.
cmake -S "$ROOT/firmware" -B "$BUILD" -DPICO_BOARD=pico -DSENSORS="$VARIANT" >/dev/null 2>&1 || { echo "[flash] cmake configure failed"; exit 1; }
if ! cmake --build "$BUILD" -j > "$BUILD/build.log" 2>&1; then
    tail -20 "$BUILD/build.log"
    echo "[flash] BUILD FAILED: nothing was flashed"; exit 1
fi
tail -1 "$BUILD/build.log"
[ -f "$UF2" ] || { echo "[flash] build failed: $UF2 missing"; exit 1; }

if ! in_bootsel; then
    PORT="$(find_port)"
    if [ -n "$PORT" ]; then
        echo "[flash] asking the Pico to reboot into BOOTSEL (1200 baud on $PORT)"
        stty -F "$PORT" 1200 2>/dev/null
        wait_for 5 in_bootsel
    fi
fi
if ! in_bootsel; then
    echo "[flash] The Pico did not enter BOOTSEL by itself."
    echo "[flash] DO THIS NOW: unplug the Pico, hold the BOOTSEL button, plug it back in, release."
    wait_for 60 in_bootsel || { echo "[flash] gave up waiting for BOOTSEL (2e8a:0003)"; exit 1; }
fi

echo "[flash] writing $UF2"
if ! picotool load -x "$UF2" > "$BUILD/flash.log" 2>&1; then
    tail -5 "$BUILD/flash.log"
    echo "[flash] picotool failed: the board was NOT flashed"; exit 1
fi
have_port() { [ -n "$(find_port)" ]; }
if wait_for 8 have_port; then
    echo "[flash] done: $(find_port)"
else
    echo "[flash] flashed, but no serial port appeared within 8 s"; exit 1
fi
