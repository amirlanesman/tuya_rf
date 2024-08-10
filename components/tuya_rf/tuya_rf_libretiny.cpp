#include "tuya_rf.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "radio.h"
#ifdef USE_LIBRETINY

namespace esphome {
namespace tuya_rf {

static const char *const TAG = "tuya_rf";

void IRAM_ATTR HOT RemoteReceiverComponentStore::gpio_intr(RemoteReceiverComponentStore *arg) {
  const uint32_t now = micros();
  // If the lhs is 1 (rising edge) we should write to an uneven index and vice versa
  const uint32_t next = (arg->buffer_write_at + 1) % arg->buffer_size;
  const bool level = arg->pin.digital_read();
  if (level != next % 2)
    return;

  // If next is buffer_read, we have hit an overflow
  if (next == arg->buffer_read_at)
    return;

  const uint32_t last_change = arg->buffer[arg->buffer_write_at];
  const uint32_t time_since_change = now - last_change;
  if (time_since_change <= arg->filter_us)
    return;

  arg->buffer[arg->buffer_write_at = next] = now;
}

void TuyaRfComponent::setup() {
  this->RemoteTransmitterBase::pin_->setup();
  this->RemoteTransmitterBase::pin_->digital_write(false);
  if (this->receiver_disabled_) {
    return;
  }
  this->RemoteReceiverBase::pin_->setup();
  
  auto &s = this->store_;
  s.filter_us = this->filter_us_;
  s.pin = this->RemoteReceiverBase::pin_->to_isr();
  s.buffer_size = this->buffer_size_;

  this->high_freq_.start();
  if (s.buffer_size % 2 != 0) {
    // Make sure divisible by two. This way, we know that every 0bxxx0 index is a space and every 0bxxx1 index is a mark
    s.buffer_size++;
  }

  s.buffer = new uint32_t[s.buffer_size];
  void *buf = (void *) s.buffer;
  memset(buf, 0, s.buffer_size * sizeof(uint32_t));

  StartRx();
  
  // First index is a space.
  if (this->RemoteReceiverBase::pin_->digital_read()) {
    s.buffer_write_at = s.buffer_read_at = 1;
  } else {
    s.buffer_write_at = s.buffer_read_at = 0;
  }
  
  this->RemoteReceiverBase::pin_->attach_interrupt(RemoteReceiverComponentStore::gpio_intr, &this->store_, gpio::INTERRUPT_ANY_EDGE);
  
}

void TuyaRfComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Tuya Rf:");
  LOG_PIN("  Tx Pin: ",this->RemoteTransmitterBase::pin_);
  if (this->receiver_disabled_) {
    ESP_LOGCONFIG(TAG, "  Receiver disabled");
    return;
  }
  LOG_PIN("  Rx Pin: ", this->RemoteReceiverBase::pin_);
  if (this->RemoteReceiverBase::pin_->digital_read()) {
    ESP_LOGW(TAG, "Remote Receiver Signal starts with a HIGH value. Usually this means you have to "
                  "invert the signal using 'inverted: True' in the pin schema!");
  }
  ESP_LOGCONFIG(TAG, "  Buffer Size: %u", this->buffer_size_);
  ESP_LOGCONFIG(TAG, "  Tolerance: %u%s", this->tolerance_,
                (this->tolerance_mode_ == remote_base::TOLERANCE_MODE_TIME) ? " us" : "%");
  ESP_LOGCONFIG(TAG, "  Filter out pulses shorter than: %u us", this->filter_us_);
  ESP_LOGCONFIG(TAG, "  Signal is done after %u us of no changes", this->idle_us_);
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
  this->RemoteTransmitterBase::pin_->digital_write(false);
  this->target_time_ += usec;
}

void TuyaRfComponent::space_(uint32_t usec) {
  this->await_target_time_();
  this->RemoteTransmitterBase::pin_->digital_write(true);
  this->target_time_ += usec;
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
  
  this->RemoteTransmitterBase::pin_->digital_write(false);
  
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
  
  this->RemoteTransmitterBase::pin_->digital_write(true);

  this->target_time_ = 0;
  //there's an extra delay somewhere, maybe the first call to get_data()
  //I don't think the timing of the leading space is  critical though
  this->space_(4700-2200);
  for (uint32_t i = 0; i < send_times; i++) {
    //InterruptLock lock;
    for (int32_t item : this->RemoteTransmitterBase::temp_.get_data()) {
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
  
  if (this->receiver_disabled_) {
    if(CMT2300A_GoStby()) {
      //ESP_LOGD(TAG,"go stby ok");
    } else {
      ESP_LOGE(TAG,"go stby error");
    } 
  } else {
    //Go back to rx mode
    StartRx();
  } 
}

void TuyaRfComponent::loop() {
  if (this->receiver_disabled_) {
    return;
  }  
  auto &s = this->store_;

  // copy write at to local variables, as it's volatile
  const uint32_t write_at = s.buffer_write_at;
  const uint32_t dist = (s.buffer_size + write_at - s.buffer_read_at) % s.buffer_size;
  // signals must at least one rising and one leading edge
  if (dist <= 1)
    return;
  const uint32_t now = micros();
  if (now - s.buffer[write_at] < this->idle_us_) {
    // The last change was fewer than the configured idle time ago.
    return;
  }

  ESP_LOGVV(TAG, "read_at=%u write_at=%u dist=%u now=%u end=%u", s.buffer_read_at, write_at, dist, now,
            s.buffer[write_at]);

  // Skip first value, it's from the previous idle level
  s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
  uint32_t prev = s.buffer_read_at;
  s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
  const uint32_t reserve_size = 1 + (s.buffer_size + write_at - s.buffer_read_at) % s.buffer_size;
  this->RemoteReceiverBase::temp_.clear();
  this->RemoteReceiverBase::temp_.reserve(reserve_size);
  int32_t multiplier = s.buffer_read_at % 2 == 0 ? 1 : -1;

  for (uint32_t i = 0; prev != write_at; i++) {
    int32_t delta = s.buffer[s.buffer_read_at] - s.buffer[prev];
    if (uint32_t(delta) >= this->idle_us_) {
      // already found a space longer than idle. There must have been two pulses
      break;
    }

    ESP_LOGVV(TAG, "  i=%u buffer[%u]=%u - buffer[%u]=%u -> %d", i, s.buffer_read_at, s.buffer[s.buffer_read_at], prev,
              s.buffer[prev], multiplier * delta);
    this->RemoteReceiverBase::temp_.push_back(multiplier * delta);
    prev = s.buffer_read_at;
    s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
    multiplier *= -1;
  }
  s.buffer_read_at = (s.buffer_size + s.buffer_read_at - 1) % s.buffer_size;
  this->RemoteReceiverBase::temp_.push_back(this->idle_us_ * multiplier);

  this->call_listeners_dumpers_();
}

}  // namespace tuya_rf
}  // namespace esphome

#endif
