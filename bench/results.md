# Bench results

Appended by `./bench/run.sh`. Newest at the bottom.

## 2026-11-14 21:44:44  git=beca700+dirty  ./bench/run.sh M1

M1: 34 pass, 0 fail, 2 skip -> NOT DONE (2 skipped)

```
PASS  M1 L0 bouncy press, 0.1 ms sampling: press events    measured=1  limit=1
PASS  M1 L0 bouncy press, 0.1 ms sampling: release events  measured=0  limit=0
PASS  M1 L0 bouncy press: delay after edge [ms]            measured=8  limit=7.9..8.1
PASS  M1 L0 bouncy press, 1 ms sampling: press events      measured=1  limit=1
PASS  M1 L0 4 ms glitch, 0.1 ms sampling: events           measured=0  limit=0
PASS  M1 L0 4 ms glitch, 1 ms sampling: events             measured=0  limit=0
PASS  M1 L0 6 ms pulse is a real click: presses            measured=1  limit=1
PASS  M1 L0 6 ms pulse is a real click: releases           measured=1  limit=1
PASS  M1 L0 sim cycle: left presses                        measured=14  limit=14
PASS  M1 L0 sim cycle: left releases                       measured=14  limit=14
PASS  M1 L0 sim cycle: right presses                       measured=14  limit=14
PASS  M1 L0 sim cycle: right releases                      measured=14  limit=14
PASS  M1 L0 sim cycle: middle presses                      measured=14  limit=14
PASS  M1 L0 sim cycle: middle releases                     measured=14  limit=14
PASS  M1 L0 sim cycle, 0.1 ms sampling: left presses       measured=14  limit=14
PASS  M1 L0 idle for 1000 ticks: reports                   measured=0  limit=0
PASS  M1 L0 button held for 1000 ticks: reports            measured=1  limit=1
PASS  M1 L0 report byte 0 while left is held               measured=1  limit=1
PASS  M1 L0 button released, then idle: reports            measured=1  limit=1
PASS  M1 L0 report byte 0 after release                    measured=0  limit=0
PASS  M1 L0 motion on 10 ticks: reports                    measured=10  limit=10
PASS  M1 L0 report byte 1 (x = 5)                          measured=5  limit=5
PASS  M1 L0 report byte 2 (y = -3 as two's complement)     measured=253  limit=253
PASS  M1 L0 report byte 3 (wheel = 1)                      measured=1  limit=1
PASS  M1 L0 bits above bit 2 are masked off                measured=0  limit=0
PASS  M1 L0 sim cycle through the pipeline: HID reports    measured=84  limit=84
PASS  M1 L2  lsusb: HID interfaces                              measured=1  limit=1
PASS  M1 L2  lsusb: HID protocol (2 = mouse)                    measured=2  limit=2
PASS  M1 L2  lsusb: HID endpoint bInterval [ms]                 measured=1  limit=1
PASS  M1 L2  lsusb: CDC ACM interfaces                          measured=1  limit=1
PASS  M1 L2  hello lines received in 12 s                       measured=12  limit=10..13
PASS  M1 L2  hello rate over 10 s [Hz]                          measured=1.000  limit=0.95..1.05
SKIP  M1 L2  scripted clicks counted by evtest                  measured=-  limit=evtest not installed: sudo apt install evtest usbutils
PASS  M1 L2  1200-baud reboot: time to BOOTSEL (2e8a:0003) [s]  measured=0.45  limit=0..3
PASS  M1 L2  firmware restarts after BOOTSEL (1 = yes)          measured=1  limit=1
SKIP  M1 L3  10 real presses per button                         measured=-  limit=needs you at the keyboard: run it in a terminal
```

## 2026-11-14 21:45:26  git=beca700+dirty  ./bench/run.sh L0

L0: 154 pass, 0 fail, 0 skip -> ALL PASS

```
PASS  M1 L0 bouncy press, 0.1 ms sampling: press events    measured=1  limit=1
PASS  M1 L0 bouncy press, 0.1 ms sampling: release events  measured=0  limit=0
PASS  M1 L0 bouncy press: delay after edge [ms]            measured=8  limit=7.9..8.1
PASS  M1 L0 bouncy press, 1 ms sampling: press events      measured=1  limit=1
PASS  M1 L0 4 ms glitch, 0.1 ms sampling: events           measured=0  limit=0
PASS  M1 L0 4 ms glitch, 1 ms sampling: events             measured=0  limit=0
PASS  M1 L0 6 ms pulse is a real click: presses            measured=1  limit=1
PASS  M1 L0 6 ms pulse is a real click: releases           measured=1  limit=1
PASS  M1 L0 sim cycle: left presses                        measured=14  limit=14
PASS  M1 L0 sim cycle: left releases                       measured=14  limit=14
PASS  M1 L0 sim cycle: right presses                       measured=14  limit=14
PASS  M1 L0 sim cycle: right releases                      measured=14  limit=14
PASS  M1 L0 sim cycle: middle presses                      measured=14  limit=14
PASS  M1 L0 sim cycle: middle releases                     measured=14  limit=14
PASS  M1 L0 sim cycle, 0.1 ms sampling: left presses       measured=14  limit=14
PASS  M1 L0 idle for 1000 ticks: reports                   measured=0  limit=0
PASS  M1 L0 button held for 1000 ticks: reports            measured=1  limit=1
PASS  M1 L0 report byte 0 while left is held               measured=1  limit=1
PASS  M1 L0 button released, then idle: reports            measured=1  limit=1
PASS  M1 L0 report byte 0 after release                    measured=0  limit=0
PASS  M1 L0 motion on 10 ticks: reports                    measured=10  limit=10
PASS  M1 L0 report byte 1 (x = 5)                          measured=5  limit=5
PASS  M1 L0 report byte 2 (y = -3 as two's complement)     measured=253  limit=253
PASS  M1 L0 report byte 3 (wheel = 1)                      measured=1  limit=1
PASS  M1 L0 bits above bit 2 are masked off                measured=0  limit=0
PASS  M1 L0 sim cycle through the pipeline: HID reports    measured=84  limit=84
PASS  M2 L0 normalise: centre                              measured=0  limit=0
PASS  M2 L0 normalise: full positive (Q15)                 measured=32768  limit=32768
PASS  M2 L0 normalise: full negative (Q15)                 measured=-32768  limit=-32768
PASS  M2 L0 normalise: u = 0.05 is inside the deadzone     measured=0  limit=0
PASS  M2 L0 normalise: u just above 0.1 passes through (Q15) measured=3361  limit=3277..3400
PASS  M2 L0 inside deadzone for 10 s: detents              measured=0  limit=0
PASS  M2 L0 negative side of deadzone for 10 s: detents    measured=0  limit=0
PASS  M2 L0 full tilt for 5 s: detents                     measured=100  limit=99..101
PASS  M2 L0 half tilt for 5 s: detents                     measured=50  limit=49..51
PASS  M2 L0 full negative tilt for 5 s: detents            measured=-100  limit=-101..-99
PASS  M2 L0 half negative tilt for 5 s: detents            measured=-50  limit=-51..-49
PASS  M2 L0 full tilt in 20 pieces of 0.25 s: detents      measured=100  limit=99..101
PASS  M2 L0 full tilt: largest wheel value in one report   measured=1  limit=1
PASS  M2 L0 sim ramp 0->full over 5 s: detents             measured=49  limit=48..51
PASS  M2 L0 sim half tilt 5 s: detents                     measured=50  limit=49..51
PASS  M2 L0 sim negative tilt 5 s: detents                 measured=-99  limit=-101..-99
PASS  M2 L0 sim drift inside deadzone 5 s: detents         measured=0  limit=0
PASS  M2 L0 sim whole cycle: wheel total                   measured=0  limit=-2..1
PASS  M3 L0 burst of +300: sum sent                        measured=300  limit=300
PASS  M3 L0 burst of +300: reports used                    measured=3  limit=3
PASS  M3 L0 burst of +300: largest |x| in a report         measured=127  limit=0..127
PASS  M3 L0 burst of +300: carry left over                 measured=0  limit=0
PASS  M3 L0 burst of -300: sum sent                        measured=-300  limit=-300
PASS  M3 L0 random stream of 100000: in - (sent + carry)   measured=0  limit=0
PASS  M3 L0 random stream: largest |x| in a report         measured=127  limit=0..127
PASS  M3 L0 random stream after draining: in - sent        measured=0  limit=0
PASS  M3 L0 sim cycle: sum of REL_X                        measured=0  limit=0
PASS  M3 L0 sim cycle: sum of REL_Y                        measured=0  limit=0
PASS  M3 L0 sim cycle: sum of |REL_X|                      measured=1000  limit=1000
PASS  M3 L0 sim cycle: sum of |REL_Y|                      measured=1000  limit=1000
PASS  M3 L0 sim cycle: largest |x| or |y| in a report      measured=127  limit=0..127
PASS  M4 L0 accel sensitivity at +/-2 g [LSB/g]            measured=16384  limit=16384
PASS  M4 L0 accel sensitivity at +/-4 g [LSB/g]            measured=8192  limit=8192
PASS  M4 L0 accel sensitivity at +/-8 g [LSB/g]            measured=4096  limit=4096
PASS  M4 L0 accel sensitivity at +/-16 g [LSB/g]           measured=2048  limit=2048
PASS  M4 L0 gyro sensitivity at +/-250 dps [LSB/dps]       measured=131  limit=131
PASS  M4 L0 gyro sensitivity at +/-2000 dps [LSB/dps]      measured=16.4  limit=16.39..16.41
PASS  M4 L0 invalid range gives 0                          measured=0  limit=0
PASS  M4 L0 raw 16384 at +/-2 g [g]                        measured=1  limit=1
PASS  M4 L0 raw -16384 at +/-2 g [g]                       measured=-1  limit=-1
PASS  M4 L0 raw 8192 at +/-2 g [g]                         measured=0.5  limit=0.5
PASS  M4 L0 raw 32767 at +/-2 g [g]                        measured=1.99994  limit=1.999..2
PASS  M4 L0 raw 131 at +/-250 dps [dps]                    measured=1  limit=1
PASS  M4 L0 raw 32767 at +/-250 dps [dps]                  measured=250.13  limit=250..250.2
PASS  M4 L0 sim ax at t = 0.25 s [counts]                  measured=8192  limit=8191..8192
PASS  M4 L0 sim ay [counts]                                measured=0  limit=0
PASS  M4 L0 sim az [counts]                                measured=16384  limit=16384
PASS  M4 L0 sim gz at t = 0.25 s [counts]                  measured=11790  limit=11790
PASS  M4 L0 sim ax at t = 0.25 s [g]                       measured=0.5  limit=0.499..0.501
PASS  M4 L0 sim gz at t = 0.25 s [dps]                     measured=90  limit=90
PASS  M4 L0 sim ax at t = 0.75 s [counts]                  measured=-8192  limit=-8192..-8191
PASS  M4 L0 sim gz at t = 0.75 s [counts]                  measured=-11790  limit=-11790
PASS  M4 L0 sim ax at t = 3 s [counts]                     measured=0  limit=0
PASS  M4 L0 sim ax vs double model over 10 s: worst error [LSB] measured=0  limit=0..1
PASS  M4 L0 sim gz square wave over 10 s: sum (turns cancel) measured=0  limit=0
PASS  M5 L0 CRC-16/CCITT-FALSE of "123456789"              measured=10673  limit=10673
PASS  M5 L0 CRC of nothing is the initial value            measured=65535  limit=65535
PASS  M5 L0 IMU frame size [bytes]                         measured=23  limit=23
PASS  M5 L0 byte 0 sync                                    measured=170  limit=170
PASS  M5 L0 byte 1 sync                                    measured=85  limit=85
PASS  M5 L0 byte 2 type                                    measured=1  limit=1
PASS  M5 L0 bytes 3-4 seq, low byte first                  measured=48879  limit=48879
PASS  M5 L0 byte 3 is the LOW byte of seq                  measured=239  limit=239
PASS  M5 L0 bytes 5-8 t_us, low byte first                 measured=2.71285e+09  limit=2.71285e+09
PASS  M5 L0 bytes 9-10 ax                                  measured=258  limit=258
PASS  M5 L0 bytes 11-12 ay (negative)                      measured=-2  limit=-2
PASS  M5 L0 bytes 13-14 az                                 measured=16384  limit=16384
PASS  M5 L0 bytes 15-16 gx                                 measured=4370  limit=4370
PASS  M5 L0 bytes 17-18 gy                                 measured=8482  limit=8482
PASS  M5 L0 bytes 19-20 gz (most negative)                 measured=-32768  limit=-32768
PASS  M5 L0 bytes 21-22 CRC over bytes 2..20               measured=26616  limit=26616
PASS  M5 L0 packed struct: seq                             measured=48879  limit=48879
PASS  M5 L0 packed struct: az                              measured=16384  limit=16384
PASS  M5 L0 packed struct: crc                             measured=26616  limit=26616
PASS  M5 L0 single-bit errors not caught by the CRC (of 168) measured=0  limit=0
PASS  M5 L0 log frame size for 5 chars [bytes]             measured=17  limit=17
PASS  M5 L0 log byte 2 type                                measured=2  limit=2
PASS  M5 L0 log byte 9 length                              measured=5  limit=5
PASS  M5 L0 log text starts at byte 10                     measured=0  limit=0
PASS  M5 L0 log CRC over bytes 2..14                       measured=58062  limit=58062
PASS  M5 L0 log frame refuses a buffer that is too small   measured=0  limit=0
PASS  M5 L0 largest frame [bytes]                          measured=267  limit=267
PASS  M5 L0 round trip: frames out (10000 in)              measured=10000  limit=10000
PASS  M5 L0 round trip: samples with a wrong field         measured=0  limit=0
PASS  M5 L0 round trip: crc errors                         measured=0  limit=0
PASS  M5 L0 round trip: bytes discarded                    measured=0  limit=0
PASS  M5 L0 round trip: bytes refused by feed              measured=0  limit=0
PASS  M5 L0 byte-at-a-time: IMU frames                     measured=1000  limit=1000
PASS  M5 L0 byte-at-a-time: log frames                     measured=10  limit=10
PASS  M5 L0 byte-at-a-time: wrong samples                  measured=0  limit=0
PASS  M5 L0 byte-at-a-time: seq gaps                       measured=0  limit=0
PASS  M5 L0 byte-at-a-time: bytes discarded                measured=0  limit=0
PASS  M5 L0 faults: bad CRCs injected (sanity)             measured=144  limit=50..500
PASS  M5 L0 faults: frames dropped (sanity)                measured=135  limit=50..500
PASS  M5 L0 faults: frames = sent - bad_crc                measured=4721  limit=4721
PASS  M5 L0 faults: crc errors = bad_crc                   measured=144  limit=144
PASS  M5 L0 faults: frames lost = bad_crc + dropped        measured=279  limit=279
PASS  M5 L0 faults: seq gap events                         measured=261  limit=261
PASS  M5 L0 faults: bytes discarded = 23*bad + noise       measured=5484  limit=5484
PASS  M5 L0 faults: frames delivered out of order/wrong    measured=0  limit=0
PASS  M5 L0 seq wrap: frames                               measured=4  limit=4
PASS  M5 L0 seq wrap: gaps (65535 -> 0)                    measured=0  limit=0
PASS  M5 L0 seq wrap: frames lost                          measured=0  limit=0
PASS  M5 L0 seq wrap: gap across wrap, events              measured=1  limit=1
PASS  M5 L0 seq wrap: gap across wrap, lost                measured=3  limit=3
PASS  M5 L0 log: log frames                                measured=3  limit=3
PASS  M5 L0 log: IMU frames                                measured=3  limit=3
PASS  M5 L0 log: IMU seq gaps (separate counters, P5)      measured=0  limit=0
PASS  M5 L0 log: len 0 round trip                          measured=1  limit=1
PASS  M5 L0 log: typical round trip                        measured=1  limit=1
PASS  M5 L0 log: len 255 round trip                        measured=1  limit=1
PASS  M5 L0 log: crc errors                                measured=0  limit=0
PASS  M5 L0 log: bytes discarded                           measured=0  limit=0
PASS  M5 L0 garbage 1 MB: false IMU frames                 measured=0  limit=0
PASS  M5 L0 garbage 1 MB: false log frames                 measured=0  limit=0
PASS  M5 L0 garbage 1 MB: bytes refused by feed            measured=0  limit=0
PASS  M5 L0 garbage 1 MB: discarded + pending = fed        measured=1.04858e+06  limit=1.04858e+06
PASS  M5 L0 garbage 1 MB: pending at end (< 1 frame)       measured=0  limit=0..266
PASS  M5 L0 unknown type: frames found after it            measured=1  limit=1
PASS  M5 L0 unknown type: not counted as a CRC error       measured=0  limit=0
PASS  M5 L0 unknown type: bytes discarded                  measured=23  limit=23
PASS  M5 L0 unknown type: sample intact                    measured=1  limit=1
PASS  M5 L0 resync: frames found behind a false start      measured=2  limit=2
PASS  M5 L0 resync: crc errors                             measured=1  limit=1
PASS  M5 L0 resync: bytes discarded                        measured=3  limit=3
PASS  M5 L0 feed: bytes accepted when over-filled          measured=4096  limit=4096
PASS  M5 L0 feed: free space when full                     measured=0  limit=0
PASS  M5 L0 feed: free space after draining junk           measured=4096  limit=4096
```

## 2026-11-14 21:47:32  git=beca700+dirty  ./bench/run.sh L1

L1: 8 pass, 0 fail, 0 skip -> ALL PASS

```
PASS  M5 L1  clean: frames received in 60 s                     measured=12000  limit=12000
PASS  M5 L1  clean: CRC errors                                  measured=0  limit=0
PASS  M5 L1  clean: seq gaps                                    measured=0  limit=0
PASS  M5 L1  clean: frames lost                                 measured=0  limit=0
PASS  M5 L1  faults: bad-CRC frames injected (about 1%)         measured=121  limit=60..240
PASS  M5 L1  faults: reader CRC errors = injected               measured=121  limit=121
PASS  M5 L1  faults: reader frames lost = bad + dropped         measured=181  limit=181
PASS  M5 L1  faults: reader good frames = sent - bad            measured=11819  limit=11819
```

## 2026-11-14 21:53:54  git=284ce9e+dirty  ./bench/run.sh M0

M0: 2 pass, 0 fail, 1 skip -> NOT DONE (1 skipped)

```
PASS  M0 L2  hello lines received in 12 s                       measured=12  limit=10..13
PASS  M0 L2  hello rate over 10 s [Hz]                          measured=1.000  limit=0.95..1.05
SKIP  M0 L2  LED toggles once a second                          measured=-  limit=needs your eyes: run it in a terminal
```

## 2026-11-14 21:54:10  git=284ce9e+dirty  ./bench/run.sh M1

M1: 48 pass, 0 fail, 2 skip -> NOT DONE (2 skipped)

```
PASS  M1 L0 bouncy press, 0.1 ms sampling: press events    measured=1  limit=1
PASS  M1 L0 bouncy press, 0.1 ms sampling: release events  measured=0  limit=0
PASS  M1 L0 bouncy press: delay after edge [ms]            measured=8  limit=7.9..8.1
PASS  M1 L0 bouncy press, 1 ms sampling: press events      measured=1  limit=1
PASS  M1 L0 4 ms glitch, 0.1 ms sampling: events           measured=0  limit=0
PASS  M1 L0 4 ms glitch, 1 ms sampling: events             measured=0  limit=0
PASS  M1 L0 6 ms pulse is a real click: presses            measured=1  limit=1
PASS  M1 L0 6 ms pulse is a real click: releases           measured=1  limit=1
PASS  M1 L0 5 ms pulse, 1 ms sampling: events (rejected)   measured=0  limit=0
PASS  M1 L0 6 ms pulse, 1 ms sampling: presses (accepted)  measured=1  limit=1
PASS  M1 L0 press held across the clock wrap: presses      measured=1  limit=1
PASS  M1 L0 command "@RUN\n": RUN fired                    measured=1  limit=1
PASS  M1 L0 command "@STOP\n": STOP fired                  measured=1  limit=1
PASS  M1 L0 command "@@RUN\n" (restart on '@'): RUN fired  measured=1  limit=1
PASS  M1 L0 command "@RU@RUN\n": RUN fired                 measured=1  limit=1
PASS  M1 L0 command "@RUN\r\n" (CR inserted): RUN fired    measured=0  limit=0
PASS  M1 L0 command "RUN\n" (no '@'): RUN fired            measured=0  limit=0
PASS  M1 L0 command "@RUNX\n": RUN fired                   measured=0  limit=0
PASS  M1 L0 command "@run\n" (lower case): RUN fired       measured=0  limit=0
PASS  M1 L0 echoed status line: RUN fired                  measured=0  limit=0
PASS  M1 L0 echoed status line: STOP fired                 measured=0  limit=0
PASS  M1 L0 two commands in one stream: RUN fired          measured=2  limit=2
PASS  M1 L0 sim cycle: left presses                        measured=14  limit=14
PASS  M1 L0 sim cycle: left releases                       measured=14  limit=14
PASS  M1 L0 sim cycle: right presses                       measured=14  limit=14
PASS  M1 L0 sim cycle: right releases                      measured=14  limit=14
PASS  M1 L0 sim cycle: middle presses                      measured=14  limit=14
PASS  M1 L0 sim cycle: middle releases                     measured=14  limit=14
PASS  M1 L0 sim cycle, 0.1 ms sampling: left presses       measured=14  limit=14
PASS  M1 L0 idle for 1000 ticks: reports                   measured=0  limit=0
PASS  M1 L0 button held for 1000 ticks: reports            measured=1  limit=1
PASS  M1 L0 report byte 0 while left is held               measured=1  limit=1
PASS  M1 L0 button released, then idle: reports            measured=1  limit=1
PASS  M1 L0 report byte 0 after release                    measured=0  limit=0
PASS  M1 L0 motion on 10 ticks: reports                    measured=10  limit=10
PASS  M1 L0 report byte 1 (x = 5)                          measured=5  limit=5
PASS  M1 L0 report byte 2 (y = -3 as two's complement)     measured=253  limit=253
PASS  M1 L0 report byte 3 (wheel = 1)                      measured=1  limit=1
PASS  M1 L0 bits above bit 2 are masked off                measured=0  limit=0
PASS  M1 L0 sim cycle through the pipeline: HID reports    measured=84  limit=84
PASS  M1 L2  lsusb: HID interfaces                              measured=1  limit=1
PASS  M1 L2  lsusb: HID protocol (2 = mouse)                    measured=2  limit=2
PASS  M1 L2  lsusb: HID endpoint bInterval [ms]                 measured=1  limit=1
PASS  M1 L2  lsusb: CDC ACM interfaces                          measured=1  limit=1
PASS  M1 L2  hello lines received in 12 s                       measured=12  limit=10..13
PASS  M1 L2  hello rate over 10 s [Hz]                          measured=1.000  limit=0.95..1.05
SKIP  M1 L2  scripted clicks counted by evtest                  measured=-  limit=evtest not installed: sudo apt install evtest usbutils
PASS  M1 L2  1200-baud reboot: time to BOOTSEL (2e8a:0003) [s]  measured=0.44  limit=0..3
PASS  M1 L2  firmware restarts after BOOTSEL (1 = yes)          measured=1  limit=1
SKIP  M1 L3  10 real presses per button                         measured=-  limit=needs you at the keyboard: run it in a terminal
```

## 2026-11-14 21:57:35  git=7ffa944+dirty  ./bench/run.sh M1

M1: 48 pass, 0 fail, 2 skip -> NOT DONE (2 skipped)

```
PASS  M1 L0 bouncy press, 0.1 ms sampling: press events    measured=1  limit=1
PASS  M1 L0 bouncy press, 0.1 ms sampling: release events  measured=0  limit=0
PASS  M1 L0 bouncy press: delay after edge [ms]            measured=8  limit=7.9..8.1
PASS  M1 L0 bouncy press, 1 ms sampling: press events      measured=1  limit=1
PASS  M1 L0 4 ms glitch, 0.1 ms sampling: events           measured=0  limit=0
PASS  M1 L0 4 ms glitch, 1 ms sampling: events             measured=0  limit=0
PASS  M1 L0 6 ms pulse is a real click: presses            measured=1  limit=1
PASS  M1 L0 6 ms pulse is a real click: releases           measured=1  limit=1
PASS  M1 L0 5 ms pulse, 1 ms sampling: events (rejected)   measured=0  limit=0
PASS  M1 L0 6 ms pulse, 1 ms sampling: presses (accepted)  measured=1  limit=1
PASS  M1 L0 press held across the clock wrap: presses      measured=1  limit=1
PASS  M1 L0 command "@RUN\n": RUN fired                    measured=1  limit=1
PASS  M1 L0 command "@STOP\n": STOP fired                  measured=1  limit=1
PASS  M1 L0 command "@@RUN\n" (restart on '@'): RUN fired  measured=1  limit=1
PASS  M1 L0 command "@RU@RUN\n": RUN fired                 measured=1  limit=1
PASS  M1 L0 command "@RUN\r\n" (CR inserted): RUN fired    measured=0  limit=0
PASS  M1 L0 command "RUN\n" (no '@'): RUN fired            measured=0  limit=0
PASS  M1 L0 command "@RUNX\n": RUN fired                   measured=0  limit=0
PASS  M1 L0 command "@run\n" (lower case): RUN fired       measured=0  limit=0
PASS  M1 L0 echoed status line: RUN fired                  measured=0  limit=0
PASS  M1 L0 echoed status line: STOP fired                 measured=0  limit=0
PASS  M1 L0 two commands in one stream: RUN fired          measured=2  limit=2
PASS  M1 L0 sim cycle: left presses                        measured=14  limit=14
PASS  M1 L0 sim cycle: left releases                       measured=14  limit=14
PASS  M1 L0 sim cycle: right presses                       measured=14  limit=14
PASS  M1 L0 sim cycle: right releases                      measured=14  limit=14
PASS  M1 L0 sim cycle: middle presses                      measured=14  limit=14
PASS  M1 L0 sim cycle: middle releases                     measured=14  limit=14
PASS  M1 L0 sim cycle, 0.1 ms sampling: left presses       measured=14  limit=14
PASS  M1 L0 idle for 1000 ticks: reports                   measured=0  limit=0
PASS  M1 L0 button held for 1000 ticks: reports            measured=1  limit=1
PASS  M1 L0 report byte 0 while left is held               measured=1  limit=1
PASS  M1 L0 button released, then idle: reports            measured=1  limit=1
PASS  M1 L0 report byte 0 after release                    measured=0  limit=0
PASS  M1 L0 motion on 10 ticks: reports                    measured=10  limit=10
PASS  M1 L0 report byte 1 (x = 5)                          measured=5  limit=5
PASS  M1 L0 report byte 2 (y = -3 as two's complement)     measured=253  limit=253
PASS  M1 L0 report byte 3 (wheel = 1)                      measured=1  limit=1
PASS  M1 L0 bits above bit 2 are masked off                measured=0  limit=0
PASS  M1 L0 sim cycle through the pipeline: HID reports    measured=84  limit=84
PASS  M1 L2  lsusb: HID interfaces                              measured=1  limit=1
PASS  M1 L2  lsusb: HID protocol (2 = mouse)                    measured=2  limit=2
PASS  M1 L2  lsusb: HID endpoint bInterval [ms]                 measured=1  limit=1
PASS  M1 L2  lsusb: CDC ACM interfaces                          measured=1  limit=1
PASS  M1 L2  hello lines received in 12 s                       measured=12  limit=10..13
PASS  M1 L2  hello rate over 10 s [Hz]                          measured=1.000  limit=0.95..1.05
SKIP  M1 L2  scripted clicks counted by evtest                  measured=-  limit=no permission on /dev/input/by-id/usb-Ant_Theremin_Mouse_E6611CB6976D8D25-if02-event-mouse: install bench/70-theremin-mouse.rules (see TESTBENCH.md)
PASS  M1 L2  1200-baud reboot: time to BOOTSEL (2e8a:0003) [s]  measured=0.44  limit=0..3
PASS  M1 L2  firmware restarts after BOOTSEL (1 = yes)          measured=1  limit=1
SKIP  M1 L3  10 real presses per button                         measured=-  limit=no permission on /dev/input/by-id/usb-Ant_Theremin_Mouse_E6611CB6976D8D25-if02-event-mouse: install bench/70-theremin-mouse.rules (see TESTBENCH.md)
```

## 2026-11-14 22:00:28  git=7ffa944+dirty  ./bench/run.sh M0

M0: 3 pass, 0 fail, 0 skip -> ALL PASS

```
PASS  M0 L2  hello lines received in 12 s                       measured=12  limit=10..13
PASS  M0 L2  hello rate over 10 s [Hz]                          measured=1.000  limit=0.95..1.05
PASS  M0 L2  LED toggles once a second (you looked; 1 = yes)    measured=1  limit=1
```

## 2026-11-14 22:06:25  git=c0c5cdc+dirty  ./bench/run.sh M2

M2: 21 pass, 0 fail, 1 skip -> NOT DONE (1 skipped)

```
PASS  M2 L0 normalise: centre                              measured=0  limit=0
PASS  M2 L0 normalise: full positive (Q15)                 measured=32768  limit=32768
PASS  M2 L0 normalise: full negative (Q15)                 measured=-32768  limit=-32768
PASS  M2 L0 normalise: u = 0.05 is inside the deadzone     measured=0  limit=0
PASS  M2 L0 normalise: u just above 0.1 passes through (Q15) measured=3361  limit=3277..3400
PASS  M2 L0 inside deadzone for 10 s: detents              measured=0  limit=0
PASS  M2 L0 negative side of deadzone for 10 s: detents    measured=0  limit=0
PASS  M2 L0 full tilt for 5 s: detents                     measured=100  limit=99..101
PASS  M2 L0 half tilt for 5 s: detents                     measured=50  limit=49..51
PASS  M2 L0 full negative tilt for 5 s: detents            measured=-100  limit=-101..-99
PASS  M2 L0 half negative tilt for 5 s: detents            measured=-50  limit=-51..-49
PASS  M2 L0 full tilt in 20 pieces of 0.25 s: detents      measured=100  limit=99..101
PASS  M2 L0 full tilt: largest wheel value in one report   measured=1  limit=1
PASS  M2 L0 sim ramp 0->full over 5 s: detents             measured=49  limit=48..51
PASS  M2 L0 sim half tilt 5 s: detents                     measured=50  limit=49..51
PASS  M2 L0 sim negative tilt 5 s: detents                 measured=-99  limit=-101..-99
PASS  M2 L0 sim drift inside deadzone 5 s: detents         measured=0  limit=0
PASS  M2 L0 sim whole cycle: wheel total                   measured=0  limit=-2..1
PASS  M2 L2  evtest: REL_WHEEL scrolled up (ramp + half tilt)   measured=99  limit=98..100
PASS  M2 L2  evtest: REL_WHEEL scrolled down (negative tilt)    measured=-99  limit=-100..-98
PASS  M2 L2  evtest: REL_WHEEL total over the cycle             measured=0  limit=-1..1
SKIP  M2 L3  real joystick: centre, range and drift             measured=-  limit=needs you at the keyboard: run it in a terminal
```

## 2026-11-14 22:10:30  git=34f3783+dirty  ./bench/run.sh L2

L2: 21 pass, 3 fail, 3 skip -> FAILED

```
PASS  M0 L2  hello lines received in 12 s                       measured=12  limit=10..13
PASS  M0 L2  hello rate over 10 s [Hz]                          measured=1.000  limit=0.95..1.05
SKIP  M0 L2  LED toggles once a second                          measured=-  limit=needs your eyes: run it in a terminal
PASS  M2 L2  evtest: REL_WHEEL scrolled up (ramp + half tilt)   measured=98  limit=98..100
PASS  M2 L2  evtest: REL_WHEEL scrolled down (negative tilt)    measured=-99  limit=-100..-98
PASS  M2 L2  evtest: REL_WHEEL total over the cycle             measured=-1  limit=-1..1
PASS  M3 L2  evtest: REL_X total (square returns to start)      measured=0  limit=0
PASS  M3 L2  evtest: REL_Y total (square returns to start)      measured=0  limit=0
FAIL  M3 L2  evtest: sum of |REL_X| (2 bursts + 2 sides)        measured=0  limit=1000
FAIL  M3 L2  evtest: sum of |REL_Y| (2 bursts + 2 sides)        measured=0  limit=1000
FAIL  M3 L2  evtest: largest |REL_X| or |REL_Y| in one event    measured=0  limit=100..127
PASS  M1 L2  lsusb: HID interfaces                              measured=1  limit=1
PASS  M1 L2  lsusb: HID protocol (2 = mouse)                    measured=2  limit=2
PASS  M1 L2  lsusb: HID endpoint bInterval [ms]                 measured=1  limit=1
PASS  M1 L2  lsusb: CDC ACM interfaces                          measured=1  limit=1
PASS  M1 L2  hello lines received in 12 s                       measured=12  limit=10..13
PASS  M1 L2  hello rate over 10 s [Hz]                          measured=1.000  limit=0.95..1.05
PASS  M1 L2  evtest: BTN_LEFT presses in one cycle              measured=14  limit=14
PASS  M1 L2  evtest: BTN_LEFT releases in one cycle             measured=14  limit=14
PASS  M1 L2  evtest: BTN_RIGHT presses in one cycle             measured=14  limit=14
PASS  M1 L2  evtest: BTN_RIGHT releases in one cycle            measured=14  limit=14
PASS  M1 L2  evtest: BTN_MIDDLE presses in one cycle            measured=14  limit=14
PASS  M1 L2  evtest: BTN_MIDDLE releases in one cycle           measured=14  limit=14
PASS  M1 L2  1200-baud reboot: time to BOOTSEL (2e8a:0003) [s]  measured=0.44  limit=0..3
PASS  M1 L2  firmware restarts after BOOTSEL (1 = yes)          measured=1  limit=1
SKIP  M4 L2  IMU text printout follows the sine model           measured=-  limit=not built until milestone 4
SKIP  M5 L2  200 Hz framed stream from the Pico                 measured=-  limit=not built until milestone 5
```

## 2026-11-14 22:11:23  git=34f3783+dirty  ./bench/run.sh M3

M3: 18 pass, 0 fail, 1 skip -> NOT DONE (1 skipped)

```
PASS  M3 L0 burst of +300: sum sent                        measured=300  limit=300
PASS  M3 L0 burst of +300: reports used                    measured=3  limit=3
PASS  M3 L0 burst of +300: largest |x| in a report         measured=127  limit=0..127
PASS  M3 L0 burst of +300: carry left over                 measured=0  limit=0
PASS  M3 L0 burst of -300: sum sent                        measured=-300  limit=-300
PASS  M3 L0 random stream of 100000: in - (sent + carry)   measured=0  limit=0
PASS  M3 L0 random stream: largest |x| in a report         measured=127  limit=0..127
PASS  M3 L0 random stream after draining: in - sent        measured=0  limit=0
PASS  M3 L0 sim cycle: sum of REL_X                        measured=0  limit=0
PASS  M3 L0 sim cycle: sum of REL_Y                        measured=0  limit=0
PASS  M3 L0 sim cycle: sum of |REL_X|                      measured=1000  limit=1000
PASS  M3 L0 sim cycle: sum of |REL_Y|                      measured=1000  limit=1000
PASS  M3 L0 sim cycle: largest |x| or |y| in a report      measured=127  limit=0..127
PASS  M3 L2  evtest: REL_X total (square returns to start)      measured=0  limit=0
PASS  M3 L2  evtest: REL_Y total (square returns to start)      measured=0  limit=0
PASS  M3 L2  evtest: sum of |REL_X| (2 bursts + 2 sides)        measured=1000  limit=1000
PASS  M3 L2  evtest: sum of |REL_Y| (2 bursts + 2 sides)        measured=1000  limit=1000
PASS  M3 L2  evtest: largest |REL_X| or |REL_Y| in one event    measured=127  limit=100..127
SKIP  M3 L3  real optical sensor: ID and 10 cm ruler            measured=-  limit=needs you at the keyboard: run it in a terminal
```

## 2026-11-14 22:13:52  git=83029d6+dirty  ./bench/run.sh M4

M4: 28 pass, 0 fail, 1 skip -> NOT DONE (1 skipped)

```
PASS  M4 L0 accel sensitivity at +/-2 g [LSB/g]            measured=16384  limit=16384
PASS  M4 L0 accel sensitivity at +/-4 g [LSB/g]            measured=8192  limit=8192
PASS  M4 L0 accel sensitivity at +/-8 g [LSB/g]            measured=4096  limit=4096
PASS  M4 L0 accel sensitivity at +/-16 g [LSB/g]           measured=2048  limit=2048
PASS  M4 L0 gyro sensitivity at +/-250 dps [LSB/dps]       measured=131  limit=131
PASS  M4 L0 gyro sensitivity at +/-2000 dps [LSB/dps]      measured=16.4  limit=16.39..16.41
PASS  M4 L0 invalid range gives 0                          measured=0  limit=0
PASS  M4 L0 raw 16384 at +/-2 g [g]                        measured=1  limit=1
PASS  M4 L0 raw -16384 at +/-2 g [g]                       measured=-1  limit=-1
PASS  M4 L0 raw 8192 at +/-2 g [g]                         measured=0.5  limit=0.5
PASS  M4 L0 raw 32767 at +/-2 g [g]                        measured=1.99994  limit=1.999..2
PASS  M4 L0 raw 131 at +/-250 dps [dps]                    measured=1  limit=1
PASS  M4 L0 raw 32767 at +/-250 dps [dps]                  measured=250.13  limit=250..250.2
PASS  M4 L0 sim ax at t = 0.25 s [counts]                  measured=8192  limit=8191..8192
PASS  M4 L0 sim ay [counts]                                measured=0  limit=0
PASS  M4 L0 sim az [counts]                                measured=16384  limit=16384
PASS  M4 L0 sim gz at t = 0.25 s [counts]                  measured=11790  limit=11790
PASS  M4 L0 sim ax at t = 0.25 s [g]                       measured=0.5  limit=0.499..0.501
PASS  M4 L0 sim gz at t = 0.25 s [dps]                     measured=90  limit=90
PASS  M4 L0 sim ax at t = 0.75 s [counts]                  measured=-8192  limit=-8192..-8191
PASS  M4 L0 sim gz at t = 0.75 s [counts]                  measured=-11790  limit=-11790
PASS  M4 L0 sim ax at t = 3 s [counts]                     measured=0  limit=0
PASS  M4 L0 sim ax vs double model over 10 s: worst error [LSB] measured=0  limit=0..1
PASS  M4 L0 sim gz square wave over 10 s: sum (turns cancel) measured=0  limit=0
PASS  M4 L2  IMU text lines in 5 s (50 per second)              measured=250  limit=200..260
PASS  M4 L2  print rate from t_us [lines/s]                     measured=50.0  limit=49.5..50.5
PASS  M4 L2  ax vs sine model: worst error [LSB]                measured=0  limit=0..1
PASS  M4 L2  lines where ay, az, gx, gy or gz differ from the model measured=0  limit=0
SKIP  M4 L3  real IMU: ID, 6 faces, gyro bias, 90 degree turn   measured=-  limit=needs you at the keyboard: run it in a terminal
```

## 2026-11-14 22:17:46  git=035add3+dirty  ./bench/run.sh L2

L2: 35 pass, 3 fail, 1 skip -> FAILED

```
FAIL  M0 L2  hello lines received in 12 s                       measured=0  limit=10..13
FAIL  M0 L2  hello rate over 10 s [Hz]                          measured=none  limit=0.95..1.05
SKIP  M0 L2  LED toggles once a second                          measured=-  limit=needs your eyes: run it in a terminal
PASS  M4 L2  IMU samples in 5 s                                 measured=1001  limit=900..1010
PASS  M4 L2  ax vs sine model: worst error [LSB]                measured=0  limit=0..1
PASS  M4 L2  samples where ay, az, gx, gy or gz differ from the model measured=0  limit=0
PASS  M2 L2  evtest: REL_WHEEL scrolled up (ramp + half tilt)   measured=99  limit=98..100
PASS  M2 L2  evtest: REL_WHEEL scrolled down (negative tilt)    measured=-99  limit=-100..-98
PASS  M2 L2  evtest: REL_WHEEL total over the cycle             measured=0  limit=-1..1
PASS  M3 L2  evtest: REL_X total (square returns to start)      measured=0  limit=0
PASS  M3 L2  evtest: REL_Y total (square returns to start)      measured=0  limit=0
PASS  M3 L2  evtest: sum of |REL_X| (2 bursts + 2 sides)        measured=1000  limit=1000
PASS  M3 L2  evtest: sum of |REL_Y| (2 bursts + 2 sides)        measured=1000  limit=1000
PASS  M3 L2  evtest: largest |REL_X| or |REL_Y| in one event    measured=127  limit=100..127
PASS  M5 L2  frames received in 20 s                            measured=4001  limit=3900..4010
PASS  M5 L2  frame rate from t_us [Hz]                          measured=200.00  limit=198..202
PASS  M5 L2  t_us step jitter, worst |step - 5000| [us]         measured=0  limit=0..99
PASS  M5 L2  reader: CRC errors                                 measured=0  limit=0
PASS  M5 L2  reader: frames lost                                measured=0  limit=0
PASS  M5 L2  values vs sine model: worst error [LSB]            measured=0  limit=0..1
PASS  M5 L2  samples with a wrong ay, az, gx, gy or gz          measured=0  limit=0
FAIL  M5 L2  log packets (hello, info ...) in 20 s              measured=140  limit=30..50
PASS  M5 L2  reader open: BTN_LEFT presses in one cycle         measured=14  limit=14
PASS  M5 L2  reader open: sum of |REL_X|                        measured=1000  limit=1000
PASS  M5 L2  reader open: frames lost during the cycle          measured=0  limit=0
PASS  M1 L2  lsusb: HID interfaces                              measured=1  limit=1
PASS  M1 L2  lsusb: HID protocol (2 = mouse)                    measured=2  limit=2
PASS  M1 L2  lsusb: HID endpoint bInterval [ms]                 measured=1  limit=1
PASS  M1 L2  lsusb: CDC ACM interfaces                          measured=1  limit=1
PASS  M1 L2  hello lines received in 12 s                       measured=12  limit=10..13
PASS  M1 L2  hello rate over 10 s [Hz]                          measured=1.000  limit=0.95..1.05
PASS  M1 L2  evtest: BTN_LEFT presses in one cycle              measured=14  limit=14
PASS  M1 L2  evtest: BTN_LEFT releases in one cycle             measured=14  limit=14
PASS  M1 L2  evtest: BTN_RIGHT presses in one cycle             measured=14  limit=14
PASS  M1 L2  evtest: BTN_RIGHT releases in one cycle            measured=14  limit=14
PASS  M1 L2  evtest: BTN_MIDDLE presses in one cycle            measured=14  limit=14
PASS  M1 L2  evtest: BTN_MIDDLE releases in one cycle           measured=14  limit=14
PASS  M1 L2  1200-baud reboot: time to BOOTSEL (2e8a:0003) [s]  measured=0.44  limit=0..3
PASS  M1 L2  firmware restarts after BOOTSEL (1 = yes)          measured=1  limit=1
```

## 2026-11-14 22:18:24  git=035add3+dirty  ./bench/run.sh M0

M0: 2 pass, 0 fail, 1 skip -> NOT DONE (1 skipped)

```
PASS  M0 L2  hello lines received in 12 s                       measured=12  limit=10..13
PASS  M0 L2  hello rate over 10 s [Hz]                          measured=1.000  limit=0.95..1.05
SKIP  M0 L2  LED toggles once a second                          measured=-  limit=needs your eyes: run it in a terminal
```

## 2026-11-14 22:20:30  git=d4b9f69+dirty  ./bench/run.sh M6

M6: 6 pass, 0 fail, 1 skip -> NOT DONE (1 skipped)

```
PASS  M6 L5  soak: frames in 20 s                               measured=4001  limit=3900..4100
PASS  M6 L5  soak: frames lost                                  measured=0  limit=0
PASS  M6 L5  soak: CRC errors                                   measured=0  limit=0
PASS  M6 L5  after soak: BTN_LEFT presses in one cycle          measured=14  limit=14
PASS  M6 L5  after soak: sum of |REL_X|                         measured=1000  limit=1000
PASS  M6 L5  reflashes by 1200-baud reboot that succeeded (of 3) measured=3  limit=3
SKIP  M6 L5  5 unplug/replugs reconnect within 2 s              measured=-  limit=needs your hands: run it in a terminal
```

## 2026-11-14 22:31:42  git=d4b9f69+dirty  ./bench/run.sh M6

M6: 6 pass, 0 fail, 1 skip -> NOT DONE (1 skipped)

```
PASS  M6 L5  soak: frames in 600 s                              measured=120000  limit=119900..120100
PASS  M6 L5  soak: frames lost                                  measured=0  limit=0
PASS  M6 L5  soak: CRC errors                                   measured=0  limit=0
PASS  M6 L5  after soak: BTN_LEFT presses in one cycle          measured=14  limit=14
PASS  M6 L5  after soak: sum of |REL_X|                         measured=1000  limit=1000
PASS  M6 L5  reflashes by 1200-baud reboot that succeeded (of 3) measured=3  limit=3
SKIP  M6 L5  5 unplug/replugs reconnect within 2 s              measured=-  limit=needs your hands: run it in a terminal
```
