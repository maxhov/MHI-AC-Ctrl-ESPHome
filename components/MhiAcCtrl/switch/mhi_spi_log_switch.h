#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../mhi_platform.h"

namespace esphome {
namespace mhi {

// Dumps changed SPI frames to the log, for working out what undocumented bytes mean.
class MhiSpiLogSwitch :
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
