// Version 4.0
#pragma once

#include "MHI-AC-Ctrl-core.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/time.h"
#include <vector>
#include <string>
#include "mhi_status_listener.h"

namespace esphome {
namespace mhi {

class MhiPlatform : 
    public Component, 
    public CallbackInterface_Status,
    public CallbackInterface_Frame {

public:

    void setup() override;
    void set_frame_size(int framesize);
    void set_room_temp_api_timeout(int time_in_seconds);
    void loop() override;
    void dump_config() override;
    void cbiStatusFunction(ACStatus status, int value) override;
    void cbiFrameFunction(const byte* mosi_frame, const byte* miso_frame, byte frame_size, int status) override;

    void set_room_temperature(float value);

    bool get_room_temp_api_active();
    float get_room_temp_offset();
    
    void set_power(ACPower value);
    void set_fan(int value);
    void set_mode(ACMode value);
    void set_tsetpoint(float value);
    void set_offset(float value);
    void set_vanes(int value);
    void set_vanesLR(int value);
    void set_3Dauto(bool value);
    void set_silent(bool value);
    void set_spi_logging(bool value);
    void set_opdata_polling(bool value);
    void set_external_room_temperature_sensor(sensor::Sensor* sensor);
    void set_sck_pin(int pin) { this->sck_pin_ = pin; };
    void set_mosi_pin(int pin) { this->mosi_pin_ = pin; };
    void set_miso_pin(int pin) { this->miso_pin_ = pin; };
    void add_listener(MhiStatusListener* listener);


private:
    void transfer_room_temperature(float value);
    bool spi_logging_ = false;
    bool have_previous_frame_ = false;
    uint32_t frame_sequence_ = 0;
    uint32_t frame_errors_ = 0;
    unsigned long last_error_report_ = 0;
    uint32_t rejected_since_log_ = 0;
    uint32_t last_reject_log_frame_ = 0;
    byte previous_mosi_[33] = {0};
    byte previous_miso_[33] = {0};
    float last_room_temperature_ = NAN; 
    float temperature_offset_ = 0.0f;

    int frame_size_;
    unsigned long room_temp_api_timeout_start_ = millis();
    unsigned long room_temp_api_timeout_;
    bool room_temp_api_active_ = false;
    int sck_pin_ = -1;
    int mosi_pin_ = -1;
    int miso_pin_ = -1;

    MHI_AC_Ctrl_Core mhi_ac_ctrl_core_;

    sensor::Sensor* external_temperature_sensor_{nullptr};

    std::vector<MhiStatusListener*> listeners_;
};



} //namespace mhi
} //namespace esphome
