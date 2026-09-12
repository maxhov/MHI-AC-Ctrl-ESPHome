import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch

from esphome.const import (
    DEVICE_CLASS_SWITCH,
    ENTITY_CATEGORY_CONFIG,
)

from .. import MhiAcCtrl, CONF_MHI_AC_CTRL_ID

mhi_ns = cg.esphome_ns.namespace('mhi')
Mhi3dAutoSwitch = mhi_ns.class_('Mhi3dAutoSwitch', switch.Switch, cg.Component)
MhiSilentSwitch = mhi_ns.class_('MhiSilentSwitch', switch.Switch, cg.Component)
MhiSpiLogSwitch = mhi_ns.class_('MhiSpiLogSwitch', switch.Switch, cg.Component)
MhiOpdataPollingSwitch = mhi_ns.class_('MhiOpdataPollingSwitch', switch.Switch, cg.Component)

CONF_VANES_3D_AUTO = "vanes_3d_auto"
CONF_SILENT_MODE = "silent_mode"
CONF_SPI_LOGGING = "spi_logging"
CONF_OPDATA_POLLING = "operating_data_polling"
ICON_3D="mdi:video-3d"
ICON_SILENT="mdi:volume-low"
ICON_SPI_LOG="mdi:console"
ICON_POLLING="mdi:database-sync"

CONFIG_SCHEMA = cv.Schema({    
    cv.GenerateID(CONF_MHI_AC_CTRL_ID): cv.use_id(MhiAcCtrl),
    cv.Optional(CONF_VANES_3D_AUTO): switch.switch_schema(
        Mhi3dAutoSwitch,
        device_class=DEVICE_CLASS_SWITCH,
        icon=ICON_3D,
    ),
    # No restore mode is set: no switch here calls restore_state_(), so none of them write
    # a remembered state on boot. The AC reports its own Silent Mode state instead.
    cv.Optional(CONF_SILENT_MODE): switch.switch_schema(
        MhiSilentSwitch,
        device_class=DEVICE_CLASS_SWITCH,
        icon=ICON_SILENT,
    ),
    # Both diagnostics force their safe state in setup(), so the restore mode is irrelevant:
    # logging always boots off, polling always boots on.
    cv.Optional(CONF_SPI_LOGGING): switch.switch_schema(
        MhiSpiLogSwitch,
        icon=ICON_SPI_LOG,
        entity_category=ENTITY_CATEGORY_CONFIG,
    ),
    cv.Optional(CONF_OPDATA_POLLING): switch.switch_schema(
        MhiOpdataPollingSwitch,
        icon=ICON_POLLING,
        entity_category=ENTITY_CATEGORY_CONFIG,
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
    if spi_logging_config := config.get(CONF_SPI_LOGGING):
        s = await switch.new_switch(spi_logging_config)
        await cg.register_component(s, spi_logging_config)
        await cg.register_parented(s, mhi)
    if opdata_polling_config := config.get(CONF_OPDATA_POLLING):
        s = await switch.new_switch(opdata_polling_config)
        await cg.register_component(s, opdata_polling_config)
        await cg.register_parented(s, mhi)
