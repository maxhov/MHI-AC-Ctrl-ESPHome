// Minimal Arduino stub so MHI-AC-Ctrl-core.cpp can be compiled and driven on a
// development machine. It replaces the SPI pins with a simulated bus: the test
// scripts the MOSI bytes the indoor unit "sends" and reads back the MISO bytes
// the core clocked out. See test/README.md.
#pragma once

#include <stdint.h>
#include <string.h>

typedef uint8_t byte;
typedef bool boolean;
typedef unsigned int uint;

#define INPUT 0
#define OUTPUT 1
#define HIGH 1
#define LOW 0
#define PROGMEM

// Arduino claims these names as number bases for Serial.print(). They are defined here so
// that a collision with a local identifier fails on the host too, rather than only when
// somebody builds the firmware.
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#define highByte(w) ((uint8_t)((uint16_t)(w) >> 8))
#define lowByte(w) ((uint8_t)((uint16_t)(w) & 0xff))

// The opdata table is a byte[][2] read as a little-endian word, exactly as the
// ESP8266/ESP32 builds do.
inline uint16_t pgm_read_word(const void *addr) {
  uint16_t value;
  memcpy(&value, addr, sizeof(value));
  return value;
}

// ---------------------------------------------------------------- simulated bus

extern int SCK_PIN;
extern int MOSI_PIN;
extern int MISO_PIN;

struct MhiFakeBus {
  uint8_t mosi[33];       // bytes the AC clocks towards the controller
  uint8_t miso[33];       // bytes the controller clocked back, captured here
  uint8_t miso_driven[33];// which of those bytes were actually clocked out at all, so that
                          // a byte nobody wrote is distinguishable from one written as 0x00
  uint8_t frame_size;
  uint16_t bit_pos;       // position within the frame, LSB first
  unsigned long millis;   // virtual clock, only advances while SCK idles high
  unsigned long preamble_until;
  bool sck_level;
};

extern MhiFakeBus mhi_bus;

// Arm the bus for one MHI_AC_Ctrl_Core::loop() call.
void mhi_bus_begin_frame(const uint8_t *mosi, uint8_t frame_size);

inline unsigned long millis() { return mhi_bus.millis; }
inline void pinMode(int, int) {}

inline int digitalRead(int pin) {
  if (pin == SCK_PIN) {
    // Hold SCK high long enough for the core to detect a frame start, then
    // clock one bit per falling/rising pair for the rest of the frame.
    if (mhi_bus.millis < mhi_bus.preamble_until) {
      mhi_bus.millis++;
      return HIGH;
    }
    mhi_bus.sck_level = !mhi_bus.sck_level;
    return mhi_bus.sck_level ? HIGH : LOW;
  }
  if (pin == MOSI_PIN) {
    const uint16_t pos = mhi_bus.bit_pos++;
    return (mhi_bus.mosi[pos / 8] >> (pos % 8)) & 1;
  }
  return LOW;
}

inline void digitalWrite(int pin, int value) {
  if (pin != MISO_PIN)
    return;
  mhi_bus.miso_driven[mhi_bus.bit_pos / 8] = 1;
  if (value)
    mhi_bus.miso[mhi_bus.bit_pos / 8] |= (uint8_t)(1u << (mhi_bus.bit_pos % 8));
}
