#include "tuya_rf.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "radio.h"
#ifdef USE_LIBRETINY

namespace esphome {
namespace tuya_rf {

static const char *const TAG = "tuya_rf";

void TuyaRfComponent::setup() {
  //this->pin_->setup();
  //this->pin_->pin_mode(gpio::FLAG_OUTPUT);
  //this->pin_->digital_write(false);
  pinMode(CMT2300A_GPIO1_PIN, OUTPUT);
  digitalWrite(CMT2300A_GPIO1_PIN, LOW);
}

void TuyaRfComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya Rf, no config parameters");
}

void TuyaRfComponent::await_target_time_() {
  const uint32_t current_time = micros();
  if (this->target_time_ == 0) {
    this->target_time_ = current_time;
  } else {
    while (this->target_time_ > micros()) {
      // busy loop that ensures micros is constantly called
    }
  }
}

void TuyaRfComponent::mark_(uint32_t usec) {
  this->await_target_time_();
  //this->pin_->digital_write(true);
  digitalWrite(CMT2300A_GPIO1_PIN, LOW);
  this->target_time_ += usec;
  ///delayMicroseconds(usec);
}

void TuyaRfComponent::space_(uint32_t usec) {
  this->await_target_time_();
  digitalWrite(CMT2300A_GPIO1_PIN, HIGH);
  //this->pin_->digital_write(false);
  this->target_time_ += usec;
  ///delayMicroseconds(usec);
}

void IRAM_ATTR TuyaRfComponent::send_internal(uint32_t send_times, uint32_t send_wait) {
  ESP_LOGD(TAG, "Sending remote code...");
  /*
    for (int32_t item : this->temp_.get_data()) {
      if (item > 0) {
        const auto length = uint32_t(item);
        ESP_LOGD(TAG, "pulse %d",length);
      } else {
        const auto length = uint32_t(-item);
        ESP_LOGD(TAG, "space %d",length);
      }
    }
*/
  
  InterruptLock lock;
  
  int res=StartTx();
  switch(res) {
    case 0:
      //ESP_LOGD(TAG,"StartTx ok");
      break;
    case 1:
      ESP_LOGE(TAG,"Error Rf_Init");
      return;
    case 2:
      ESP_LOGE(TAG,"Error go tx");
      return;
    default:
      ESP_LOGE(TAG,"Unknown error %d",res);
      return;      
  }
  
  //this->pin_->setup();
  //this->pin_->pin_mode(gpio::FLAG_OUTPUT);
  //this->pin_->digital_write(false);

  this->target_time_ = 0;
  //there's an extra delay somewhere, maybe the first call to get_data()
  //I don't think the timing of the leading space is  critical though
  this->space_(4700-2200);
  for (uint32_t i = 0; i < send_times; i++) {
    //InterruptLock lock;
    for (int32_t item : this->temp_.get_data()) {
      if (item > 0) {
        const auto length = uint32_t(item);
        this->mark_(length);
      } else {
        const auto length = uint32_t(-item);
        this->space_(length);
      }
      App.feed_wdt();
    }
    if (i + 1 < send_times && send_wait>0)
      this->space_(send_wait);
  }
  this->space_(2000);
  this->await_target_time_();
  
  if(CMT2300A_GoStby()) {
    //ESP_LOGD(TAG,"go stby ok");
  } else {
    ESP_LOGE(TAG,"go stby error");
  }  
}

}  // namespace tuya_rf
}  // namespace esphome

#endif
