import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from esphome.const import (
    DEVICE_CLASS_SWITCH
)

from .. import MhiAcCtrl, CONF_MHI_AC_CTRL_ID

mhi_ns = cg.esphome_ns.namespace('mhi')
Mhi3dAutoSwitch = mhi_ns.class_('Mhi3dAutoSwitch', switch.Switch, cg.Component)
MhiSilentSwitch = mhi_ns.class_('MhiSilentSwitch', switch.Switch, cg.Component)

CONF_VANES_3D_AUTO = "vanes_3d_auto"
CONF_SILENT_MODE = "silent_mode"
ICON_3D="mdi:video-3d"
ICON_SILENT="mdi:volume-low"

CONFIG_SCHEMA = cv.Schema({    
    cv.GenerateID(CONF_MHI_AC_CTRL_ID): cv.use_id(MhiAcCtrl),
    cv.Optional(CONF_VANES_3D_AUTO): switch.switch_schema(
        Mhi3dAutoSwitch,
        device_class=DEVICE_CLASS_SWITCH,
        icon=ICON_3D,
    ),
    # The AC reports its own Silent Mode state, so don't write a restored state on boot.
    cv.Optional(CONF_SILENT_MODE): switch.switch_schema(
        MhiSilentSwitch,
        device_class=DEVICE_CLASS_SWITCH,
        icon=ICON_SILENT,
        default_restore_mode="DISABLED",
    ),
})

async def to_code(config):
    mhi = await cg.get_variable(config[CONF_MHI_AC_CTRL_ID])
    if vanes_3d_auto_config := config.get(CONF_VANES_3D_AUTO):
        s = await switch.new_switch(vanes_3d_auto_config)
        await cg.register_component(s, vanes_3d_auto_config)
        await cg.register_parented(s, mhi)
    if silent_mode_config := config.get(CONF_SILENT_MODE):
        s = await switch.new_switch(silent_mode_config)
        await cg.register_component(s, silent_mode_config)
        await cg.register_parented(s, mhi)
