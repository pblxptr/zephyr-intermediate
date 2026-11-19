# Zephyr Training Environment

Welcome to the Zephyr RTOS training! This repository includes a ready-to-use
development environment based on Zephyr **4.4.0**, which you can set up in one of
three ways:

---

## Manual Zephyr Setup

Follow the following guide:
- [Getting Started Guide](https://docs.zephyrproject.org/4.4.0/develop/getting_started/index.html#).

Make sure to select appropriate OS and to perform all steps till
[Build the Blinky Sample](https://docs.zephyrproject.org/4.4.0/develop/getting_started/index.html#build-the-blinky-sample).

## L6: latency investigation

Control should start handling an event within 10 ms of the timer releasing it.
The measurement starts at `ready_ms`, just before `k_msgq_put()`, and ends when
`k_msgq_get()` returns in control. The trace markers are `event_release` and
`control_start`. This is the delay before processing starts, not the time spent
processing the event. Each delay above 10 ms increments the miss counter;
rate limiting only affects the warnings.

Both runs used `native_sim/native/64` with Zephyr 4.4.0. The timer releases an
event every 250 ms, starting at 500 ms. Maintenance busy-waits for 45 ms per
event. The workload and detector were the same in both runs; only control's
priority changed. Sequences 0–9 gave these results:

| Control priority | Maintenance priority | Logged latency | Misses |
| ---: | ---: | ---: | ---: |
| 7 | 4 | 40 ms | 10/10 |
| 3 | 4 | 0 ms | 0/10 |

The problem was the priority order. Maintenance at priority 4 ran before control
at priority 7, even though control had the deadline. Changing control to 3 lets
it handle the event first. Maintenance still does the same work afterwards.

The [original trace for sequence 0](evidence/l6/before/sequence-0.txt) shows
control becoming ready at 0.500 s. Maintenance runs from 0.500 to 0.545 s while
control waits. Control then runs at 0.545 s. In the
[fixed run](evidence/l6/after/sequence-0.txt), control starts at 0.500 s, before
maintenance. These excerpts were decoded from CTF with Babeltrace 2.0.5.

The logged 40 ms is due to the 10 ms clock ticks used by `k_uptime_get_32()`;
the trace shows the full 45 ms delay. The same rounding means this detector can
miss small overruns between 10 and 20 ms. CTF confirms the timings for these
runs, but testing on the target board is still needed.

`kernel thread list` was run before the first timer event. Control was pending
on the message queue, at priority 7 before the fix and 3 afterwards. Maintenance
was pending on its semaphore at priority 4 in both runs. The shell snapshots
show the threads between events; the trace shows what happens during the delay.

The [original console output](evidence/l6/before/console.txt) and
[fixed console output](evidence/l6/after/console.txt) contain the thread lists,
latencies and miss counts. Some log lines repeat because the shell redraws them.
Both directories also contain the raw CTF files and full decoded output.

To repeat the runs, check out this submission and run from the west workspace
root with the Python environment active:

```sh
export ZEPHYR_BASE="$PWD/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=host
west build -b native_sim/native/64 -d /tmp/l6-before zephyr-intermediate/app -- -DEXTRA_CFLAGS=-DCONTROL_PRIORITY=7
west build -b native_sim/native/64 -d /tmp/l6-after zephyr-intermediate/app
cd zephyr-intermediate
python3 evidence/l6/capture.py /tmp/l6-before/zephyr/zephyr.exe /tmp/l6-capture-before "$ZEPHYR_BASE/subsys/tracing/ctf/tsdl/metadata"
python3 evidence/l6/capture.py /tmp/l6-after/zephyr/zephyr.exe /tmp/l6-capture-after "$ZEPHYR_BASE/subsys/tracing/ctf/tsdl/metadata"
babeltrace2 --clock-seconds /tmp/l6-capture-before/ctf
```

The script sends `kernel thread list` through the simulator's stdin, captures
2.9 seconds of output and decodes the trace. Both builds passed, and all ten
sequences, miss counts and the scheduling order were checked.
