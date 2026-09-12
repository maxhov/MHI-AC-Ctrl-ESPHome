#include "esphome/core/log.h"
#include "mhi_spi_log_switch.h"

namespace esphome {
namespace mhi {

static const char *TAG = "mhi.switch";

void MhiSpiLogSwitch::setup() {
    // Always boot with logging off. It is a diagnostic, and a restored "on" would quietly
    // flood the log of a device nobody is debugging.
    this->parent_->set_spi_logging(false);
    this->publish_state(false);
}

void MhiSpiLogSwitch::write_state(bool state) {
    this->parent_->set_spi_logging(state);
    this->publish_state(state);
}

void MhiSpiLogSwitch::dump_config(){
    ESP_LOGCONFIG(TAG, "SPI frame logging switch");
}

} //namespace mhi
} //namespace esphome
