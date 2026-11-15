#!/usr/bin/env bash
# run.sh - the test bench. One command per level or per milestone:
#   ./bench/run.sh L0 ... L5     every test at that level
#   ./bench/run.sh M0 ... M7     every test for that milestone
#   ./bench/run.sh HW            hands-free check of the real sensors (leave the board still, stick untouched)
# Each test prints PASS / FAIL / SKIP with the measured number next to the limit.
# Results are appended to bench/results.md. See docs/TESTBENCH.md for the cards.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2
TARGET="${1:-}"
case "$TARGET" in L[0-5]|M[0-7]|HW) ;; *) echo "usage: $0 L0..L5 | M0..M7 | HW"; exit 2 ;; esac

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

# Read the CDC port for N seconds through the real reader (host/build/imu_reader).
# Log packets are saved to OUTFILE as "<arrival time> <text>"; with a 4th argument
# the IMU frames are also saved there as CSV (seq,t_us,ax,ay,az,gx,gy,gz).
# The reader's final TOTAL line is left in $TMP/total.
READER=host/build/imu_reader
capture() { # capture PORT SECONDS OUTFILE [CSVFILE]
    [ -x "$READER" ] || make -s -C host >/dev/null 2>&1
    local csv=(); [ -n "${4:-}" ] && csv=(--csv "$4")
    : > "$TMP/total"
    "$READER" "$1" --duration "$2" "${csv[@]}" 2>/dev/null | while IFS= read -r l; do
        case "$l" in
            "LOG: "*) printf '%s %s\n' "$EPOCHREALTIME" "${l#LOG: }" ;;   # stamp each line as it arrives
            TOTAL*)   echo "$l" > "$TMP/total" ;;
        esac
    done > "$3"
}

# Make sure the board runs the SIM or HW firmware; flash it if not.
ensure_fw() { # ensure_fw SIM|HW  -> 0 if the board now runs that variant
    local port dir uf2; port="$(find_port)"
    dir="firmware/build-$(echo "$1" | tr A-Z a-z)"; uf2="$dir/theremin_mouse.uf2"
    # Rebuild first (quick when nothing changed), so edited source is never tested stale.
    [ -f "$dir/CMakeCache.txt" ] && cmake --build "$dir" -j >/dev/null 2>&1
    if [ -n "$port" ] && [ -f "$uf2" ] && [ "$(sha256sum "$uf2" | cut -d" " -f1)" = "$(cat bench/.last_flashed 2>/dev/null)" ]; then
        capture "$port" 2.5 "$TMP/fw"
        grep -q "sensors=$1" "$TMP/fw" && return 0
    fi
    echo "[bench] board is not running the current $1 firmware: flashing it"
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
    # Any current firmware prints hello; SIM is used because it needs nothing wired.
    if ! ensure_fw SIM; then fail "M0 L2" "board runs the SIM firmware" "could not flash or find the Pico"; return; fi
    hello_rate "M0 L2"
    # The LED is the one thing a script cannot see: ask.
    if [ -t 0 ]; then
        local a; read -r -p ">>> Look at the Pico: is the LED changing state once a second? [y/n] " a
        check_eq "M0 L2" "LED toggles once a second (you looked; 1 = yes)" "$([ "$a" = y ] && echo 1 || echo 0)" 1
    else
        skip "M0 L2" "LED toggles once a second" "needs your eyes: run it in a terminal"
    fi
}

# Play the 30 s scripted cycle once per run, with the mouse grabbed by evtest.
# All L2 mouse tests read the same capture, $TMP/ev. Returns 0 if it is usable.
CYCLE=""
sim_cycle() { # sim_cycle TAG
    [ "$CYCLE" = ok ] && return 0
    if [ "$CYCLE" = bad ]; then skip "$1" "scripted cycle counted by evtest" "the cycle could not be captured (see above)"; return 1; fi
    CYCLE=bad
    evtest_ready "$1" "scripted cycle counted by evtest" || return 1
    stdbuf -oL evtest --grab "$EVENT" > "$TMP/ev" 2>&1 &
    EVPID=$!
    sleep 1
    # Never start the script unless the grab is certain: no grab = clicks on the desktop.
    if ! kill -0 "$EVPID" 2>/dev/null || grep -qi 'grab' "$TMP/ev"; then
        fail "$1" "scripted cycle counted by evtest" "evtest could not grab the device (is the monitor or another evtest running?)"
        kill "$EVPID" 2>/dev/null; EVPID=""; return 1
    fi
    echo "[bench] playing the 30 s scripted cycle (events go to the test, not the desktop) ..."
    SIM_RUNNING=1
    send_cmd RUN
    local i=62 lost=0
    while [ $i -gt 0 ]; do          # 31 s, checking the grab is still alive
        sleep 0.5; i=$((i - 1))
        if ! kill -0 "$EVPID" 2>/dev/null; then lost=1; break; fi
    done
    stop_sim
    kill "$EVPID" 2>/dev/null; wait "$EVPID" 2>/dev/null; EVPID=""
    if [ $lost = 1 ]; then
        fail "$1" "scripted cycle counted by evtest" "evtest stopped during the cycle; the script was stopped"; return 1
    fi
    CYCLE=ok
}
# Sum of one relative axis in an evtest log: sum_rel REL_WHEEL up|down|all|abs FILE
sum_rel() {
    awk -v pat="($1), value" -v mode="$2" 'index($0, pat) { v = $NF + 0
        if (mode == "up" && v > 0) s += v; else if (mode == "down" && v < 0) s += v
        else if (mode == "all") s += v; else if (mode == "abs") s += (v < 0 ? -v : v) }
        END { print s + 0 }' "$3"
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

    # 3. Scripted clicks, counted by evtest.
    if sim_cycle "M1 L2"; then
        local b
        for b in LEFT RIGHT MIDDLE; do
            check_eq "M1 L2" "evtest: BTN_$b presses in one cycle" "$(count_ev BTN_$b 1 "$TMP/ev")" 14
            check_eq "M1 L2" "evtest: BTN_$b releases in one cycle" "$(count_ev BTN_$b 0 "$TMP/ev")" 14
        done
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

m2_l2() {
    if ! ensure_fw SIM; then fail "M2 L2" "board runs the SIM firmware" "could not flash or find the Pico"; return; fi
    make -s -C tests build/test_m2 >/dev/null 2>&1
    local pred up down
    pred="$(tests/build/test_m2 | grep 'predicted REL_WHEEL')"
    up="$(echo "$pred" | sed -n 's/.*up=\(-*[0-9]*\).*/\1/p')"; down="$(echo "$pred" | sed -n 's/.*down=\(-*[0-9]*\).*/\1/p')"
    if [ -z "$up" ] || [ -z "$down" ]; then fail "M2 L2" "predicted wheel totals" "tests/build/test_m2 gave no prediction"; return; fi
    if sim_cycle "M2 L2"; then
        check_range "M2 L2" "evtest: REL_WHEEL scrolled up (ramp + half tilt)" "$(sum_rel REL_WHEEL up "$TMP/ev")" $((up - 1)) $((up + 1))
        check_range "M2 L2" "evtest: REL_WHEEL scrolled down (negative tilt)" "$(sum_rel REL_WHEEL down "$TMP/ev")" $((down - 1)) $((down + 1))
        check_range "M2 L2" "evtest: REL_WHEEL total over the cycle" "$(sum_rel REL_WHEEL all "$TMP/ev")" $((up + down - 1)) $((up + down + 1))
    fi
}

# min / max / v0 of the "joy raw=N v0=N" debug lines in a capture file
joy_stats() { awk '{ for (i = 1; i <= NF; i++) { if ($i ~ /^raw=/) r = substr($i, 5) + 0; if ($i ~ /^v0=/) v = substr($i, 4) + 0 } }
    / joy raw=/ { if (!n || r < mn) mn = r; if (!n || r > mx) mx = r; n++ }
    END { if (n) print mn, mx, v, n; else print "none none none 0" }' "$1"; }

m2_l3() {
    if [ ! -t 0 ]; then skip "M2 L3" "real joystick: centre, range and drift" "needs you at the keyboard: run it in a terminal"; return; fi
    if ! ensure_fw HW; then fail "M2 L3" "board runs the HW firmware" "could not flash or find the Pico"; return; fi
    local port; port="$(find_port)"
    if ! grep -q 'joy=ok' "$TMP/fw"; then
        fail "M2 L3" "joystick found at boot" "status line says joy is not ok: check the wiring to GPIO26 / 3V3 / GND, then replug"; return
    fi
    check_eq "M2 L3" "joystick found at boot (1 = yes)" 1 1

    echo; echo ">>> Take your hand OFF the joystick. Measuring for 10 s ..."
    local ev_ok=0
    if evtest_ready "M2 L3" "no scrolling at rest (evtest)"; then
        stdbuf -oL evtest --grab "$EVENT" > "$TMP/ev-rest" 2>&1 & EVPID=$!; ev_ok=1
    fi
    capture "$port" 10 "$TMP/joy-rest"
    [ $ev_ok = 1 ] && { kill "$EVPID" 2>/dev/null; wait "$EVPID" 2>/dev/null; EVPID=""; }
    local mn mx v0 n; read -r mn mx v0 n <<< "$(joy_stats "$TMP/joy-rest")"
    echo "[bench] at rest: ADC centre v0=$v0  min=$mn  max=$mx  ($n readings)"
    check_range "M2 L3" "joystick readings in 10 s" "$n" 40 60
    local drift; drift=$(awk -v a="$mn" -v b="$mx" -v c="$v0" 'BEGIN { d = (c - a > b - c) ? c - a : b - c; printf "%.3f", d / 2048 }')
    check_range "M2 L3" "centre drift at rest, as a fraction of full tilt" "$drift" 0 0.099
    [ $ev_ok = 1 ] && check_eq "M2 L3" "REL_WHEEL events at rest" "$(grep -c '(REL_WHEEL), value' "$TMP/ev-rest")" 0

    echo; read -r -p ">>> Press Enter, then push the joystick fully UP and fully DOWN a few times for 6 s. " _
    capture "$port" 6 "$TMP/joy-move"
    read -r mn mx v0 n <<< "$(joy_stats "$TMP/joy-move")"
    echo "[bench] moving: ADC min=$mn  max=$mx  centre v0=$v0"
    check_range "M2 L3" "ADC travel above centre [counts]" "$(( ${mx/none/0} - ${v0/none/0} ))" 1000 2100
    check_range "M2 L3" "ADC travel below centre [counts]" "$(( ${v0/none/0} - ${mn/none/0} ))" 1000 2100
}

m3_l2() {
    if ! ensure_fw SIM; then fail "M3 L2" "board runs the SIM firmware" "could not flash or find the Pico"; return; fi
    if sim_cycle "M3 L2"; then
        check_eq "M3 L2" "evtest: REL_X total (square returns to start)" "$(sum_rel REL_X all "$TMP/ev")" 0
        check_eq "M3 L2" "evtest: REL_Y total (square returns to start)" "$(sum_rel REL_Y all "$TMP/ev")" 0
        check_eq "M3 L2" "evtest: sum of |REL_X| (2 bursts + 2 sides)" "$(sum_rel REL_X abs "$TMP/ev")" 1000
        check_eq "M3 L2" "evtest: sum of |REL_Y| (2 bursts + 2 sides)" "$(sum_rel REL_Y abs "$TMP/ev")" 1000
        local worst
        worst=$(awk '/\(REL_[XY]\), value/ { v = $NF + 0; if (v < 0) v = -v; if (v > m) m = v } END { print m + 0 }' "$TMP/ev")
        check_range "M3 L2" "evtest: largest |REL_X| or |REL_Y| in one event" "$worst" 100 127
    fi
}

m3_l3() {
    if [ ! -t 0 ]; then skip "M3 L3" "real optical sensor: ID and 10 cm ruler" "needs you at the keyboard: run it in a terminal"; return; fi
    if ! ensure_fw HW; then fail "M3 L3" "board runs the HW firmware" "could not flash or find the Pico"; return; fi
    if grep -q 'opt=off' "$TMP/fw"; then skip "M3 L3" "real optical sensor: ID and 10 cm ruler" "optical sensor switched off: CFG_OPT_ENABLE is 0 in firmware/config.h"; return; fi
    local info id srom cpi
    info="$(grep ' info ' "$TMP/fw" | tail -1)"
    id="$(echo "$info" | sed -n 's/.*opt_id=\(0x[0-9a-f]*\).*/\1/p')"
    srom="$(echo "$info" | sed -n 's/.*opt_srom=\(0x[0-9a-f]*\).*/\1/p')"
    cpi="$(echo "$info" | sed -n 's/.*opt_cpi=\([0-9]*\).*/\1/p')"
    check_eq "M3 L3" "Product_ID register (0x42 = 66 = PMW3360)" "$(( ${id:-0} ))" 66
    check_eq "M3 L3" "SROM_ID after firmware upload (expect 4)" "$(( ${srom:-0} ))" 4
    check_eq "M3 L3" "CPI setting read back" "${cpi:-0}" 1600
    if [ "$(( ${id:-0} ))" != 66 ]; then return; fi
    evtest_ready "M3 L3" "10 cm ruler move" || return
    stdbuf -oL evtest --grab "$EVENT" > "$TMP/ev-ruler" 2>&1 & EVPID=$!
    sleep 1
    echo; read -r -p ">>> Slide the sensor exactly 10 cm to the RIGHT along a ruler, then press Enter. " _
    kill "$EVPID" 2>/dev/null; wait "$EVPID" 2>/dev/null; EVPID=""
    local x y; x="$(sum_rel REL_X all "$TMP/ev-ruler")"; y="$(sum_rel REL_Y all "$TMP/ev-ruler")"
    echo "[bench] ruler move: REL_X total=$x  REL_Y total=$y"
    # 10 cm = 3.937 inch; at 1600 CPI that is 6299 counts. +/-5%.
    check_range "M3 L3" "10 cm to the right: REL_X total [counts]" "$x" 5984 6614
    check_range "M3 L3" "10 cm to the right: REL_Y total (sideways error)" "$y" -630 630
}

# ---- IMU samples from the reader's CSV: seq,t_us,ax,ay,az,gx,gy,gz ----
# imu_awk CSVFILE 'per-row program' 'END program': fields are in v["t_us"], v["ax"] ...
imu_awk() {
    awk -F, 'NR > 1 && NF == 8 { v["seq"] = $1 + 0; v["t_us"] = $2 + 0; v["ax"] = $3 + 0; v["ay"] = $4 + 0; v["az"] = $5 + 0
                                 v["gx"] = $6 + 0; v["gy"] = $7 + 0; v["gz"] = $8 + 0; n++; '"$2"' }
         END { '"$3"' }' "$1"
}
# Mean of one field in g (accel) or deg/s (gyro): imu_mean CSVFILE ax
imu_mean() {
    imu_awk "$1" 's += v["'"$2"'"]' 'if (!n) { print "none"; exit } d = ("'"$2"'" ~ /^a/) ? 16384 : 131; printf "%.3f", s / n / d'
}
# The sine-model check shared by M4 and M5: prints "n worst_ax_error bad_lines rate max_step_error"
imu_model() {
    imu_awk "$1" '
        ph = v["t_us"] % 1000000; m = 8192 * sin(6.283185307179586 * ph / 1000000)
        m = (m < 0) ? -int(-m + 0.5) : int(m + 0.5)
        e = v["ax"] - m; if (e < 0) e = -e; if (e > worst) worst = e
        if (v["ay"] != 0 || v["az"] != 16384 || v["gx"] != 0 || v["gy"] != 0) bad++
        if (v["gz"] != ((ph < 500000) ? 11790 : -11790)) bad++
        if (n == 1) t0 = v["t_us"]
        if (n > 1 && v["seq"] == sp + 1) { j = v["t_us"] - tp - 5000; if (j < 0) j = -j; if (j > jit) jit = j }
        t1 = v["t_us"]; tp = v["t_us"]; sp = v["seq"]' '
        printf "%d %d %d %.2f %d", n, worst, bad, (n > 1 && t1 > t0) ? (n - 1) * 1000000 / (t1 - t0) : 0, jit'
}

m4_l2() {
    if ! ensure_fw SIM; then fail "M4 L2" "board runs the SIM firmware" "could not flash or find the Pico"; return; fi
    local port; port="$(find_port)"
    echo "[bench] reading IMU samples for 5 s ..."
    capture "$port" 5 "$TMP/imu-log" "$TMP/imu.csv"
    local n worst bad rate jit; read -r n worst bad rate jit <<< "$(imu_model "$TMP/imu.csv")"
    check_range "M4 L2" "IMU samples in 5 s" "${n:-0}" 900 1010
    check_range "M4 L2" "ax vs sine model: worst error [LSB]" "${worst:-99}" 0 1
    check_eq "M4 L2" "samples where ay, az, gx, gy or gz differ from the model" "${bad:-99}" 0
}

m5_l2() {
    if ! ensure_fw SIM; then fail "M5 L2" "board runs the SIM firmware" "could not flash or find the Pico"; return; fi
    local port; port="$(find_port)"
    echo "[bench] reading the framed stream for 20 s ..."
    capture "$port" 20 "$TMP/m5-log" "$TMP/m5.csv"
    local n worst bad rate jit; read -r n worst bad rate jit <<< "$(imu_model "$TMP/m5.csv")"
    local tot; tot="$(cat "$TMP/total")"
    check_range "M5 L2" "frames received in 20 s" "${n:-0}" 3900 4010
    check_range "M5 L2" "frame rate from t_us [Hz]" "${rate:-0}" 198 202
    check_range "M5 L2" "t_us step jitter, worst |step - 5000| [us]" "${jit:-999}" 0 99
    check_eq "M5 L2" "reader: CRC errors" "$(field "$tot" crc_errors)" 0
    check_eq "M5 L2" "reader: frames lost" "$(field "$tot" frames_lost)" 0
    check_range "M5 L2" "values vs sine model: worst error [LSB]" "${worst:-99}" 0 1
    check_eq "M5 L2" "samples with a wrong ay, az, gx, gy or gz" "${bad:-99}" 0
    # Per second: 1 hello + 1 info + 5 joy lines = 7 log packets.
    check_range "M5 L2" "log packets (hello, info, joy) in 20 s" "$(field "$tot" log_frames)" 130 150

    # The mouse must work with the reader open AND closed. The cached cycle (used
    # by M1-M3) ran with the port closed; this one runs with the reader reading.
    if evtest_ready "M5 L2" "mouse works while the reader is open"; then
        "$READER" "$port" --duration 34 --quiet > "$TMP/m5-open" 2>/dev/null &
        local rdpid=$!
        sleep 1
        CYCLE=""
        if sim_cycle "M5 L2"; then
            wait $rdpid 2>/dev/null
            check_eq "M5 L2" "reader open: BTN_LEFT presses in one cycle" "$(count_ev BTN_LEFT 1 "$TMP/ev")" 14
            check_eq "M5 L2" "reader open: sum of |REL_X|" "$(sum_rel REL_X abs "$TMP/ev")" 1000
            check_eq "M5 L2" "reader open: frames lost during the cycle" "$(field "$(grep '^TOTAL' "$TMP/m5-open")" frames_lost)" 0
        else
            kill $rdpid 2>/dev/null
        fi
    fi
}

m4_l3() {
    if [ ! -t 0 ]; then skip "M4 L3" "real IMU: ID, 6 faces, gyro bias, 90 degree turn" "needs you at the keyboard: run it in a terminal"; return; fi
    if ! ensure_fw HW; then fail "M4 L3" "board runs the HW firmware" "could not flash or find the Pico"; return; fi
    local port who; port="$(find_port)"
    who="$(grep ' info ' "$TMP/fw" | tail -1 | sed -n 's/.*imu_id=\(0x[0-9a-f]*\).*/\1/p')"
    check_eq "M4 L3" "WHO_AM_I register (0x68 = 104)" "$(( ${who:-0} ))" 104
    if [ "$(( ${who:-0} ))" != 104 ]; then return; fi

    # Six faces: each axis in turn points up, then down. Gravity must read +/-1 g on it.
    local face axis sign
    for face in "+Z:az:1:flat on the desk, chip facing UP" "-Z:az:-1:upside down" "+X:ax:1:on its edge, X arrow pointing UP" \
                "-X:ax:-1:on the opposite edge, X arrow pointing DOWN" "+Y:ay:1:on its end, Y arrow pointing UP" "-Y:ay:-1:on the other end, Y arrow pointing DOWN"; do
        IFS=: read -r name axis sign text <<< "$face"
        echo; read -r -p ">>> Hold the IMU still, $text. Press Enter. " _
        capture "$port" 2 "$TMP/face-log" "$TMP/face"
        local g lo hi; g="$(imu_mean "$TMP/face" "$axis")"
        if [ "$sign" = 1 ]; then lo=0.95; hi=1.05; else lo=-1.05; hi=-0.95; fi
        check_range "M4 L3" "face $name: $axis [g]" "$g" "$lo" "$hi"
        if [ "$name" = "+Z" ]; then   # flat and still: also the gyro bias (datasheet zero-rate output: +/-20 deg/s)
            local a; for a in gx gy gz; do check_range "M4 L3" "at rest: $a bias [deg/s]" "$(imu_mean "$TMP/face" "$a")" -20 20; done
        fi
    done

    echo; read -r -p ">>> Lay it flat. Press Enter, then turn it 90 degrees anticlockwise (seen from above) within 4 s. " _
    capture "$port" 4 "$TMP/turn-log" "$TMP/turn"
    local ang; ang="$(imu_awk "$TMP/turn" 'if (n > 1) { dt = v["t_us"] - tp; if (dt < 0) dt += 4294967296; a += v["gz"] / 131 * dt / 1000000 } tp = v["t_us"]' 'printf "%.1f", a')"
    check_range "M4 L3" "90 degree turn: integrated gz [deg]" "${ang:-none}" 80 100
}

SOAK_SECONDS="${SOAK_SECONDS:-600}"   # the spec says 10 minutes; shorten only when developing the bench
m6_l5() {
    if ! ensure_fw SIM; then fail "M6 L5" "board runs the SIM firmware" "could not flash or find the Pico"; return; fi
    local port tot n; port="$(find_port)"

    # 1. Soak: every frame for 10 minutes.
    echo "[bench] soak: reading the stream for $SOAK_SECONDS s ..."
    capture "$port" "$SOAK_SECONDS" "$TMP/soak-log"
    tot="$(cat "$TMP/total")"; n=$((SOAK_SECONDS * 200))
    check_range "M6 L5" "soak: frames in $SOAK_SECONDS s" "$(field "$tot" frames)" $((n - 100)) $((n + 100))
    check_eq "M6 L5" "soak: frames lost" "$(field "$tot" frames_lost)" 0
    check_eq "M6 L5" "soak: CRC errors" "$(field "$tot" crc_errors)" 0

    # 2. The mouse still works after the soak.
    CYCLE=""
    if sim_cycle "M6 L5"; then
        check_eq "M6 L5" "after soak: BTN_LEFT presses in one cycle" "$(count_ev BTN_LEFT 1 "$TMP/ev")" 14
        check_eq "M6 L5" "after soak: sum of |REL_X|" "$(sum_rel REL_X abs "$TMP/ev")" 1000
    fi

    # 3. Three reflashes in a row over the one cable (1200-baud reboot each time).
    local i ok=0
    for i in 1 2 3; do "$ROOT/bench/flash.sh" SIM >/dev/null 2>&1 && ok=$((ok + 1)); sleep 1; done
    check_eq "M6 L5" "reflashes by 1200-baud reboot that succeeded (of 3)" "$ok" 3

    # 4. Unplug and replug five times with one reader left running throughout.
    if [ ! -t 0 ]; then skip "M6 L5" "5 unplug/replugs reconnect within 2 s" "needs your hands: run it in a terminal"; return; fi
    port="$(find_port)"
    "$READER" "$port" --duration 600 --quiet --csv "$TMP/replug.csv" > "$TMP/replug-out" 2>/dev/null &
    local rdpid=$! before t0 dt
    sleep 2
    for i in 1 2 3 4 5; do
        echo; echo ">>> ($i of 5) UNPLUG the Pico now ..."
        while [ -e "$port" ]; do sleep 0.05; done
        echo ">>> ... now plug it back in."
        while [ ! -e "$port" ]; do sleep 0.02; done
        t0=$EPOCHREALTIME; before=$(wc -l < "$TMP/replug.csv"); dt=none
        local k=100
        while [ $k -gt 0 ]; do     # up to 5 s for new samples to arrive
            if [ "$(wc -l < "$TMP/replug.csv")" -gt "$before" ]; then dt=$(awk -v a="$t0" -v b="$EPOCHREALTIME" 'BEGIN { printf "%.2f", b - a }'); break; fi
            sleep 0.05; k=$((k - 1))
        done
        check_range "M6 L5" "replug $i: time from port back to data flowing [s]" "$dt" 0 2
        sleep 1
    done
    kill -INT $rdpid 2>/dev/null; wait $rdpid 2>/dev/null
    check_eq "M6 L5" "reader reconnects counted" "$(field "$(grep '^TOTAL' "$TMP/replug-out")" reconnects)" 5
}

# Hands-free check of the real sensors as wired. The board must lie still with the joystick untouched.
hw_auto() {
    if ! ensure_fw HW; then fail "HW" "board runs the HW firmware" "could not flash or find the Pico"; return; fi
    local port st; port="$(find_port)"
    capture "$port" 10 "$TMP/hw-log" "$TMP/hw"
    st="$(grep ' hello ' "$TMP/hw-log" | tail -1)"
    echo "[bench] status: ${st#* }"
    check_eq "HW" "joystick found at boot (1 = yes)" "$(echo "$st" | grep -c 'joy=ok')" 1
    check_eq "HW" "IMU found at boot (1 = yes)" "$(echo "$st" | grep -c 'imu=ok')" 1
    check_eq "HW" "optical sensor ok, or switched off (1 = yes)" "$(echo "$st" | grep -c -e 'opt=ok' -e 'opt=off')" 1
    local mn mx v0 n; read -r mn mx v0 n <<< "$(joy_stats "$TMP/hw-log")"
    check_range "HW" "joystick at rest: worst drift from centre, fraction of full tilt (deadzone 0.1)" \
        "$(awk -v a="$mn" -v b="$mx" -v c="$v0" 'BEGIN { d = (c - a > b - c) ? c - a : b - c; printf "%.3f", d / 2048 }')" 0 0.099
    local t; t="$(cat "$TMP/total" 2>/dev/null)"
    check_range "HW" "IMU frame rate over 10 s [Hz]" "$(field "$t" rate_hz)" 199 201
    check_eq "HW" "CRC errors" "$(field "$t" crc_errors)" 0
    check_eq "HW" "frames lost" "$(field "$t" frames_lost)" 0
    check_range "HW" "at rest: |a| = sqrt(ax^2+ay^2+az^2) [g]" \
        "$(imu_awk "$TMP/hw" 'x += v["ax"]; y += v["ay"]; z += v["az"]' 'if (!n) { print "none"; exit } printf "%.3f", sqrt(x*x + y*y + z*z) / n / 16384')" 0.90 1.10
    local a; for a in gx gy gz; do check_range "HW" "at rest: $a bias [deg/s]" "$(imu_mean "$TMP/hw" "$a")" -20 20; done
    check_range "HW" "at rest: gz noise, standard deviation [deg/s]" \
        "$(imu_awk "$TMP/hw" 's += v["gz"]; q += v["gz"] * v["gz"]' 'if (!n) { print "none"; exit } m = s / n; printf "%.3f", sqrt(q / n - m * m) / 131')" 0 1
}

# "@M dx dy" from the PC must come out of the USB mouse exactly, split into reports of at most 127.
m7_l2() {
    if ! ensure_fw HW; then fail "M7 L2" "board runs the HW firmware" "could not flash or find the Pico"; return; fi
    evtest_ready "M7 L2" "cursor command reaches the USB mouse" || return
    stdbuf -oL evtest --grab "$EVENT" > "$TMP/ev-move" 2>&1 & EVPID=$!   # --grab: the desktop pointer does not move
    sleep 1
    local c; for c in "M 10 0" "M 0 -10" "M 600 -450" "M -110 60" "M 1 1" "M 9999 0" "M -2000 0" "M 5" "M x y"; do send_cmd "$c"; sleep 0.3; done
    sleep 0.5
    kill "$EVPID" 2>/dev/null; wait "$EVPID" 2>/dev/null; EVPID=""
    # 10 + 600 - 110 + 1 + 2000 (9999 clamped) - 2000 = 501 ;  -10 - 450 + 60 + 1 = -399 ; the last two are malformed
    check_eq "M7 L2" "evtest: REL_X total of 7 good commands" "$(sum_rel REL_X all "$TMP/ev-move")" 501
    check_eq "M7 L2" "evtest: REL_Y total of 7 good commands" "$(sum_rel REL_Y all "$TMP/ev-move")" -399
    check_eq "M7 L2" "evtest: largest |REL_X| or |REL_Y| in one event" \
        "$(awk '/\(REL_[XY]\), value/ { v = $NF + 0; if (v < 0) v = -v; if (v > m) m = v } END { print m + 0 }' "$TMP/ev-move")" 127
}
# The PC program's maths, on made-up IMU data with known answers.
m7_motion() {
    local out; out="$(make -s -C theremin test 2>&1)"
    echo "$out" | grep -E '^(PASS|FAIL)' | while read -r st rest; do echo "$st  M7 L0  $rest"; done
    check_eq "M7 L0" "motion maths: failed checks" "$(echo "$out" | grep -c '^FAIL')" 0
    check_range "M7 L0" "motion maths: passed checks" "$(echo "$out" | grep -c '^PASS')" 140 400
}

not_built() { skip "$1" "$2" "not built until milestone $3"; }

# ---------- what runs for each target ----------
run_level() {
    case "$1" in
        L0) l0 ;;
        L1) m5_l1 ;;
        L2) m0_l2; m4_l2; m2_l2; m3_l2; m5_l2; m1_l2 ;;
        L3) m1_l3; m2_l3; m3_l3; m4_l3 ;;
        L4) skip "L4" "everything real and integrated" "run after the L3 tests pass: ./bench/run.sh L3, then use the mouse and bench/build/monitor" ;;
        L5) m6_l5 ;;
    esac
}
run_milestone() {
    case "$1" in
        M0) m0_l2 ;;
        M1) l0 m1; m1_l2; m1_l3 ;;
        M2) l0 m2; m2_l2; m2_l3 ;;
        M3) l0 m3; m3_l2; m3_l3 ;;
        M4) l0 m4; m4_l2; m4_l3 ;;
        M5) l0 m5; m5_l1; m5_l2 ;;
        M6) m6_l5 ;;
        M7) l0 m7; m7_motion; hw_auto; m7_l2 ;;
    esac
}

: > "$TMP/results"
echo "=== ./bench/run.sh $TARGET ==="
case "$TARGET" in L*) run_level "$TARGET" ;; M*) run_milestone "$TARGET" ;; HW) hw_auto ;; esac

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
