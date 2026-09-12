// Host-side tests for the SPI framing in MHI-AC-Ctrl-core.cpp, focused on the
// Silent Mode service command (see issue #166). Run with test/run.sh.

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "../components/MhiAcCtrl/MHI-AC-Ctrl-core.h"
#include "../components/MhiAcCtrl/mhi_frame_filter.h"

int SCK_PIN = 14;
int MOSI_PIN = 13;
int MISO_PIN = 12;

MhiFakeBus mhi_bus;

void mhi_bus_begin_frame(const uint8_t *mosi, uint8_t frame_size) {
  memcpy(mhi_bus.mosi, mosi, 33);
  memset(mhi_bus.miso, 0, sizeof(mhi_bus.miso));
  memset(mhi_bus.miso_driven, 0, sizeof(mhi_bus.miso_driven));
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
  uint8_t driven[33];
  int result;
};

static MisoFrame run_frame_raw(MosiFrame mosi, bool seal) {
  if (seal)
    mosi.seal();
  mhi_bus_begin_frame(mosi.bytes, 20);
  MisoFrame out;
  out.result = core.loop(100);
  memcpy(out.bytes, mhi_bus.miso, sizeof(out.bytes));
  memcpy(out.driven, mhi_bus.miso_driven, sizeof(out.driven));
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
      check(miso.driven[DB10] != 0, "DB10 was actually clocked out");
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

  // Each discriminator is checked on its own, so that deleting either half of the guard
  // fails a test rather than being masked by the other half.
  MosiFrame wrong_db10;
  wrong_db10.bytes[DB6] = 0xc0;
  wrong_db10.bytes[DB9] = 0xdd;
  wrong_db10.bytes[DB10] = 0x10;   // only DB10 is off-shape
  wrong_db10.bytes[DB11] = 0x20;
  wrong_db10.bytes[DB12] = 0x00;
  run_frame(wrong_db10);
  check_eq(capture.count(opdata_silent), 0, "a wrong DB10 alone is not Silent status");

  capture.clear();
  MosiFrame wrong_db12;
  wrong_db12.bytes[DB6] = 0xc0;
  wrong_db12.bytes[DB9] = 0xdd;
  wrong_db12.bytes[DB10] = 0x80;
  wrong_db12.bytes[DB11] = 0x20;
  wrong_db12.bytes[DB12] = 0x37;   // only DB12 is off-shape
  run_frame(wrong_db12);
  check_eq(capture.count(opdata_silent), 0, "a wrong DB12 alone is not Silent status");
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

static uint8_t rig_nonce = 0x30;
static MisoFrame quiet_frame() { return run_frame(idle_frame(rig_nonce += 37)); }

// Run frames until the controller puts a Silent Mode read on the mailbox.
static bool run_until_silent_read(int limit) {
  for (int i = 0; i < limit; i++) {
    MisoFrame m = quiet_frame();
    if (m.bytes[DB6] == 0xc0 && m.bytes[DB9] == 0xdd)
      return true;
  }
  return false;
}

static MosiFrame silent_record(uint8_t flags, uint8_t nonce) {
  MosiFrame f;
  f.bytes[DB9] = 0xdd;
  f.bytes[DB10] = 0x80;
  f.bytes[DB11] = flags;
  f.bytes[DB12] = 0x00;
  f.bytes[DB14] = nonce;
  return f;
}

static void test_a_write_is_confirmed_by_its_own_read() {
  printf("a Silent write is confirmed by a reply to a read issued after it\n");

  capture.clear();
  run_frame(silent_record(0x20, 0x01));
  check(capture.has(opdata_silent, 1), "baseline: Silent Mode reads as on");

  core.set_silent(false);

  // A reply that crossed with the command still describes the old state. Reporting it
  // would flip the switch back under the user.
  capture.clear();
  run_frame(silent_record(0x20, 0x02));
  check_eq(capture.count(opdata_silent), 0, "a reply already in flight does not bounce the switch");

  // The controller issues the confirmation read itself. A reply decoded while that request
  // is still going out was also already on its way, so it must not be accepted either.
  check(run_until_silent_read(40), "a confirmation read is issued after the write");
  capture.clear();
  run_frame(silent_record(0x20, 0x03));
  check_eq(capture.count(opdata_silent), 0, "a reply predating the request is not accepted");

  // Once the request has finished leaving the wire, the next reply is the answer to it and
  // is reported even though it is unchanged, which is what surfaces an ignored command.
  quiet_frame();
  quiet_frame();
  capture.clear();
  run_frame(silent_record(0x20, 0x04));
  check(capture.has(opdata_silent, 1), "the reply to that read is reported even though unchanged");
}

static void test_silent_is_still_confirmed_with_polling_off() {
  printf("Silent Mode is still read back when the operating data poller is off\n");
  core.set_opdata_polling(false);
  for (int i = 0; i < 40; i++) quiet_frame();          // let anything pending drain

  core.set_silent(true);
  check(run_until_silent_read(40), "a confirmation read goes out even with polling off");
  quiet_frame();
  quiet_frame();

  capture.clear();
  run_frame(silent_record(0x00, 0x11));
  check(capture.has(opdata_silent, 0), "the reply is reported, so the switch cannot latch");
  core.set_opdata_polling(true);
}

static void test_two_toggles_never_merge_on_the_wire() {
  printf("two quick toggles never merge into one run of the write selector\n");
  core.set_silent(true);
  int run_len = 0, longest = 0;
  for (int i = 0; i < 40; i++) {
    if (i == 2)
      core.set_silent(false);      // second toggle while the first is still in flight
    MisoFrame m = quiet_frame();
    if (m.bytes[DB9] == 0x21) {
      run_len++;
      if (run_len > longest) longest = run_len;
    } else {
      run_len = 0;
    }
  }
  check(longest > 0, "the write was sent at all");
  check(longest <= 2, "a Silent command never occupies more than its own frame pair");
}

// Width of the first operating data request window after the poller has been off for `gap`
// frames, having run `offset` frames of normal polling first.
static int request_window_width(int offset, int gap) {
  core.set_opdata_polling(true);
  for (int i = 0; i < offset; i++) quiet_frame();
  core.set_opdata_polling(false);
  for (int i = 0; i < gap; i++) quiet_frame();
  core.set_opdata_polling(true);

  int width = 0;
  bool started = false;
  for (int i = 0; i < 80; i++) {
    MisoFrame m = quiet_frame();
    bool request = (m.bytes[DB9] != 0xff && m.bytes[DB9] != 0x21);
    if (request) { started = true; width++; }
    else if (started) break;
  }
  return width;
}

static void test_requests_always_span_a_frame_pair() {
  printf("switching the poller off and on never truncates a request to one frame\n");
  int truncated = 0;
  for (int offset = 0; offset < 20; offset++)
    for (int gap = 1; gap <= 6; gap++)
      if (request_window_width(offset, gap) != 2)
        truncated++;
  check_eq(truncated, 0, "every request still spans the frame pair the protocol expects");
}

static void test_poller_collapse_leaves_the_mailbox_idle() {
  printf("with the poller off and nothing pending, the mailbox stays idle\n");
  core.set_opdata_polling(false);
  for (int i = 0; i < 40; i++) quiet_frame();          // let anything pending drain

  for (int i = 0; i < 80; i++) {
    MisoFrame m = quiet_frame();
    check_eq(m.bytes[DB6], 0x80, "DB6 idle");
    check_eq(m.bytes[DB9], 0xff, "DB9 idle");
    check_eq(m.bytes[DB10], 0xff, "DB10 idle");
  }
  core.set_opdata_polling(true);
}

static void test_all_flags_set_is_a_value_not_a_sentinel() {
  printf("the first reading after a reset is reported whatever its value\n");
  // Both passes matter: a design that cannot tell "no reading yet" from a real reading goes
  // silent for whichever value it borrowed as its sentinel.
  for (int pass = 0; pass < 2; pass++) {
    uint8_t flags = (pass == 0) ? 0x00 : 0xff;
    core.reset_old_values();
    capture.clear();
    run_frame(silent_record(flags, (uint8_t)(0x50 + pass)));
    check_eq(capture.count(opdata_silent), 1, "a reading is reported after a reset");
  }
}

static void test_unrelated_flags_do_not_re_announce_silent_mode() {
  printf("an unrelated DB11 flag does not re-announce the same Silent Mode state\n");
  core.reset_old_values();

  capture.clear();
  run_frame(silent_record(0x20, 0x60));
  check(capture.has(opdata_silent, 1), "the first reading is reported");

  capture.clear();
  run_frame(silent_record(0x21, 0x61));   // same Silent bit, some other flag moved
  check_eq(capture.count(opdata_silent), 0, "an unrelated flag is not a Silent Mode change");

  capture.clear();
  run_frame(silent_record(0x01, 0x62));   // the Silent bit itself clears
  check(capture.has(opdata_silent, 0), "the Silent Mode bit clearing is still reported");
}

static void test_repeated_toggles_do_not_starve_the_poller() {
  printf("repeated Silent toggles do not starve the operating data poller\n");
  std::set<int> selectors;
  for (int i = 0; i < 1600; i++) {
    if (i % 40 == 0)
      core.set_silent((i % 80) == 0);
    MisoFrame m = quiet_frame();
    if (m.bytes[DB9] != 0xff && m.bytes[DB9] != 0x21)
      selectors.insert((m.bytes[DB6] << 8) | m.bytes[DB9]);
  }
  check((int) selectors.size() >= 21, "every operating data selector is still requested");
}

static void test_frame_filter() {
  printf("the log's change filter ignores exactly the fields that move every frame\n");
  using esphome::mhi::normalise_frame;
  using esphome::mhi::format_hex;

  byte base[33];
  memset(base, 0, sizeof(base));
  base[SB0] = 0x6c; base[SB1] = 0x80; base[SB2] = 0x04;
  base[DB3] = 0x80; base[DB9] = 0xff; base[DB11] = 0x42;

  byte a[33], b[33];
  normalise_frame(base, a, 20, true);

  // Each of these moves on its own must compare equal.
  struct { size_t index; uint8_t value; const char *what; } noise[] = {
    {SB0, 0x6d, "the MOSI signature toggle"},
    {DB3, 0x81, "the AC's jittering room temperature"},
    {CBH, 0x99, "the checksum high byte"},
    {CBL, 0x77, "the checksum low byte"},
  };
  for (auto &n : noise) {
    byte variant[33];
    memcpy(variant, base, sizeof(variant));
    variant[n.index] = n.value;
    normalise_frame(variant, b, 20, true);
    check(memcmp(a, b, 20) == 0, std::string("ignored: ") + n.what);
  }

  // A real change must not be.
  byte real[33];
  memcpy(real, base, sizeof(real));
  real[DB11] = 0x43;
  normalise_frame(real, b, 20, true);
  check(memcmp(a, b, 20) != 0, "a genuine byte change is still seen");

  // On MISO it is the frame-pair bit that is noise, and DB3 is a real value we send.
  byte miso_base[33];
  memset(miso_base, 0, sizeof(miso_base));
  miso_base[DB3] = 0x9a;
  miso_base[DB14] = 0x04;
  normalise_frame(miso_base, a, 20, false);
  byte miso_toggled[33];
  memcpy(miso_toggled, miso_base, sizeof(miso_toggled));
  miso_toggled[DB14] = 0x00;
  normalise_frame(miso_toggled, b, 20, false);
  check(memcmp(a, b, 20) == 0, "ignored: the MISO frame-pair bit");
  miso_toggled[DB3] = 0x9b;
  normalise_frame(miso_toggled, b, 20, false);
  check(memcmp(a, b, 20) != 0, "the temperature we send is not treated as noise");

  char text[33 * 3];
  byte three[3] = {0xA9, 0x00, 0x0f};
  format_hex(three, 3, text);
  check(std::string(text) == "A9 00 0F", "hex formatting");
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
  test_a_write_is_confirmed_by_its_own_read();
  test_silent_is_still_confirmed_with_polling_off();
  test_two_toggles_never_merge_on_the_wire();
  test_requests_always_span_a_frame_pair();
  test_poller_collapse_leaves_the_mailbox_idle();
  test_all_flags_set_is_a_value_not_a_sentinel();
  test_unrelated_flags_do_not_re_announce_silent_mode();
  test_repeated_toggles_do_not_starve_the_poller();
  test_frame_filter();
  test_silent_status_is_polled();
  test_silent_write_still_works_with_polling_off();
  test_frame_observer();

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
