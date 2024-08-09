#pragma once

#include "esphome/components/remote_base/remote_base.h"
#include "esphome/core/component.h"

#include <vector>

namespace esphome {
namespace tuya_rf {

class TuyaRfComponent : public remote_base::RemoteTransmitterBase,
                                   public Component
{
 public:
  explicit TuyaRfComponent() : remote_base::RemoteTransmitterBase(NULL) {}
  void setup() override;

  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::DATA; }


 protected:
  void send_internal(uint32_t send_times, uint32_t send_wait) override;

  void mark_(uint32_t usec);

  void space_(uint32_t usec);

  void await_target_time_();
  uint32_t target_time_;

};

}  // namespace tuya_rf
}  // namespace esphome
