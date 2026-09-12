import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_HEAT,
    DEVICE_CLASS_RUNNING,
)
from .. import MhiAcCtrl, CONF_MHI_AC_CTRL_ID

mhi_ns = cg.esphome_ns.namespace('mhi')
MhiBinarySensors = mhi_ns.class_('MhiBinarySensors', cg.Component)

CONF_DEFROST = "defrost"
CONF_3D_AUTO = "vanes_3d_auto_enabled"
CONF_COMPRESSOR = "compressor"
CONF_HEATING = "heating"

ICON_SNOWLAKE_MELT = "mdi:snowflake-melt"
ICON_3D="mdi:video-3d"

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MhiBinarySensors),
    cv.GenerateID(CONF_MHI_AC_CTRL_ID): cv.use_id(MhiAcCtrl),
    cv.Optional(CONF_DEFROST): binary_sensor.binary_sensor_schema(
        icon=ICON_SNOWLAKE_MELT
    ),
    cv.Optional(CONF_3D_AUTO): binary_sensor.binary_sensor_schema(
        icon=ICON_3D
    ),
    # Both come from DB13, which arrives in every frame, so these follow what the unit is
    # actually doing rather than what it was last asked to do.
    cv.Optional(CONF_COMPRESSOR): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_RUNNING,
    ),
    cv.Optional(CONF_HEATING): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_HEAT,
    ),
})


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    mhi = await cg.get_variable(config[CONF_MHI_AC_CTRL_ID])
    
    await cg.register_component(var, config)
    await cg.register_parented(var, mhi)

    if CONF_DEFROST in config:
        conf = config[CONF_DEFROST]
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_defrost(sens))
    if CONF_3D_AUTO in config:
        conf = config[CONF_3D_AUTO]
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_vanes_3d_auto_enabled(sens))
    if CONF_COMPRESSOR in config:
        conf = config[CONF_COMPRESSOR]
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_compressor(sens))
    if CONF_HEATING in config:
        conf = config[CONF_HEATING]
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(var.set_heating(sens))