[!["Buy Me A Coffee"](https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png)](https://www.buymeacoffee.com/ginkage)
[![paypal RobertJansen1](https://www.paypalobjects.com/en_GB/i/btn/btn_donate_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=TL3SFZ4P6ZDHN)

# How to get started

Create a new device within ESPHome builder and combine the yaml with one of the examples from the `examples` directory. Give your unit a name, configure OTA passwords, hotspot, add an API key (generator can be found on https://esphome.io/components/api/#configuration-variables) and install!  

- [`simple.yaml`](https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/blob/master/examples/simple.yaml): Basic yaml to get started, contains climate and fan direction control.
- [`external_sensor.yaml`](https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/blob/master/examples/external_sensor.yaml): Contains the basics to configure the room temperature using an external temperature sensor connected to Home Assistant.  
- [`full.yaml`](https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/blob/master/examples/full.yaml): Contains all (20+!) of the metrics that are possibly in the unit and returns them.  
- [`simple-energy-management.yaml`](https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/blob/master/examples/simple-energy-measurement.yaml): Contains the basics to get started with energy measuring (assuming a voltage of 230v to convert A to Wh).

# MHI-AC-Ctrl-ESPHome
This project is a simple integration of the amazing work [absalom-muc](https://github.com/absalom-muc) has done with his project [MHI-AC-Ctrl](https://github.com/absalom-muc/MHI-AC-Ctrl).\
It's supposed to simplify the [Home Assistant](https://www.home-assistant.io/) setup, while giving you OTA and auto-discovery with virtually zero effort and no MQTT needed, powered by [ESPHome](https://ESPHome.io/).\
`MHI-AC-Ctrl-core.*` files were forked directly, with a small modification to allow for pin setting in yaml, whereas your WiFi credentials should go into the `*.yaml` file, and `mhi_ac_ctrl.h` is the core of the integration.

# Fan Modes Up/Down Left/Right
Most newer MHI units (the ones supporting the WF-RAC WiFi module) support fine grained vane control for Left/Right and Up/Down.  
When your log is flooded with `mhi_ac_ctrl_core.loop error: -2` errors after updating to the newer code, please change your yaml file to set `frame_size: 20`.  
Currently the MHI code allows for more fine grained fan direction than ESPHome climate supports. for that, additional template parts are added.  
There are 8 modes for Left/Right: Left, Left/Center, Center, Center/Right, Right, Wide, Spot and Swing  
There are 5 modes for Up/Down: Up, Up/Center, Center/Down, Down and Swing  
Setting swing from the ESPHome climate now fully works. It will store the oldvanes mode, and configure swing. After disabling swing (either vertically, horizontally or off), the old settings will be restored. Manually changing modes for Left/Right or Up/Down will update the climate state as well.

# Climate Quiet

Climate Quiet was added to ESPhome, so QUIET was added. Ordering still needs work (https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/22#issuecomment-1744448983)
Added the solution for the auto mode from: https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/22#issuecomment-1310271934:
CLIMATE_FAN_DIFFUSE in fan speed and status sections and reshuffle the numbers and add CLIMATE_FAN_DIFFUSE to the traits.set_supported_fan_modes

Has now 5 different fan modes but I'm not sure if the auto mode works proper, keep testing.

# Silent Mode

Silent Mode is the **outdoor** unit's quiet function (the `Silent` button on the remote): it
caps the compressor and outdoor fan speed, which roughly halves power draw at the cost of
slower heating and cooling. It is a different thing from the indoor fan's quiet speed
described above, which is a climate fan mode.

Enable the switch to get it:

```yaml
switch:
  - platform: MhiAcCtrl
    silent_mode:
      name: "Silent mode"
```

The state is read back from the AC, so switching Silent Mode with the remote is reflected in
Home Assistant too. It works on both frame sizes, because the command travels in the service
mailbox that the short frame already carries.

Protocol details, reverse engineered by [@mreijnde](https://github.com/mreijnde) in
[issue #166](https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/166):

| Direction | DB6 | DB9 | DB10 | DB11 | DB12 | Meaning |
| --- | --- | --- | --- | --- | --- | --- |
| controller &rarr; AC | `80` | `21` | `01` | `ff` | `ff` | Silent ON |
| controller &rarr; AC | `80` | `21` | `00` | `ff` | `ff` | Silent OFF |
| controller &rarr; AC | `c0` | `dd` | `ff` | `ff` | `ff` | read the state |
| AC &rarr; controller | | `dd` | `80` | flags | `00` | state, silent when `DB11 & 0x20` |

> Confirmed on an SCM60ZS-W. Silent Mode is not documented by MHI, so if your unit does not
> respond to the switch, please report the model in the issue above.

# SPI frame logging

Two optional switches help work out what the undocumented bytes in the protocol mean. Both
are in [`examples/full.yaml`](examples/full.yaml):

```yaml
switch:
  - platform: MhiAcCtrl
    spi_logging:
      name: "SPI logging"
    operating_data_polling:
      name: "Operating data polling"
```

Silent Mode logs what it does at `INFO`, so the evidence survives the `level: INFO` that the
shipped examples set: the command being written, and the state the AC reports back. The
per-frame chatter stays at `DEBUG`, because logging every frame is what disturbs the timing
in the first place.

`spi_logging` dumps frames to the ESPHome log as hex, MOSI alongside MISO. `operating_data_polling`
turns the operating data requests off, which stops the service mailbox bytes churning.

**Only changed frames are logged.** Frames arrive about 20 times a second, and formatting and
shipping every one of them costs more than the frame interval allows, which disturbs the very
SPI timing you are trying to observe. Ignored when deciding whether a frame changed: the
signature toggle, the frame-pair bit, the checksums and the AC's own room temperature in
`DB3`, all of which move every frame by design.

Frames the controller rejected are logged too, but rate limited to about one line a second
with a count of what was skipped, because a bad bus rejects every frame and an unthrottled
log would make the timing worse. Frames lost to a stuck clock are not logged at all: they
never complete a transfer, so nothing sees them.

To hunt for an unknown field:

1. Turn `operating_data_polling` **off**. A settled unit then goes almost silent, because the
   mailbox stops rotating through its selectors. Every operating data sensor stops updating
   until you turn it back on. Silent Mode still reads itself back, so that switch keeps
   working, but that is one exchange per toggle rather than a continuous poll.
2. Turn `spi_logging` **on**. The first frame is logged as a baseline.
3. Capture to a file with `esphome logs your-device.yaml > capture.log 2>&1`.
4. Press one button on the remote. Press one, not several.
5. A byte that moves only on the keypress and then stays put is your candidate. Repeat it a
   few times before believing it.

Then turn both switches back: logging always starts off after a reboot, and polling always
starts on, so a reboot is enough if you forget.

Bear in mind that the IR remote talks to the indoor unit directly, not across this bus, so a
keypress shows up as the resulting *state* on MOSI. That is enough to identify a field, and
for the cyclic fields it is usually enough to control one too, because those carry a matching
"set" indicator bit in the same position. It is not enough to derive a service write command:
those use a different selector entirely, which is why `C0/DD` reads Silent Mode but `80/21`
writes it.

# Low temperature heating and cooling

To allow for lower temperature heating or cooling, set the visual_min_temperature in the climate section of the yaml like so:

```yaml
climate:  
  - platform: MhiAcCtrl  
    name: "MHI Air Conditioner"  
    temperature_offset: true  
    visual_min_temperature: 17.0  
```
This will allow for lower temperature heating or cooling.


# Hardware
 - Hardware designed by [fonske](https://github.com/fonske) can be found [here](JLCPCB/Hardware.md)
 - A ready-to-go esp32-s3 based airco controller can be bought at [tinytronics.nl](https://www.tinytronics.nl/en/development-boards/microcontroller-boards/with-wi-fi/universal-air-conditioning-controller-esp32-s3) instructions can be found [here](UniversalAircoController/README.md)


# Changelog:

**Unreleased**
 - Silent Mode switch: read and control the outdoor unit quiet function https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/166
 - `compressor` and `heating` binary sensors, decoded from DB13
 - `frame_errors` sensor, a running total of unreadable frames
 - `spi_logging` and `operating_data_polling` switches for protocol analysis
 - Host-side tests for the SPI framing in `test/`

**v4.2** (2025-07)
 - Allow configuration of pins through yaml
 - Update calculation of Indoor Heat exchanger temperature 2 (capillary)
 - Don't spam unit with unchanged room_temp
 - **Deprecating set set_vertical_vanes and set_horizontal_vanes, will be removed in v4.3 use select functions for fan control**

**v4.1** (2025-07)
 - Changed climate.CLIMATE_SCHEMA to fix deprecation warning in 2025.5.0 and higher https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/151
 - Make 0.5 degrees setpoint actually work https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/88
 - Allow low temp heating and cooling (below 18 degrees) https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/152
 - Allow fan speed from esphome web interface https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/136
 - Deprecating set set_vertical_vanes and set_horizontal_vanes, will be removed in v4.3 use select functions for fan control

**v4.0** (2025-04)
 - Compatibility with ESPHOME 2025.2+
 - Breaking change: the implementation is ported to the native ESPHome codegen
   - The configuration file is significantly simplified
   - No need for custom code in the config file
   - Sensors are no longer positional
 - External sensor support

**v3.0** (2024-08)
 - Breaking change: moved all files to component and allow for easy install, thanks to @XMaarten and https://github.com/hberntsen/mhi-ac-ctrl-esp32-c3
   - When you are upgrading from v2.1 or older, and experience compile errors, please see https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/100#issuecomment-2395388853 for manual cleanup steps
 - Manually downloading the files to your Home Assistant setup is no longer needed
 - Legacy or Large framesize files are merged again

**v2.1** (2024-03)
 - Breaking change: Cleaned up conf files
 - Add restart button
 - Update Home Assistant naming convention
 - Enable energy dashboard usage 

**v2.0** (2024-01)
 - Based on absalom-muc v2.8 (September 2023)
 - Breaking change in YAML configuration (need to set frame_size in globals)
 - Added legacy support configurable from YAML (removing 3d auto and vanes LR control)

# FAQ

### I have no clue where to start, what can i do?
Step 1: Learn Dutch to understand the blogpost (or use some translation service :) 
Step 2: Go to this extensive blogpost: https://www.twoenter.nl/blog/smarthome/mitsubishi-airco-voorzien-van-wifi-besturing/ 
  
### I am getting the following logline in the console of my device:
```
[W][component:237]: Component MhiAcCtrl took a long time for an operation (52 ms).
[W][component:238]: Components should block for at most 30 ms.
```

This is because ESPHome now alerts for slow components. To ignore this warning you can configure logging for components to ERROR, this will suppress the WARNING. See https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/issues/61 for more information.
```yaml
# Enable logging
logger:
    level: INFO
    baud_rate: 0
    logs:
        component: ERROR
```

### I am getting `mhi_ac_ctrl_core.loop error: -2` errors and nothing works!  
You probably have an older version of the Airco unit which doesn't support the newer, larger framesize.   
You can to change the frame_size in the yaml to 20 to disable newer functionality.  
When framsize is set to 20, 3D auto, and vane Left / Right doesn't work, vane Up / Down works limited.  
All other features should work fine.  

### Everything has changed, how do I update?!?  
As time progresses, ESPHome and this project evolved, allowing for more features and easier installation and updates in the future. When you are on an older versione (whether that is v3.0, v2.0 or earlier) the best way forward is to get one of the example yaml files from the examples folder and apply this to your current yaml file. 
 1. Make a copy of your current yaml file, and save it somewhere outside of Home Assistant.
 2. Did you not skip step 1?
 3. Choose one of the example yaml files from the [examples dir](https://github.com/ginkage/MHI-AC-Ctrl-ESPHome/tree/master/examples)
 4. Carefully copy the basic information from the backup file into the example file. Take things like name, ota password, wifi credentials, ap fallback settings, api settings and key, and frame_size
 5. Double check your new yaml and paste it in ESPHome builder and deploy
 6. If you replaced all information carefully, all should build fine. If not, read the message and see what parts are missing or duplicate (most frequently done wrong)


# License
This project is licensed under the MIT License - see the LICENSE file for details.\
(TL;DR: Do whatever you want with the code, no warranty given, give credit where it's due.)
