#!/usr/bin/env bash
# run.sh - the test bench. One command per level or per milestone:
#   ./bench/run.sh L0 ... L5     every test at that level
#   ./bench/run.sh M0 ... M6     every test for that milestone
# Each test prints PASS / FAIL / SKIP with the measured number next to the limit.
# Results are appended to bench/results.md. See docs/TESTBENCH.md for the cards.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2
TARGET="${1:-}"
case "$TARGET" in L[0-5]|M[0-6]) ;; *) echo "usage: $0 L0..L5 | M0..M6"; exit 2 ;; esac

RESULTS="$ROOT/bench/results.md"
TMP="$(mktemp -d)"
SIM_RUNNING=0     # 1 while the Pico is playing the scripted clicks
EVPID=""
cleanup() {
    # If we are interrupted mid-script, stop the clicks BEFORE the grab is released.
    if [ "$SIM_RUNNING" = 1 ]; then stop_sim; fi
    [ -n "$EVPID" ] && kill "$EVPID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT
trap 'exit 130' INT TERM
NPASS=0; NFAIL=0; NSKIP=0
L1_SECONDS="${L1_SECONDS:-60}"   # shorten only when developing the bench itself

# ---------- reporting ----------
line() { # line STATUS TAG NAME MEASURED LIMIT
    local s; s=$(printf '%-4s  %-6s %-50s measured=%s  limit=%s' "$1" "$2" "$3" "$4" "$5")
    echo "$s"; echo "$s" >> "$TMP/results"
    case "$1" in PASS) NPASS=$((NPASS + 1)) ;; FAIL) NFAIL=$((NFAIL + 1)) ;; SKIP) NSKIP=$((NSKIP + 1)) ;; esac
}
check_range() { # check_range TAG NAME MEASURED LO HI
    local st=FAIL lim="$4..$5"
    [ "$4" = "$5" ] && lim="$4"
    if awk -v m="$3" -v lo="$4" -v hi="$5" 'BEGIN { exit !(m == m + 0 && m >= lo && m <= hi) }'; then st=PASS; fi
    line "$st" "$1" "$2" "$3" "$lim"
}
check_eq() { check_range "$1" "$2" "$3" "$4" "$4"; }
skip() { line SKIP "$1" "$2" "-" "$3"; }   # skip TAG NAME REASON
fail() { line FAIL "$1" "$2" "-" "$3"; }   # fail TAG NAME REASON

# ---------- finding the Pico ----------
find_port()  { ls /dev/serial/by-id/usb-Ant_Theremin_Mouse_*-if00 /dev/serial/by-id/usb-Raspberry_Pi_Pico_*-if00 2>/dev/null | head -1; }
find_event() { ls /dev/input/by-id/usb-Ant_Theremin_Mouse_*-event-mouse 2>/dev/null | head -1; }
in_bootsel() { lsusb -d 2e8a:0003 >/dev/null 2>&1; }

# Read the CDC port for N seconds; each line is saved as "<arrival time> <text>".
capture() { # capture PORT SECONDS OUTFILE
    stty -F "$1" raw -echo 2>/dev/null
    timeout "$2" cat "$1" 2>/dev/null | while IFS= read -r l; do
        printf '%s %s\n' "$EPOCHREALTIME" "${l%$'\r'}"    # stamp each line as it arrives
    done > "$3"
}

# Make sure the board runs the SIM or HW firmware; flash it if not.
ensure_fw() { # ensure_fw SIM|HW  -> 0 if the board now runs that variant
    local port; port="$(find_port)"
    if [ -n "$port" ]; then
        capture "$port" 2.5 "$TMP/fw"
        grep -q "sensors=$1" "$TMP/fw" && return 0
    fi
    echo "[bench] board is not running the $1 firmware: flashing it"
    "$ROOT/bench/flash.sh" "$1" || return 1
    sleep 1.5
    port="$(find_port)"; [ -n "$port" ] || return 1
    capture "$port" 2.5 "$TMP/fw"
    grep -q "sensors=$1" "$TMP/fw"
}

# Bench commands to a SIM build (see common/cmd_match.h).
send_cmd() { local port; port="$(find_port)"; [ -n "$port" ] || return 1
             stty -F "$port" raw -echo 2>/dev/null; printf '@%s\n' "$1" > "$port"; }
stop_sim() { send_cmd STOP; SIM_RUNNING=0; }

# Can we read the mouse's input device with evtest?
evtest_ready() { # evtest_ready TAG NAME -> 0 if ready, else prints the reason as a SKIP
    if ! command -v evtest >/dev/null; then
        skip "$1" "$2" "evtest not installed: sudo apt install evtest usbutils"; return 1
    fi
    EVENT="$(find_event)"
    if [ -z "$EVENT" ]; then fail "$1" "$2" "no mouse input device found (is the Pico plugged in?)"; return 1; fi
    if [ ! -r "$EVENT" ]; then
        skip "$1" "$2" "no permission on $EVENT: install bench/70-theremin-mouse.rules (see TESTBENCH.md)"; return 1
    fi
    return 0
}
count_ev() { grep -c "($1), value $2\$" "$3"; }   # count_ev BTN_LEFT 1 FILE

# ---------- L0: unit tests on the PC ----------
l0() { # l0 [mN]   run the unit tests (all, or one milestone's)
    local out="$TMP/l0"
    make -s -C tests run M="${1:-}" > "$out" 2>&1
    local rc=$?
    grep -E '^(PASS|FAIL)' "$out" | while IFS= read -r l; do echo "$l"; done
    grep -E '^(PASS|FAIL)' "$out" >> "$TMP/results"
    NPASS=$((NPASS + $(grep -c '^PASS' "$out")))
    NFAIL=$((NFAIL + $(grep -c '^FAIL' "$out")))
    if [ $rc -ne 0 ] && ! grep -q '^FAIL' "$out"; then
        tail -15 "$out"; fail "L0" "unit tests build and run" "make -C tests failed (output above)"
    fi
}

# ---------- L1: whole pipeline on the PC (fake Pico on a pseudo-terminal) ----------
l1_run() { # l1_run NAME pico_sim-options...  -> sets SIM_LINE and READER_LINE
    local name="$1"; shift
    local link="$TMP/pty-$name"
    bench/build/pico_sim --duration "$L1_SECONDS" --link "$link" "$@" > "$TMP/sim-$name" 2>&1 &
    local simpid=$!
    local n=50; while [ ! -e "$link" ] && [ $n -gt 0 ]; do sleep 0.1; n=$((n - 1)); done
    host/build/imu_reader "$link" --duration $((L1_SECONDS + 3)) --quiet > "$TMP/rd-$name" 2>&1
    wait $simpid 2>/dev/null
    SIM_LINE="$(grep '^INJECTED' "$TMP/sim-$name" | tail -1)"
    READER_LINE="$(grep '^TOTAL' "$TMP/rd-$name" | tail -1)"
}
field() { echo "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1; }   # field "LINE" key

m5_l1() {
    if ! make -s -C host >"$TMP/mk" 2>&1 || ! make -s -C bench >>"$TMP/mk" 2>&1; then
        tail -15 "$TMP/mk"; fail "M5 L1" "build reader and pico_sim" "make failed (output above)"; return
    fi
    local expect=$((L1_SECONDS * 200))
    echo "[bench] L1 clean run: ${L1_SECONDS} s at 200 Hz ..."
    l1_run clean
    check_eq "M5 L1" "clean: frames received in ${L1_SECONDS} s" "$(field "$READER_LINE" frames)" "$expect"
    check_eq "M5 L1" "clean: CRC errors" "$(field "$READER_LINE" crc_errors)" 0
    check_eq "M5 L1" "clean: seq gaps" "$(field "$READER_LINE" seq_gaps)" 0
    check_eq "M5 L1" "clean: frames lost" "$(field "$READER_LINE" frames_lost)" 0

    echo "[bench] L1 fault run: 1% bad CRC, 0.5% dropped, 2% noise, 5% split ..."
    l1_run faults --corrupt 1 --drop 0.5 --noise 2 --split 5 --seed 1
    local bad dropped sent
    bad="$(field "$SIM_LINE" bad_crc)"; dropped="$(field "$SIM_LINE" dropped)"; sent="$(field "$SIM_LINE" frames_sent)"
    check_range "M5 L1" "faults: bad-CRC frames injected (about 1%)" "${bad:-none}" $((expect / 200)) $((expect / 50))
    check_eq "M5 L1" "faults: reader CRC errors = injected" "$(field "$READER_LINE" crc_errors)" "${bad:-x}"
    check_eq "M5 L1" "faults: reader frames lost = bad + dropped" "$(field "$READER_LINE" frames_lost)" $(( ${bad:-0} + ${dropped:-0} ))
    check_eq "M5 L1" "faults: reader good frames = sent - bad" "$(field "$READER_LINE" frames)" $(( ${sent:-0} - ${bad:-0} ))
}

# ---------- L2 / L3: tests on the real Pico ----------
hello_rate() { # hello_rate TAG   "hello" lines must arrive at 1 Hz +/-5% over 10 s
    local port; port="$(find_port)"
    if [ -z "$port" ]; then fail "$1" "hello rate over 10 s [Hz]" "no Pico serial port found"; return; fi
    echo "[bench] listening to $port for 12 s ..."
    capture "$port" 12 "$TMP/hello"
    local n rate
    n=$(grep -c '^[0-9.]* hello' "$TMP/hello")
    rate=$(awk '$2 == "hello" || $2 ~ /^hello/ { if (!n) first = $1; last = $1; n++ } END { if (n > 1) printf "%.3f", (n - 1) / (last - first); else print "none" }' "$TMP/hello")
    check_range "$1" "hello lines received in 12 s" "$n" 10 13
    check_range "$1" "hello rate over 10 s [Hz]" "$rate" 0.95 1.05
}

m0_l2() {
    hello_rate "M0 L2"
    # The LED is the one thing a script cannot see: ask.
    if [ -t 0 ]; then
        local a; read -r -p ">>> Look at the Pico: is the LED changing state once a second? [y/n] " a
        check_eq "M0 L2" "LED toggles once a second (you looked; 1 = yes)" "$([ "$a" = y ] && echo 1 || echo 0)" 1
    else
        skip "M0 L2" "LED toggles once a second" "needs your eyes: run it in a terminal"
    fi
}

m1_l2() {
    if ! ensure_fw SIM; then fail "M1 L2" "board runs the SIM firmware" "could not flash or find the Pico"; return; fi

    # 1. Descriptors, straight from lsusb.
    lsusb -v -d cafe:4005 > "$TMP/lsusb" 2>/dev/null
    local hid proto interval acm
    hid=$(grep -c 'bInterfaceClass *3 Human Interface Device' "$TMP/lsusb")
    proto=$(awk '/bInterfaceClass/ { hid = ($2 == 3) } hid && /bInterfaceProtocol/ { print $2; exit }' "$TMP/lsusb")
    interval=$(awk '/bInterfaceClass/ { hid = ($2 == 3) } hid && /bInterval/ { print $2; exit }' "$TMP/lsusb")
    acm=$(awk '/bInterfaceClass/ { cdc = ($2 == 2) } cdc && /bInterfaceSubClass *2 / { n++ } END { print n + 0 }' "$TMP/lsusb")
    check_eq "M1 L2" "lsusb: HID interfaces" "$hid" 1
    check_eq "M1 L2" "lsusb: HID protocol (2 = mouse)" "${proto:-none}" 2
    check_eq "M1 L2" "lsusb: HID endpoint bInterval [ms]" "${interval:-none}" 1
    check_eq "M1 L2" "lsusb: CDC ACM interfaces" "$acm" 1

    # 2. Debug text still prints.
    hello_rate "M1 L2"

    # 3. Scripted clicks, counted by evtest. The grab keeps them away from the desktop.
    if evtest_ready "M1 L2" "scripted clicks counted by evtest"; then
        stdbuf -oL evtest --grab "$EVENT" > "$TMP/ev" 2>&1 &
        EVPID=$!
        sleep 1
        # Never start the clicks unless the grab is certain: no grab = clicks on the desktop.
        if ! kill -0 "$EVPID" 2>/dev/null || grep -qi 'grab' "$TMP/ev"; then
            fail "M1 L2" "scripted clicks counted by evtest" "evtest could not grab the device (is the monitor or another evtest running?)"
        else
            echo "[bench] playing the 30 s scripted cycle (clicks go to the test, not the desktop) ..."
            SIM_RUNNING=1
            send_cmd RUN
            local i=62 lost=0
            while [ $i -gt 0 ]; do          # 31 s, checking the grab is still alive
                sleep 0.5; i=$((i - 1))
                if ! kill -0 "$EVPID" 2>/dev/null; then lost=1; break; fi
            done
            stop_sim
            if [ $lost = 1 ]; then
                fail "M1 L2" "scripted clicks counted by evtest" "evtest stopped during the cycle; the script was stopped"
            else
                local b
                for b in LEFT RIGHT MIDDLE; do
                    check_eq "M1 L2" "evtest: BTN_$b presses in one cycle" "$(count_ev BTN_$b 1 "$TMP/ev")" 14
                    check_eq "M1 L2" "evtest: BTN_$b releases in one cycle" "$(count_ev BTN_$b 0 "$TMP/ev")" 14
                done
            fi
        fi
        kill "$EVPID" 2>/dev/null; wait "$EVPID" 2>/dev/null; EVPID=""
    fi

    # 4. 1200-baud reboot reaches BOOTSEL within 3 s. Run last: it stops the firmware.
    local port t0 t1 ms=none; port="$(find_port)"
    t0=$EPOCHREALTIME
    stty -F "$port" 1200 2>/dev/null
    local n=50
    while [ $n -gt 0 ]; do
        if in_bootsel; then t1=$EPOCHREALTIME; ms=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.2f", b - a }'); break; fi
        sleep 0.1; n=$((n - 1))
    done
    check_range "M1 L2" "1200-baud reboot: time to BOOTSEL (2e8a:0003) [s]" "$ms" 0 3
    if in_bootsel; then
        picotool reboot >/dev/null 2>&1     # start the firmware again
        n=80; while [ -z "$(find_port)" ] && [ $n -gt 0 ]; do sleep 0.1; n=$((n - 1)); done
        check_eq "M1 L2" "firmware restarts after BOOTSEL (1 = yes)" "$([ -n "$(find_port)" ] && echo 1 || echo 0)" 1
    fi
}

m1_l3() {
    if [ ! -t 0 ]; then skip "M1 L3" "10 real presses per button" "needs you at the keyboard: run it in a terminal"; return; fi
    if ! ensure_fw HW; then fail "M1 L3" "board runs the HW firmware" "could not flash or find the Pico"; return; fi
    evtest_ready "M1 L3" "10 real presses per button" || return
    local b
    for b in LEFT RIGHT MIDDLE; do
        stdbuf -oL evtest --grab "$EVENT" > "$TMP/ev-$b" 2>&1 &
        local evpid=$!
        sleep 1
        echo
        echo ">>> Press the $b button 10 times (MIDDLE = push the joystick in), then press Enter here."
        read -r _
        kill $evpid 2>/dev/null; wait $evpid 2>/dev/null
        check_eq "M1 L3" "real BTN_$b presses (you pressed 10)" "$(count_ev BTN_$b 1 "$TMP/ev-$b")" 10
        check_eq "M1 L3" "real BTN_$b releases" "$(count_ev BTN_$b 0 "$TMP/ev-$b")" 10
    done
}

not_built() { skip "$1" "$2" "not built until milestone $3"; }

# ---------- what runs for each target ----------
run_level() {
    case "$1" in
        L0) l0 ;;
        L1) m5_l1 ;;
        L2) m0_l2; m1_l2
            not_built "M2 L2" "scripted joystick ramp: REL_WHEEL total" 2
            not_built "M3 L2" "scripted square path: REL_X / REL_Y totals" 3
            not_built "M4 L2" "IMU text printout follows the sine model" 4
            not_built "M5 L2" "200 Hz framed stream from the Pico" 5 ;;
        L3) m1_l3
            not_built "M2 L3" "real joystick: ADC centre / min / max, drift" 2
            not_built "M3 L3" "real optical sensor: ID register, 10 cm ruler" 3
            not_built "M4 L3" "real IMU: ID register, 6-face test, 90 degree turn" 4 ;;
        L4) not_built "L4" "everything real and integrated" 5 ;;
        L5) not_built "M6 L5" "10-minute soak, unplug/replug, reflash x3" 6 ;;
    esac
}
run_milestone() {
    case "$1" in
        M0) m0_l2 ;;
        M1) l0 m1; m1_l2; m1_l3 ;;
        M2) l0 m2; not_built "M2 L2" "scripted joystick ramp: REL_WHEEL total" 2
            not_built "M2 L3" "real joystick: ADC centre / min / max, drift" 2 ;;
        M3) l0 m3; not_built "M3 L2" "scripted square path: REL_X / REL_Y totals" 3
            not_built "M3 L3" "real optical sensor: ID register, 10 cm ruler" 3 ;;
        M4) l0 m4; not_built "M4 L2" "IMU text printout follows the sine model" 4
            not_built "M4 L3" "real IMU: ID register, 6-face test, 90 degree turn" 4 ;;
        M5) l0 m5; m5_l1; not_built "M5 L2" "200 Hz framed stream from the Pico" 5 ;;
        M6) not_built "M6 L5" "10-minute soak, unplug/replug, reflash x3" 6 ;;
    esac
}

: > "$TMP/results"
echo "=== ./bench/run.sh $TARGET ==="
case "$TARGET" in L*) run_level "$TARGET" ;; M*) run_milestone "$TARGET" ;; esac

VERDICT="ALL PASS"
[ "$NSKIP" -gt 0 ] && VERDICT="NOT DONE ($NSKIP skipped)"
[ "$NFAIL" -gt 0 ] && VERDICT="FAILED"
SUMMARY="$TARGET: $NPASS pass, $NFAIL fail, $NSKIP skip -> $VERDICT"
echo "=== $SUMMARY ==="

{
    echo
    echo "## $(date '+%Y-%m-%d %H:%M:%S')  git=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)$(git diff --quiet 2>/dev/null || echo '+dirty')  ./bench/run.sh $TARGET"
    echo
    echo "$SUMMARY"
    echo
    echo '```'
    cat "$TMP/results"
    echo '```'
} >> "$RESULTS"
[ "$NFAIL" -eq 0 ]
