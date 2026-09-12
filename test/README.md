# Core tests

`MHI-AC-Ctrl-core.cpp` is the part of this project that talks SPI to the indoor unit, and it
is the part that is hardest to check by flashing and looking at Home Assistant. These tests
compile it for your development machine against a stub `Arduino.h` that simulates the bus:
the test scripts the bytes the AC clocks towards the controller and reads back the bytes the
controller clocked out.

No ESPHome or Xtensa toolchain is needed, just a C++17 compiler:

```sh
./test/run.sh
```

## What is covered

The service mailbox arbiter, which is where the interesting bugs have been: that a Silent
Mode write owns `DB6`/`DB9`/`DB10` for exactly its frame pair and never merges with the next
one, that an operating data request always spans a full pair however the polling switch is
toggled, that the mailbox goes idle when polling is off, and that a Silent write is confirmed
only by a reply to a read issued *after* it — replies already in flight describe the old
state and must not bounce the switch.

Also the record decoder, and the log's change filter in `mhi_frame_filter.h`.

The stub deliberately defines Arduino's `HEX`, `DEC`, `OCT` and `BIN` number-base macros, so
that a local identifier colliding with one of them fails here. This only helps for code the
suite actually compiles, which is why the filter lives in its own header rather than inside
`mhi_platform.cpp` — an earlier `HEX` collision compiled fine on the host and failed on the
firmware precisely because it was in a file these tests never built.

## What is not covered

- **The ESP8266 direct-GPIO bit-bang.** The stub has no `ESP8266` macro or `GPI`/`GPOS`/`GPOC`,
  so the host build always takes the `digitalRead`/`digitalWrite` branch. The register path is
  compiled by a firmware build but exercised by nothing here.
- **Timeouts.** The stub only advances `millis()` during the frame preamble, so
  `err_msg_timeout_SCK_low`, `err_msg_timeout_SCK_high` and `max_time_ms` are unreachable —
  as is the unbounded `while (!MHI_SCK_HIGH) {}` spin, which has no timeout of its own.
- **Frame size 33.** `run_frame` clocks 20-byte frames only, so the long-frame checksum,
  `DB16`/`DB17` and the `0xAA` signature are untested.
- **Everything in `mhi_platform.cpp` except the filter**: the rate limiter, the log lines and
  the ESPHome plumbing are checked by building the firmware, not by these tests.
