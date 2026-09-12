#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../mhi_platform.h"

namespace esphome {
namespace mhi {

// Turning this off stops the operating data requests, which leaves the service mailbox
// bytes still so that a real change stands out in a frame capture. Every operating data
// sensor stops updating while it is off.
class MhiOpdataPollingSwitch :
    public switch_::Switch,
    public Component,
    public Parented<MhiPlatform> {
public:
  void write_state(bool state);

protected:
    void setup() override;
    void dump_config() override;
};

} //namespace mhi
} //namespace esphome
