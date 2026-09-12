// Host-side tests for the SPI framing in MHI-AC-Ctrl-core.cpp, focused on the
// Silent Mode service command (see issue #166). Run with test/run.sh.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../components/MhiAcCtrl/MHI-AC-Ctrl-core.h"

int SCK_PIN = 14;
int MOSI_PIN = 13;
int MISO_PIN = 12;

MhiFakeBus mhi_bus;

void mhi_bus_begin_frame(const uint8_t *mosi, uint8_t frame_size) {
  memcpy(mhi_bus.mosi, mosi, 33);
  memset(mhi_bus.miso, 0, sizeof(mhi_bus.miso));
  mhi_bus.frame_size = frame_size;
  mhi_bus.bit_pos = 0;
  mhi_bus.preamble_until = mhi_bus.millis + 5;
  mhi_bus.sck_level = true;  // first read in the bit loop yields the falling edge
}

// ------------------------------------------------------------------ test rig

static int failures = 0;
static int checks = 0;

static void check(bool ok, const std::string &what) {
  checks++;
  if (!ok) {
    failures++;
    printf("  FAIL: %s\n", what.c_str());
  }
}

static void check_eq(int got, int want, const std::string &what) {
  checks++;
  if (got != want) {
    failures++;
    printf("  FAIL: %s (got 0x%02x, want 0x%02x)\n", what.c_str(), got, want);
  }
}

struct StatusEvent {
  ACStatus status;
  int value;
};

class StatusCapture : public CallbackInterface_Status {
 public:
  std::vector<StatusEvent> events;
  void cbiStatusFunction(ACStatus status, int value) override {
    this->events.push_back({status, value});
  }
  bool has(ACStatus status, int value) const {
    for (const auto &e : this->events)
      if (e.status == status && e.value == value)
        return true;
    return false;
  }
  int count(ACStatus status) const {
    int n = 0;
    for (const auto &e : this->events)
      if (e.status == status)
        n++;
    return n;
  }
  void clear() { this->events.clear(); }
};

static StatusCapture capture;
static MHI_AC_Ctrl_Core core;

// A signature-correct, checksum-correct frame from the indoor unit.
struct MosiFrame {
  uint8_t bytes[33];

  MosiFrame() {
    memset(this->bytes, 0, sizeof(this->bytes));
    this->bytes[SB0] = 0x6c;
    this->bytes[SB1] = 0x80;
    this->bytes[SB2] = 0x04;
    this->bytes[DB3] = 0x80;   // some plausible room temperature
    this->bytes[DB9] = 0xff;   // "no operating data in this frame"
    this->bytes[DB10] = 0xff;
  }

  void seal() {
    uint16_t checksum = 0;
    for (int i = 0; i < CBH; i++)
      checksum += this->bytes[i];
    this->bytes[CBH] = (uint8_t)(checksum >> 8);
    this->bytes[CBL] = (uint8_t)(checksum & 0xff);
  }
};

// Clock one frame through the core and return what it put on MISO.
struct MisoFrame {
  uint8_t bytes[33];
  int result;
};

static MisoFrame run_frame(MosiFrame mosi) {
  mosi.seal();
  mhi_bus_begin_frame(mosi.bytes, 20);
  MisoFrame out;
  out.result = core.loop(100);
  memcpy(out.bytes, mhi_bus.miso, sizeof(out.bytes));
  return out;
}

// Frames are only parsed when something changed, so nudge a byte the core does
// not interpret when a test needs two look-alike frames back to back.
static MosiFrame idle_frame(uint8_t nonce) {
  MosiFrame f;
  f.bytes[DB14] = nonce;
  return f;
}

// ------------------------------------------------------------------- the tests

static void test_idle_frames_leave_the_mailbox_alone() {
  printf("idle frames keep DB10 idle\n");
  for (uint8_t i = 0; i < 8; i++) {
    MisoFrame miso = run_frame(idle_frame(i));
    check_eq(miso.bytes[DB10], 0xff, "DB10 stays 0xff while no Silent write is pending");
  }
}

static void test_silent_on_writes_the_service_command() {
  printf("set_silent(true) emits the service write command\n");
  core.set_silent(true);

  int frames_with_command = 0;
  int first_seen = -1;
  for (int i = 0; i < 6; i++) {
    MisoFrame miso = run_frame(idle_frame(0x20 + i));
    if (miso.bytes[DB9] == 0x21) {
      if (first_seen < 0)
        first_seen = i;
      frames_with_command++;
      check_eq(miso.bytes[DB6], 0x80, "DB6 is the service write group");
      check_eq(miso.bytes[DB10], 0x01, "DB10 carries Silent ON");
    }
  }
  check_eq(frames_with_command, 2, "command is sent on exactly one frame pair");
  check(first_seen >= 0 && first_seen <= 1, "command starts within one frame pair");
}

static void test_mailbox_is_released_after_the_command() {
  printf("mailbox returns to the opdata poller afterwards\n");
  bool saw_stuck_command = false;
  bool saw_opdata_request = false;
  for (int i = 0; i < 60; i++) {
    MisoFrame miso = run_frame(idle_frame(0x40 + (uint8_t)i));
    if (miso.bytes[DB9] == 0x21)
      saw_stuck_command = true;
    if (miso.bytes[DB9] != 0x21 && miso.bytes[DB9] != 0xff)
      saw_opdata_request = true;
    if (miso.bytes[DB9] != 0x21)
      check_eq(miso.bytes[DB10], 0xff, "DB10 is released back to 0xff");
  }
  check(!saw_stuck_command, "Silent command is not repeated forever");
  check(saw_opdata_request, "operating data polling resumes");
}

static void test_silent_off_writes_zero() {
  printf("set_silent(false) emits Silent OFF\n");
  core.set_silent(false);
  bool seen = false;
  for (int i = 0; i < 6; i++) {
    MisoFrame miso = run_frame(idle_frame(0x60 + (uint8_t)i));
    if (miso.bytes[DB9] == 0x21) {
      seen = true;
      check_eq(miso.bytes[DB10], 0x00, "DB10 carries Silent OFF");
    }
  }
  check(seen, "Silent OFF command was sent");
}

static void test_status_record_is_decoded() {
  printf("the C0/DD status record is decoded\n");
  capture.clear();

  MosiFrame on;
  on.bytes[DB6] = 0xc0;
  on.bytes[DB9] = 0xdd;
  on.bytes[DB10] = 0x80;
  on.bytes[DB11] = 0x20;  // bit 5 set => Silent Mode active
  on.bytes[DB12] = 0x00;
  run_frame(on);
  check(capture.has(opdata_silent, 1), "Silent ON is reported");

  capture.clear();
  MosiFrame off = on;
  off.bytes[DB11] = 0x00;
  run_frame(off);
  check(capture.has(opdata_silent, 0), "Silent OFF is reported");
}

static void test_status_record_is_not_confused_with_other_records() {
  printf("look-alike records are ignored\n");
  capture.clear();

  // Same selector, but not the Silent status record shape.
  MosiFrame other;
  other.bytes[DB6] = 0xc0;
  other.bytes[DB9] = 0xdd;
  other.bytes[DB10] = 0x10;
  other.bytes[DB11] = 0x20;
  other.bytes[DB12] = 0x37;
  run_frame(other);
  check_eq(capture.count(opdata_silent), 0, "DB10/DB12 mismatch is not treated as Silent status");
}

static void test_unsolicited_status_record_is_decoded() {
  printf("unsolicited status records are decoded on either frame phase\n");
  // Drive both phases of a frame pair with the record; the remote can change
  // Silent Mode at any time, so neither phase may be ignored.
  for (int phase = 0; phase < 2; phase++) {
    capture.clear();
    // One extra idle frame on the second pass, so the record lands on the other
    // half of the frame pair.
    for (int i = 0; i <= phase; i++)
      run_frame(idle_frame((uint8_t)(0x80 + phase * 8 + i)));

    MosiFrame rec;
    rec.bytes[DB9] = 0xdd;
    rec.bytes[DB10] = 0x80;
    rec.bytes[DB11] = (phase == 0) ? 0x20 : 0x00;
    rec.bytes[DB12] = 0x00;
    run_frame(rec);
    check(capture.count(opdata_silent) == 1,
          "unsolicited record reported on frame phase " + std::to_string(phase));
  }
}

static void test_state_is_resynced_after_a_command() {
  printf("the state is reported again after a command, even when unchanged\n");
  MosiFrame rec;
  rec.bytes[DB9] = 0xdd;
  rec.bytes[DB10] = 0x80;
  rec.bytes[DB11] = 0x20;
  rec.bytes[DB12] = 0x00;

  capture.clear();
  run_frame(rec);
  check(capture.has(opdata_silent, 1), "baseline state is reported");

  // The AC ignores the command and keeps reporting the same state.
  core.set_silent(false);
  for (int i = 0; i < 4; i++)
    run_frame(idle_frame((uint8_t)(0xc0 + i)));

  capture.clear();
  MosiFrame same = rec;
  same.bytes[DB14] = 0x01;  // a different frame, same Silent Mode state
  run_frame(same);
  check(capture.has(opdata_silent, 1), "state is re-reported so the switch can correct itself");
}

static void test_silent_status_is_polled() {
  printf("Silent Mode state is polled in the operating data cycle\n");
  bool requested = false;
  for (int i = 0; i < 900 && !requested; i++) {
    MisoFrame miso = run_frame(idle_frame((uint8_t)i));
    if (miso.bytes[DB6] == 0xc0 && miso.bytes[DB9] == 0xdd)
      requested = true;
  }
  check(requested, "a C0/DD read request is issued during an opdata cycle");
}

int main() {
  core.MHIAcCtrlStatus(&capture);
  core.init();
  core.set_frame_size(20);

  test_idle_frames_leave_the_mailbox_alone();
  test_silent_on_writes_the_service_command();
  test_mailbox_is_released_after_the_command();
  test_silent_off_writes_zero();
  test_status_record_is_decoded();
  test_status_record_is_not_confused_with_other_records();
  test_unsolicited_status_record_is_decoded();
  test_state_is_resynced_after_a_command();
  test_silent_status_is_polled();

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
