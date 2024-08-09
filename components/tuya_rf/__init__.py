import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import remote_base
from esphome.const import CONF_ID

AUTO_LOAD = ["remote_base"]
DEPENDENCIES = ["libretiny"]

tuya_rf_ns = cg.esphome_ns.namespace("tuya_rf")
TuyaRfComponent = tuya_rf_ns.class_(
    "TuyaRfComponent", remote_base.RemoteTransmitterBase, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TuyaRfComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
