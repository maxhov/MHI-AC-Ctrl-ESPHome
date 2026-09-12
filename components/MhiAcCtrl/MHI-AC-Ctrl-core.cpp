// MHI-AC-Ctrol-core
// implements the core functions (read & write SPI)

#include "MHI-AC-Ctrl-core.h"

uint16_t calc_checksum(byte* frame) {
  uint16_t checksum = 0;
  for (int i = 0; i < CBH; i++)
    checksum += frame[i];
  return checksum;
}

uint16_t calc_checksumFrame33(byte* frame) {
  uint16_t checksum = 0;
  for (int i = 0; i < CBL2; i++)
    checksum += frame[i];
  return checksum;
}

void MHI_AC_Ctrl_Core::reset_old_values() {  // used e.g. when MQTT connection to broker is lost, to re-output data
  // old status
  status_power_old = 0xff;
  status_mode_old = 0xff;
  status_fan_old = 0xff;
  status_vanes_old = 0xff;
  status_troom_old = 0xfe;
  status_tsetpoint_old = 0x00;
  status_errorcode_old = 0xff;
  status_compressor_old = 0xff;
  status_heating_old = 0xff;
  status_vanesLR_old = 0xff;
  status_3Dauto_old = 0xff;

  // old operating data
  op_kwh_old = 0xffff;
  op_mode_old = 0xff;
  op_settemp_old = 0xff;
  op_return_air_old = 0xff;
  op_iu_fanspeed_old = 0xff;
  op_thi_r1_old = 0x00;
  op_thi_r2_old = 0x00;
  op_thi_r3_old = 0x00;
  op_total_iu_run_old = 0;
  op_outdoor_old = 0xff;
  op_tho_r1_old = 0x00;
  op_total_comp_run_old = 0;
  op_ct_old = 0xff;
  op_tdsh_old = 0xff;
  op_protection_no_old = 0xff;
  op_ou_fanspeed_old = 0xff;
  op_defrost_old = 0x00;
  op_silent_known = false;
  op_comp_old = 0xffff;
  op_td_old  = 0x00;
  op_ou_eev1_old = 0xffff;
}

void MHI_AC_Ctrl_Core::init() {
  //MeasureFrequency(m_cbiStatus);
  pinMode(SCK_PIN, INPUT);
  pinMode(MOSI_PIN, INPUT);
  pinMode(MISO_PIN, OUTPUT);
  MHI_AC_Ctrl_Core::reset_old_values();
  // Read Silent Mode once at startup, so the switch does not assert a state it has never
  // been told for the length of a whole operating data cycle.
  silent_phase = silent_confirm_due;
}

void MHI_AC_Ctrl_Core::set_power(boolean power) {
  new_Power = 0b10 | power;
}

void MHI_AC_Ctrl_Core::set_mode(ACMode mode) {
  new_Mode = 0b00100000 | mode;
}

void MHI_AC_Ctrl_Core::set_tsetpoint(uint tsetpoint) {
  new_Tsetpoint = 0b10000000 | tsetpoint;
}

void MHI_AC_Ctrl_Core::set_fan(uint fan) {
  new_Fan = 0b00001000 | fan;
}

void MHI_AC_Ctrl_Core::set_3Dauto(AC3Dauto Dauto) {
  new_3Dauto = 0b00001010 | Dauto;
}

void MHI_AC_Ctrl_Core::set_vanes(uint vanes) {
  if (vanes == vanes_swing) {
    new_Vanes0 = 0b11000000; // enable swing
  }
  else {
    new_Vanes0 = 0b10000000; // disable swing
    new_Vanes1 = 0b10000000 | ((vanes - 1) << 4);
  }
}

void MHI_AC_Ctrl_Core::set_silent(boolean silent) {
  new_Silent = 0b10 | silent;
}

void MHI_AC_Ctrl_Core::set_opdata_polling(boolean on) {
  opdata_polling = on;
}

void MHI_AC_Ctrl_Core::set_vanesLR(uint vanesLR) {
  if (vanesLR == vanesLR_swing) {
    new_VanesLR0 = 0b00001011; // enable swing
  }
  else {
    new_VanesLR0 = 0b00001010; // disable swing
    new_VanesLR1 = 0b00010000 | (vanesLR - 1);
  }
}

void MHI_AC_Ctrl_Core::request_ErrOpData() {
  request_erropData = true;
}

void MHI_AC_Ctrl_Core::set_troom(byte troom) {
  //Serial.printf("MHI_AC_Ctrl_Core::set_troom %i\n", troom);
  new_Troom = troom;
}

float MHI_AC_Ctrl_Core::get_troom_offset() {
  return Troom_offset;
}

void MHI_AC_Ctrl_Core::set_troom_offset(float offset) {
  Troom_offset = offset;
}

void MHI_AC_Ctrl_Core::set_frame_size(byte framesize) {
  if (framesize == 20 || framesize == 33)
    frameSize = framesize;
}

int MHI_AC_Ctrl_Core::loop(uint max_time_ms) {
  const byte opdataCnt = sizeof(opdata) / sizeof(byte) / 2;
  long startMillis = millis();             // start time of this loop run
  byte MOSI_byte;                         // received MOSI byte
  bool new_datapacket_received = false;   // indicated that a new frame was received
  static uint call_counter = 0;           // counts how often this loop was called
  static unsigned long lastTroomInternalMillis = 0; // remember when Troom internal has changed
  if (frameSize == 33)
    MISO_frame[0] = 0xAA;

   
  call_counter++;
  int SCKMillis = millis();               // time of last SCK low level
  while (millis() - SCKMillis < 5) {      // wait for 5ms stable high signal to detect a frame start
    if (!digitalRead(SCK_PIN))
      SCKMillis = millis();
    if (millis() - startMillis > max_time_ms)
      return err_msg_timeout_SCK_low;       // SCK stuck@ low error detection
  }
  // build the next MISO frame

  doubleframe = !doubleframe;             // toggle every frame
  MISO_frame[DB14] = doubleframe << 2;    // MISO_frame[DB14] bit2 toggles with every frame

  // Requesting all different opdata's is an opdata cycle. A cycle will take 20s.
  // With the current 21 different opdata's, every opdata request will take about 1sec (interval).
  // If there are only 5 different opdata's defined, these 5 will be spread about the 20s cycle. The interval will increase.
  // requesting a new opdata will always start at a doubleframe start
  if ((frame > (NoFramesPerOpDataCycle / opdataCnt)) && doubleframe ) {    // interval for requesting new opdata depending on de number of opdata requests
    frame = 1;                              // start requesting new OpData
  }
  const bool opdata_due = (frame == 1);
  frame++;                                  // advances on every frame, so the opdata window
                                            // keeps step with doubleframe even while polling
                                            // is switched off

  // ---- service mailbox ---------------------------------------------------------------
  // DB6, DB9 and DB10 are shared by every service transaction: operating data reads, the
  // Silent Mode write, its confirmation read, and the error data request. They are written
  // from here and nowhere else, so no transaction can cut into another's frames.
  //
  // A transaction owns the mailbox for a frame pair starting on a doubleframe, and the
  // mailbox is then held idle for a further pair. Without that gap two transactions run
  // together on the wire as one longer run, which for two quick Silent toggles would look
  // like a single command with the payload changing underneath it.
  if (mailbox_send > 0)
    mailbox_send--;
  if (mailbox_hold > 0)
    mailbox_hold--;

  if (mailbox_send == 0 && mailbox_owner != mailbox_idle) {
    // The payload has finished leaving the wire, so anything waiting on it can move on.
    if (mailbox_owner == mailbox_silent_write) {
      silent_phase = silent_confirm_due;
    }
    else if (mailbox_owner == mailbox_silent_read) {
      // Only now can a reply describe the state after the write. Anything decoded earlier
      // in this frame was already on its way before the request went out.
      silent_phase = silent_awaiting_reply;
      op_silent_known = false;
    }
    mailbox_owner = mailbox_idle;
    mailbox_db6 = 0x80;
    mailbox_db9 = 0xff;
    mailbox_db10 = 0xff;
  }

  if (doubleframe && erropdataCnt > 0)
    erropdataCnt--;                         // the AC is streaming error operating data

  if (mailbox_hold == 0 && doubleframe && erropdataCnt == 0) {
    if (new_Silent != 0) {                  // a user action outranks the rest
      mailbox_db6 = 0x80;
      mailbox_db9 = 0x21;
      mailbox_db10 = new_Silent & 0x01;
      new_Silent = 0;
      mailbox_owner = mailbox_silent_write;
      mailbox_send = 2;
      mailbox_hold = 4;
    }
    else if (silent_phase == silent_confirm_due) {
      // Confirm with a read of our own rather than waiting for the operating data cycle to
      // come round: that cycle can be switched off for frame analysis, and the {0xc0,0xdd}
      // row can be commented out of the opdata table.
      mailbox_db6 = 0xc0;
      mailbox_db9 = 0xdd;
      mailbox_db10 = 0xff;
      silent_phase = silent_confirm_sent;
      mailbox_owner = mailbox_silent_read;
      mailbox_send = 2;
      mailbox_hold = 4;
    }
    else if (request_erropData) {
      mailbox_db6 = 0x80;
      mailbox_db9 = 0x45;
      mailbox_db10 = 0xff;
      request_erropData = false;
      mailbox_owner = mailbox_errdata;
      mailbox_send = 2;
      mailbox_hold = 4;
    }
    else if (opdata_polling && opdata_due) {
      mailbox_db6 = pgm_read_word(opdata + opdataNo);
      mailbox_db9 = pgm_read_word(opdata + opdataNo) >> 8;
      mailbox_db10 = 0xff;
      opdataNo = (opdataNo + 1) % opdataCnt;
      mailbox_owner = mailbox_opdata;
      mailbox_send = 2;
      mailbox_hold = 4;
    }
  }

  MISO_frame[DB6] = mailbox_db6;
  MISO_frame[DB9] = mailbox_db9;
  MISO_frame[DB10] = mailbox_db10;

  if (doubleframe) {                        // and the other MISO data changes are updated when MISO_frame[DB14] bit2 is set
    MISO_frame[DB0] = 0x00;
    MISO_frame[DB1] = 0x00;
    MISO_frame[DB2] = 0x00;

    // set Power, Mode, Tsetpoint, Fan, Vanes
    MISO_frame[DB0] = new_Power;
    new_Power = 0;

    MISO_frame[DB0] |= new_Mode;
    new_Mode = 0;

    MISO_frame[DB2] = new_Tsetpoint;
    new_Tsetpoint = 0;

    MISO_frame[DB1] = new_Fan;
    new_Fan = 0;

    MISO_frame[DB0] |= new_Vanes0;
    MISO_frame[DB1] |= new_Vanes1;
    new_Vanes0 = 0;
    new_Vanes1 = 0;

  }

  MISO_frame[DB3] = new_Troom;  // from MQTT or DS18x20

  uint16_t checksum = calc_checksum(MISO_frame);
  MISO_frame[CBH] = highByte(checksum);
  MISO_frame[CBL] = lowByte(checksum);

  if (frameSize == 33) { // Only for framesize 33 (WF-RAC)
    MISO_frame[DB16] = 0;
    MISO_frame[DB16] |= new_VanesLR1;
    MISO_frame[DB17] = 0;
    MISO_frame[DB17] |= new_VanesLR0;  
    MISO_frame[DB17] |= new_3Dauto;
    new_3Dauto = 0;
    new_VanesLR0 = 0;
    new_VanesLR1 = 0;

    checksum = calc_checksumFrame33(MISO_frame);
    MISO_frame[CBL2] = lowByte(checksum);
  }
  //Serial.println();
  //Serial.print(F("MISO:"));
  // read/write MOSI/MISO frame
  // On ESP8266, digitalRead/Write are ~half an SPI clock period too slow and tip
  // timing-marginal units past the error threshold (issue #191) — drive/read the
  // pins through direct GPIO registers instead. GPIO0..15 only (D1-mini wiring);
  // ESP32 keeps digitalRead/Write unchanged.
#ifdef ESP8266
  const uint32_t sck_mask = (1UL << SCK_PIN), mosi_mask = (1UL << MOSI_PIN), miso_mask = (1UL << MISO_PIN);
  #define MHI_SCK_HIGH      (GPI & sck_mask)
  #define MHI_MOSI_HIGH     (GPI & mosi_mask)
  #define MHI_MISO_WRITE(v) do { if (v) GPOS = miso_mask; else GPOC = miso_mask; } while (0)
#else
  #define MHI_SCK_HIGH      digitalRead(SCK_PIN)
  #define MHI_MOSI_HIGH     digitalRead(MOSI_PIN)
  #define MHI_MISO_WRITE(v) digitalWrite(MISO_PIN, (v))
#endif
  for (uint8_t byte_cnt = 0; byte_cnt < frameSize; byte_cnt++) { // read and write a data packet
    MOSI_byte = 0;
    byte bit_mask = 1;
    for (uint8_t bit_cnt = 0; bit_cnt < 8; bit_cnt++) { // read and write 1 byte
      uint16_t guard = 0;
      while (MHI_SCK_HIGH) { // wait for falling edge (millis() kept out of the spin)
        if (++guard == 0 && millis() - startMillis > max_time_ms)
          return err_msg_timeout_SCK_high;       // SCK stuck@ high error detection
      }
      MHI_MISO_WRITE((MISO_frame[byte_cnt] & bit_mask) > 0);
      while (!MHI_SCK_HIGH) {} // wait for rising edge
      if (MHI_MOSI_HIGH)
        MOSI_byte += bit_mask;
      bit_mask = bit_mask << 1;
    }
    if (MOSI_frame[byte_cnt] != MOSI_byte) {
      new_datapacket_received = true;
      MOSI_frame[byte_cnt] = MOSI_byte;
    }
  }
#undef MHI_SCK_HIGH
#undef MHI_MOSI_HIGH
#undef MHI_MISO_WRITE

  checksum = calc_checksum(MOSI_frame);
  int frame_status = err_msg_valid_frame;
  if (((MOSI_frame[SB0] & 0xfe) != 0x6c) | (MOSI_frame[SB1] != 0x80) | (MOSI_frame[SB2] != 0x04))
    frame_status = err_msg_invalid_signature;
  else if ((MOSI_frame[CBH] << 8 | MOSI_frame[CBL]) != checksum)
    frame_status = err_msg_invalid_checksum;
  else if (frameSize == 33)   // Only for framesize 33 (WF-RAC)
    if (MOSI_frame[CBL2] != lowByte(calc_checksumFrame33(MOSI_frame)))
      frame_status = err_msg_invalid_checksum;

  if (m_cbiFrame != nullptr)
    m_cbiFrame->cbiFrameFunction(MOSI_frame, MISO_frame, frameSize, frame_status);

  if (frame_status != err_msg_valid_frame)
    return frame_status;

  if (new_datapacket_received) {

    if (frameSize == 33) { // Only for framesize 33 (WF-RAC)
      byte vanesLRtmp = (MOSI_frame[DB16] & 0x07) + ((MOSI_frame[DB17] & 0x01) << 4);
      if (vanesLRtmp != status_vanesLR_old) { // Vanes Left Right
        if ((vanesLRtmp & 0x10) != 0) // Vanes LR status swing
          m_cbiStatus->cbiStatusFunction(status_vanesLR, vanesLR_swing);
        else {
          m_cbiStatus->cbiStatusFunction(status_vanesLR, (vanesLRtmp & 0x07) + 1 );
        }
        status_vanesLR_old = vanesLRtmp;
      }

      if ((MOSI_frame[DB17] & 0x04) != status_3Dauto_old) { // 3D auto
        status_3Dauto_old = MOSI_frame[DB17] & 0x04;
        m_cbiStatus->cbiStatusFunction(status_3Dauto, status_3Dauto_old);
      }
    }
    // evaluate status
    if ((MOSI_frame[DB0] & 0x1c) != status_mode_old) { // Mode
      status_mode_old = MOSI_frame[DB0] & 0x1c;
      m_cbiStatus->cbiStatusFunction(status_mode, status_mode_old);
    }

    if ((MOSI_frame[DB0] & 0x01) != status_power_old) { // Power
      status_power_old = MOSI_frame[DB0] & 0x01;
      m_cbiStatus->cbiStatusFunction(status_power, status_power_old);
    }

    uint fantmp = MOSI_frame[DB1] & 0x07;
    if (fantmp != status_fan_old) {
      status_fan_old = fantmp;
      m_cbiStatus->cbiStatusFunction(status_fan, status_fan_old);
    }

    // Only updated when Vanes command via wired RC
    uint vanestmp = (MOSI_frame[DB0] & 0xc0) + ((MOSI_frame[DB1] & 0xB0) >> 4);
    if (vanestmp != status_vanes_old) {
      // if ((vanestmp & 0x88) == 0) // last vanes update was via IR-RC, so status is not known
      //   m_cbiStatus->cbiStatusFunction(status_vanes, vanes_unknown);
      // else 
      if ((vanestmp & 0x40) != 0) // Vanes status swing
        m_cbiStatus->cbiStatusFunction(status_vanes, vanes_swing);
      else {
        m_cbiStatus->cbiStatusFunction(status_vanes, (vanestmp & 0x03) + 1);
      }
      status_vanes_old = vanestmp;
    }

    
    if(MOSI_frame[DB3] != status_troom_old) {
      // To avoid jitter with the fast changing AC internal temperature sensor
      if (MISO_frame[DB3] != 0xff) {                                      // not internal sensor used, just publish
        status_troom_old = MOSI_frame[DB3];
        m_cbiStatus->cbiStatusFunction(status_troom, status_troom_old);
        lastTroomInternalMillis = 0;
      }
      else                                                               //  internal sensor used
        if ((unsigned long)(millis() - lastTroomInternalMillis) > minTimeInternalTroom) { // Only publish when last change was more then minTimeInternalTroom ago
          lastTroomInternalMillis = millis();
          status_troom_old = MOSI_frame[DB3];
          m_cbiStatus->cbiStatusFunction(status_troom, status_troom_old);
        }
    }
    
    if (MOSI_frame[DB2] != status_tsetpoint_old) { // Temperature setpoint
      status_tsetpoint_old = MOSI_frame[DB2];
      m_cbiStatus->cbiStatusFunction(status_tsetpoint, status_tsetpoint_old);
    }

    if ((MOSI_frame[DB13] & 0x02) != status_heating_old) { // heating, as opposed to the requested mode
      status_heating_old = MOSI_frame[DB13] & 0x02;
      m_cbiStatus->cbiStatusFunction(status_heating, status_heating_old != 0);
    }

    if ((MOSI_frame[DB13] & 0x04) != status_compressor_old) { // compressor actually running
      status_compressor_old = MOSI_frame[DB13] & 0x04;
      m_cbiStatus->cbiStatusFunction(status_compressor, status_compressor_old != 0);
    }

    if (MOSI_frame[DB4] != status_errorcode_old) { // error code
      status_errorcode_old = MOSI_frame[DB4];
      m_cbiStatus->cbiStatusFunction(status_errorcode, status_errorcode_old);
    }

    // Evaluate Operating Data and Error Operating Data
    bool MOSI_type_opdata = (MOSI_frame[DB10] & 0x30) == 0x10;

    switch (MOSI_frame[DB9]) {
      case 0x94:                              // 0 energy-kwh n * 0.25 kWh used since power on
        if ((MOSI_frame[DB6] & 0x80) != 0) {  // 
          if (MOSI_type_opdata) {
            if (((MOSI_frame[DB12]<<8)+(MOSI_frame[DB11])) != op_kwh_old) {
              op_kwh_old = (MOSI_frame[DB12]<<8)+(MOSI_frame[DB11]);
              m_cbiStatus->cbiStatusFunction(opdata_kwh, op_kwh_old);
            }
          }
          //else
          //  m_cbiStatus->cbiStatusFunction(erropdata_unknown, op_unknown_old);  // noch nie gesehen, dass es auftaucht
        }
        break;
      case 0x02:
        if ((MOSI_frame[DB6] & 0x80) != 0) {  // 1 MODE
          if (MOSI_type_opdata) {
            if ((MOSI_frame[DB10] != op_mode_old)) {
              op_mode_old = MOSI_frame[DB10];
              m_cbiStatus->cbiStatusFunction(opdata_mode, (op_mode_old & 0x0f) << 2);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_mode, (MOSI_frame[DB10] & 0x0f) << 2);
        }
        break;
      case 0x05:
        if ((MOSI_frame[DB6] & 0x80) != 0) {  // 2 SET-TEMP
          if (MOSI_frame[DB10] == 0x13) {
            if (MOSI_frame[DB11] != op_settemp_old) {
              op_settemp_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_tsetpoint, op_settemp_old);
            }
          }
          else if (MOSI_frame[DB10] == 0x33)
            m_cbiStatus->cbiStatusFunction(erropdata_tsetpoint, MOSI_frame[DB11]);
        }
        break;
      case 0x81:                              // 5 THI-R1 or 6 THI-R2
        if ((MOSI_frame[DB6] & 0x80) != 0) {  // 5 THI-R1
          if ((MOSI_frame[DB10] & 0x30) == 0x20) {
            if (MOSI_frame[DB11] != op_thi_r1_old) {
              op_thi_r1_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_thi_r1, op_thi_r1_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_thi_r1, MOSI_frame[DB11]);
        }
        else {                                // 6 THI-R2
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB11] != op_thi_r2_old) {
              op_thi_r2_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_thi_r2, op_thi_r2_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_thi_r2, MOSI_frame[DB11]);
        }
        break;
      case 0x87:
        if ((MOSI_frame[DB6] & 0x80) != 0) {  // 7 THI-R3
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB11] != op_thi_r3_old) {
              op_thi_r3_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_thi_r3, op_thi_r3_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_thi_r3, MOSI_frame[DB11]);
        }
        break;
      case 0x80:                              // 3 RETURN-AIR or 21 OUTDOOR
        if ((MOSI_frame[DB6] & 0x80) != 0) {  // 3 RETURN-AIR
          if ((MOSI_frame[DB10] & 0x30) == 0x20) {           // operating Data
            if (MOSI_frame[DB11] != op_return_air_old) {
              op_return_air_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_return_air, op_return_air_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_return_air, MOSI_frame[DB11]);
        }
        else {                                // 21 OUTDOOR
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB11] != op_outdoor_old) {
              op_outdoor_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_outdoor, op_outdoor_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_outdoor, MOSI_frame[DB11]);
        }
        break;
      case 0x1f:                              // 8 IU-FANSPEED or 34 OU-FANSPEED
        if ((MOSI_frame[DB6] & 0x80) != 0) {  // 8 IU-FANSPEED
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB10] != op_iu_fanspeed_old) {
              op_iu_fanspeed_old = MOSI_frame[DB10];
              m_cbiStatus->cbiStatusFunction(opdata_iu_fanspeed, op_iu_fanspeed_old & 0x0f);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_iu_fanspeed, MOSI_frame[DB10] & 0x0f);
        }
        else {                                // 34 OU-FANSPEED
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB10] != op_ou_fanspeed_old) {
              op_ou_fanspeed_old = MOSI_frame[DB10];
              m_cbiStatus->cbiStatusFunction(opdata_ou_fanspeed, op_ou_fanspeed_old & 0x0f);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_ou_fanspeed, MOSI_frame[DB10] & 0x0f);
        }
        break;
      case 0x1e:                              // 12 TOTAL-IU-RUN or 37 TOTAL-COMP-RUN
        if ((MOSI_frame[DB6] & 0x80) != 0) {  // 12 TOTAL-IU-RUN
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB11] != op_total_iu_run_old) {
              op_total_iu_run_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_total_iu_run, op_total_iu_run_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_total_iu_run, MOSI_frame[DB11]);
        }
        else {                                // 37 TOTAL-COMP-RUN
          if (MOSI_frame[DB10] == 0x11) {
            if (MOSI_frame[DB11] != op_total_comp_run_old) {
              op_total_comp_run_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_total_comp_run, op_total_comp_run_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_total_comp_run, MOSI_frame[DB11]);
        }
        break;
      case 0x82:
        if ((MOSI_frame[DB6] & 0x80) == 0) {  // 22 ThO-R1
          if (MOSI_type_opdata) {    // operating data
            if (MOSI_frame[DB11] != op_tho_r1_old) {
              op_tho_r1_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_tho_r1, op_tho_r1_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_tho_r1, MOSI_frame[DB11]);
        }
        break;
      case 0x11:
        if ((MOSI_frame[DB6] & 0x80) == 0) {  // 24 COMP
          if (MOSI_type_opdata) {
            if ((MOSI_frame[DB10] << 8 | MOSI_frame[DB11]) != op_comp_old) {
              op_comp_old = MOSI_frame[DB10] << 8 | MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_comp, op_comp_old & 0x0fff);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_comp, (MOSI_frame[DB10] << 8 | MOSI_frame[DB11]) & 0x0fff);
        }
        break;
      case 0x85:
        if ((MOSI_frame[DB6] & 0x80) == 0) {  // 27 Td
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB11] != op_td_old) {
              op_td_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_td, op_td_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_td, MOSI_frame[DB11]);
        }
        break;
      case 0x90:
        if ((MOSI_frame[DB6] & 0x80) == 0) {  // 29 CT
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB11] != op_ct_old) {
              op_ct_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_ct, op_ct_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_ct, MOSI_frame[DB11]);
        }
        break;
      case 0xb1:
        if ((MOSI_frame[DB6] & 0x80) == 0) {  // 32 TDSH
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB11] != op_tdsh_old) {
              op_tdsh_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_tdsh, op_tdsh_old / 2);
            }
          }
        }
        break;
      case 0x7c:
        if ((MOSI_frame[DB6] & 0x80) == 0) {  // 33 PROTECTION-No
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB11] != op_protection_no_old) {
              op_protection_no_old = MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_protection_no, op_protection_no_old);
            }
          }
        }
        break;
      case 0x0c:
        if ((MOSI_frame[DB6] & 0x80) == 0) {  // 36 DEFROST
          if (MOSI_type_opdata) {
            if (MOSI_frame[DB10] != op_defrost_old) {
              op_defrost_old = MOSI_frame[DB10];
              m_cbiStatus->cbiStatusFunction(opdata_defrost, op_defrost_old & 0b1);
            }
          }
        }
        break;
      case 0x13:
        if ((MOSI_frame[DB6] & 0x80) == 0) {  // 38 OU-EEV
          if (MOSI_type_opdata) {
            if ((MOSI_frame[DB12] << 8 | MOSI_frame[DB11]) != op_ou_eev1_old) {
              op_ou_eev1_old = MOSI_frame[DB12] << 8 | MOSI_frame[DB11];
              m_cbiStatus->cbiStatusFunction(opdata_ou_eev1, op_ou_eev1_old);
            }
          }
          else
            m_cbiStatus->cbiStatusFunction(erropdata_ou_eev1, MOSI_frame[DB12] << 8 | MOSI_frame[DB11]);
        }
        break;
      case 0xdd:                              // Silent Mode (outdoor unit quiet function)
        // The AC also sends this record unsolicited after Silent Mode is changed with the
        // remote, and then DB6 does not carry the request group, so it is not checked here.
        if ((MOSI_frame[DB10] == 0x80) && (MOSI_frame[DB12] == 0x00)) {
          // Only bit 5 is reported, so only bit 5 is remembered. Caching the whole byte
          // re-announced the same Silent Mode state whenever an unrelated flag moved.
          byte silent_flag = MOSI_frame[DB11] & 0x20;
          if (silent_phase == silent_awaiting_reply)
            silent_phase = silent_settled;    // the answer to our own confirmation read
          if (!op_silent_known || silent_flag != op_silent_old) {
            op_silent_old = silent_flag;
            op_silent_known = true;
            m_cbiStatus->cbiStatusFunction(opdata_silent, silent_flag != 0);
          }
        }
        else
          // A different layout means this model encodes Silent Mode some other way. Report
          // it as unknown rather than swallowing it: that record is the evidence anyone
          // reporting an unsupported model needs.
          m_cbiStatus->cbiStatusFunction(opdata_unknown, MOSI_frame[DB10] << 8 | MOSI_frame[DB9]);
        break;
      case 0x21:  // our own Silent Mode write selector echoed back, nothing to report
        break;
      case 0x45: // last error number or count of following error operating data
        if ((MOSI_frame[DB6] & 0x80) != 0) {
          if (MOSI_frame[DB10] == 0x11) {     // last error number
            m_cbiStatus->cbiStatusFunction(erropdata_errorcode, MOSI_frame[DB11]);
          }
          else if (MOSI_frame[DB10] == 0x12) { // count of following error operating data
            erropdataCnt = MOSI_frame[DB11] + 4;
          }
        }
        break;
      case 0x00:  // dummy
        break;
      case 0xff:  // default
        break;
      default:    // unknown operating data
        m_cbiStatus->cbiStatusFunction(opdata_unknown, MOSI_frame[DB10] << 8 | MOSI_frame[DB9]);
        // Serial.printf("Unknown operating data, MOSI_frame[DB9]=%i MOSI_frame[D10]=%i\n", MOSI_frame[DB9], MOSI_frame[DB10]);
    }
  }
  return call_counter;
}
