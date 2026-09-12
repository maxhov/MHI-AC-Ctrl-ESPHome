#include "esphome/core/log.h"
#include "mhi_opdata_polling_switch.h"

namespace esphome {
namespace mhi {

static const char *TAG = "mhi.switch";

void MhiOpdataPollingSwitch::setup() {
    // Always boot polling. A restored "off" would leave every operating data sensor blank
    // with nothing on the device to explain why.
    this->parent_->set_opdata_polling(true);
    this->publish_state(true);
}

void MhiOpdataPollingSwitch::write_state(bool state) {
    this->parent_->set_opdata_polling(state);
    this->publish_state(state);
}

void MhiOpdataPollingSwitch::dump_config(){
    ESP_LOGCONFIG(TAG, "Operating data polling switch");
}

} //namespace mhi
} //namespace esphome
