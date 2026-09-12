#include "esphome/core/log.h"
#include "mhi_silent_switch.h"

namespace esphome {
namespace mhi {

static const char *TAG = "mhi.switch";

void MhiSilentSwitch::setup() {
    this->parent_->add_listener(this);
}

void MhiSilentSwitch::write_state(bool state) {
    this->parent_->set_silent(state);
    // The AC only reports Silent Mode when polled, so show the request immediately rather
    // than leaving the switch on its old position for a poll cycle.
    this->publish_state(state);
    ESP_LOGD(TAG, "silent mode state written %d", state);
}

void MhiSilentSwitch::dump_config(){
    ESP_LOGCONFIG(TAG, "Silent mode switch");
    ESP_LOGCONFIG(TAG, "  state: %d", this->state);
}

void MhiSilentSwitch::update_status(ACStatus status, int value) {

    if (status == opdata_silent) {
        this->publish_state(value != 0);
        ESP_LOGD(TAG, "silent mode status updated: %s", value != 0 ? "enabled" : "disabled");
    }
}

} //namespace mhi
} //namespace esphome
