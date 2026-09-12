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

The current tests cover the Silent Mode service command (issue #166): that a pending change
claims the service mailbox for one frame pair and then releases it, that the operating data
poller is not disturbed, and that the status record is decoded both when it answers our read
request and when the AC sends it unsolicited after the remote is used. They also cover the
analysis switches: that collapsing the operating data poller really does leave the mailbox
idle without blocking a Silent command, and that every completed frame reaches the observer,
including the ones rejected for a bad checksum.

The stub deliberately defines Arduino's `HEX`, `DEC`, `OCT` and `BIN` number-base macros, so
that a local identifier colliding with one of them fails here rather than only when somebody
builds the firmware.
