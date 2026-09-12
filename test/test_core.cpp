// Host-side tests for the SPI framing in MHI-AC-Ctrl-core.cpp, focused on the
// Silent Mode service command (see issue #166). Run with test/run.sh.

#include <cstdio>
#include <cstring>
#include <set>
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

struct FrameEvent {
  uint8_t mosi[33];
  uint8_t miso[33];
  int frame_size;
  int status;
};

class FrameCapture : public CallbackInterface_Frame {
 public:
  std::vector<FrameEvent> events;
  void cbiFrameFunction(const byte *mosi, const byte *miso, byte frame_size, int status) override {
    FrameEvent e;
    memcpy(e.mosi, mosi, 33);
    memcpy(e.miso, miso, 33);
    e.frame_size = frame_size;
    e.status = status;
    this->events.push_back(e);
  }
  void clear() { this->events.clear(); }
};

static FrameCapture frames;
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

static MisoFrame run_frame_raw(MosiFrame mosi, bool seal) {
  if (seal)
    mosi.seal();
  mhi_bus_begin_frame(mosi.bytes, 20);
  MisoFrame out;
  out.result = core.loop(100);
  memcpy(out.bytes, mhi_bus.miso, sizeof(out.bytes));
  return out;
}

static MisoFrame run_frame(MosiFrame mosi) { return run_frame_raw(mosi, true); }

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

static void test_confirmation_waits_for_a_fresh_read() {
  printf("a Silent write is confirmed by a fresh read, not by a reply already in flight\n");

  MosiFrame on;
  on.bytes[DB9] = 0xdd;
  on.bytes[DB10] = 0x80;
  on.bytes[DB11] = 0x20;
  on.bytes[DB12] = 0x00;

  capture.clear();
  run_frame(on);
  check(capture.has(opdata_silent, 1), "baseline: Silent Mode reads as on");

  core.set_silent(false);

  // A reply that crossed with the command still describes the old state. Reporting it would
  // flip the switch back under the user a moment after they moved it.
  capture.clear();
  MosiFrame stale = on;
  stale.bytes[DB14] = 0x11;
  run_frame(stale);
  check_eq(capture.count(opdata_silent), 0, "a reply already in flight does not bounce the switch");

  // Once the poller asks again the answer is authoritative, and is reported even when it is
  // unchanged, which is what surfaces a command the AC ignored.
  bool asked = false;
  for (int i = 0; i < 900 && !asked; i++) {
    MisoFrame miso = run_frame(idle_frame((uint8_t)(0x30 + (uint8_t)i)));
    if (miso.bytes[DB6] == 0xc0 && miso.bytes[DB9] == 0xdd)
      asked = true;
  }
  check(asked, "the poller re-reads Silent Mode after a write");

  capture.clear();
  MosiFrame again = on;
  again.bytes[DB14] = 0x22;
  run_frame(again);
  check(capture.has(opdata_silent, 1), "the fresh reading is reported even though unchanged");
}

static void test_all_flags_set_is_a_value_not_a_sentinel() {
  printf("a DB11 of 0xff is reported rather than mistaken for 'nothing seen yet'\n");
  core.reset_old_values();
  capture.clear();

  MosiFrame rec;
  rec.bytes[DB9] = 0xdd;
  rec.bytes[DB10] = 0x80;
  rec.bytes[DB11] = 0xff;   // every flag set, which used to collide with the sentinel
  rec.bytes[DB12] = 0x00;
  run_frame(rec);
  check(capture.has(opdata_silent, 1), "0xff is reported as a real reading");
}

static void test_repeated_toggles_do_not_starve_the_poller() {
  printf("repeated Silent toggles do not starve the operating data poller\n");
  std::set<int> selectors;
  for (int i = 0; i < 1200; i++) {
    if (i % 40 == 0)
      core.set_silent((i % 80) == 0);
    MisoFrame miso = run_frame(idle_frame((uint8_t) i));
    if (miso.bytes[DB9] != 0xff && miso.bytes[DB9] != 0x21)
      selectors.insert((miso.bytes[DB6] << 8) | miso.bytes[DB9]);
  }
  check((int) selectors.size() >= 21, "every operating data selector is still requested");
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

static void test_poller_can_be_collapsed() {
  printf("the operating data poller can be silenced for frame analysis\n");

  core.set_opdata_polling(false);
  for (int i = 0; i < 80; i++) {
    MisoFrame miso = run_frame(idle_frame((uint8_t)(0xa0 + (uint8_t)i)));
    check_eq(miso.bytes[DB6], 0x80, "DB6 stays idle while polling is off");
    check_eq(miso.bytes[DB9], 0xff, "DB9 stays idle while polling is off");
    check_eq(miso.bytes[DB10], 0xff, "DB10 stays idle while polling is off");
  }

  core.set_opdata_polling(true);
  bool resumed = false;
  for (int i = 0; i < 80 && !resumed; i++) {
    MisoFrame miso = run_frame(idle_frame((uint8_t)(0xe0 + (uint8_t)i)));
    if (miso.bytes[DB9] != 0xff)
      resumed = true;
  }
  check(resumed, "polling resumes when switched back on");
}

static void test_silent_write_still_works_with_polling_off() {
  printf("a Silent command still goes out while the poller is collapsed\n");
  core.set_opdata_polling(false);
  core.set_silent(true);
  int sent = 0;
  for (int i = 0; i < 6; i++) {
    MisoFrame miso = run_frame(idle_frame((uint8_t)(0x10 + (uint8_t)i)));
    if (miso.bytes[DB9] == 0x21) {
      sent++;
      check_eq(miso.bytes[DB10], 0x01, "Silent ON still carried");
    }
  }
  check_eq(sent, 2, "command still sent on one frame pair");
  core.set_opdata_polling(true);
}

static void test_frame_observer() {
  printf("every completed frame reaches the observer, valid or not\n");

  frames.clear();
  run_frame(idle_frame(0x05));
  check_eq((int) frames.events.size(), 1, "one callback per frame");
  if (!frames.events.empty()) {
    check_eq(frames.events[0].status, err_msg_valid_frame, "a good frame reports as valid");
    check_eq(frames.events[0].frame_size, 20, "frame size is reported");
    check_eq(frames.events[0].mosi[SB1], 0x80, "the MOSI frame is handed over");
    check_eq(frames.events[0].miso[SB0], 0xA9, "the MISO frame is handed over");
  }

  // A frame whose checksum was never written must still be observable, and must still
  // be rejected by loop().
  frames.clear();
  MosiFrame broken = idle_frame(0x06);
  MisoFrame out = run_frame_raw(broken, false);
  check_eq((int) frames.events.size(), 1, "a rejected frame is still handed over");
  if (!frames.events.empty())
    check_eq(frames.events[0].status, err_msg_invalid_checksum, "the rejection reason is reported");
  check_eq(out.result, err_msg_invalid_checksum, "loop() still rejects it");
}

int main() {
  core.MHIAcCtrlStatus(&capture);
  core.MHIAcCtrlFrame(&frames);
  core.init();
  core.set_frame_size(20);

  test_idle_frames_leave_the_mailbox_alone();
  test_silent_on_writes_the_service_command();
  test_mailbox_is_released_after_the_command();
  test_silent_off_writes_zero();
  test_status_record_is_decoded();
  test_status_record_is_not_confused_with_other_records();
  test_unsolicited_status_record_is_decoded();
  test_confirmation_waits_for_a_fresh_read();
  test_all_flags_set_is_a_value_not_a_sentinel();
  test_repeated_toggles_do_not_starve_the_poller();
  test_silent_status_is_polled();
  test_poller_can_be_collapsed();
  test_silent_write_still_works_with_polling_off();
  test_frame_observer();

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
