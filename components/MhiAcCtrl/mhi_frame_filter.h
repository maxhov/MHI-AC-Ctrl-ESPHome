#pragma once

// Frame comparison and formatting for the SPI logger. These are kept out of mhi_platform.cpp
// and free of ESPHome dependencies so that the host test suite can compile and exercise
// them — including the Arduino macro collisions that only show up once Arduino.h is in
// scope, which is how `HEX` slipped through once already.

#include "MHI-AC-Ctrl-core.h"

namespace esphome {
namespace mhi {

// Blank the fields that move on every frame by design, so that a genuine one-byte change
// stands out instead of drowning in noise: the MOSI signature toggle, the AC's own
// fast-jittering room temperature, the MISO frame-pair bit, and the checksums.
inline void normalise_frame(const byte* source, byte* target, byte frame_size, bool is_mosi) {
    memcpy(target, source, frame_size);
    if (is_mosi) {
        target[SB0] &= 0xfe;
        target[DB3] = 0;
    }
    else
        target[DB14] &= ~0x04;
    target[CBH] = 0;
    target[CBL] = 0;
    if (frame_size == 33)
        target[CBL2] = 0;
}

// Writes "A9 00 07 ..." into target, which must hold frame_size * 3 bytes.
inline void format_hex(const byte* frame, byte frame_size, char* target) {
    // Not named HEX: Arduino already defines that as the number base 16.
    static const char hex_digits[] = "0123456789ABCDEF";
    for (byte i = 0; i < frame_size; i++) {
        if (i != 0)
            *target++ = ' ';
        *target++ = hex_digits[frame[i] >> 4];
        *target++ = hex_digits[frame[i] & 0x0f];
    }
    *target = '\0';
}

} //namespace mhi
} //namespace esphome
