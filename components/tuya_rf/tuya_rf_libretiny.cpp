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
  const bool level = !arg->pin.digital_read();
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
  
  // First index is a space (signal is inverted)
  if (!this->RemoteReceiverBase::pin_->digital_read()) {
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
  //probably the warning isn't usefule due to the noisy signal
  if (!this->RemoteReceiverBase::pin_->digital_read()) {
    ESP_LOGW(TAG, "Remote Receiver Signal starts with a HIGH value. Usually this means you have to "
                  "invert the signal using 'inverted: True' in the pin schema!");
    ESP_LOGW(TAG, "It could also be that the signal is noisy.");
  }
  ESP_LOGCONFIG(TAG, "  Buffer Size: %u", this->buffer_size_);
  ESP_LOGCONFIG(TAG, "  Tolerance: %u%s", this->tolerance_,
                (this->tolerance_mode_ == remote_base::TOLERANCE_MODE_TIME) ? " us" : "%");
  ESP_LOGCONFIG(TAG, "  Filter out pulses shorter than: %u us", this->filter_us_);
  ESP_LOGCONFIG(TAG, "  Signal start with a pulse between %u and %u us", this->start_pulse_min_us_, this->start_pulse_max_us_);
  ESP_LOGCONFIG(TAG, "  Signal is done after a pulse of %u us", this->end_pulse_us_);
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

/*
The rf input is quite noisy, so some heavy filtering must be done:

  - the signal is inverted (the pin gives 0 for mark, 1 for
    space, with the inversion the space is 0 and pulse is 1).

  - the starting pulse must be more than a specified duration
    (start_pulse_min_us_) but less than start_pulse_max_us_
    The default values work with my remote, I don't know if 
    they are valid for other remotes.

  - the cmt2300a gives a very long pulse (90ms) at the end of a
    reception, the parameter to detect it is end_pulse_us_.
*/
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

  //stop the reception if the end pulse never arrives
  if (receive_started_ && dist >= s.buffer_size - 5) {
    ESP_LOGD(TAG,"Buffer overflow, restarting reception");
    receive_started_=false;
    receive_end_=false;
    #if 0
    uint32_t prev = s.buffer_read_at;
    s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
    int32_t multiplier = s.buffer_read_at % 2 == 0 ? 1 : -1;
    for (uint32_t i = 0; prev != write_at; i++) {
      int32_t delta = s.buffer[s.buffer_read_at] - s.buffer[prev];

      ESP_LOGD(TAG, "  i=%u buffer[%u]=%u - buffer[%u]=%u -> %d", i, s.buffer_read_at, s.buffer[s.buffer_read_at], prev,
                s.buffer[prev], delta * multiplier);
      prev = s.buffer_read_at;
      s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
      multiplier *= -1;
    }
    #endif
    s.buffer_read_at = s.buffer_write_at;
    old_write_at_ = s.buffer_read_at;
    return;
  }

  //now we check all the available data in the buffer from the old position read
  uint32_t new_write_at = old_write_at_;
  while (new_write_at != write_at) {
    uint32_t prev;
    if (new_write_at==0) {
      prev=s.buffer_size-1;
    } else {
      prev=new_write_at-1;
    }
    uint32_t diff=s.buffer[new_write_at]-s.buffer[prev];
    //reception starts and ends with a pulse (transition to low, new value is 0)
    if (new_write_at % 2 == 0) {
      //check if it's a start or end pulse
      if (diff>=this->start_pulse_min_us_) {
        if (diff >= this->end_pulse_us_) {
          //it's a probable end pulse
          if (receive_started_) {
            receive_end_=true;
            new_write_at=prev;
            break;
          } else {
            ESP_LOGD(TAG, "Ignoring too long pulse %u",diff);
          }
        } else if (diff<this->start_pulse_max_us_) {
          //it's a new start pulse, discard old data and start again
          ESP_LOGD(TAG, "Long pulse %u, start reception",diff);
          s.buffer_read_at=prev;
          receive_started_=true;
          receive_end_=false;
        } else {
          ESP_LOGD(TAG, "Ignoring too long pulse (2) %u",diff);
        }
      }
    } else if (receive_started_ && diff>=this->start_pulse_min_us_) {
      //pauses can never be longer than the start pulse
      ESP_LOGD(TAG, "Long pause (%u) during reception, restarting",diff);
      receive_started_=false;
    }
    if (!receive_started_) {
      s.buffer_read_at=prev;
    }
    new_write_at = (new_write_at + 1) % s.buffer_size;
  }
  old_write_at_ = new_write_at;

  if (!receive_end_) {
    return;
  }

  //here we have a supposedly valid sequence
  const uint32_t now = micros();

  receive_started_=false;
  receive_end_=false;

  ESP_LOGD(TAG, "read_at=%u write_at=%u dist=%u now=%u end=%u", s.buffer_read_at, new_write_at, dist, now,
            s.buffer[new_write_at]);

  uint32_t prev = s.buffer_read_at;
  s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
  const uint32_t reserve_size = 1 + (s.buffer_size + new_write_at - s.buffer_read_at) % s.buffer_size;
  this->RemoteReceiverBase::temp_.clear();
  this->RemoteReceiverBase::temp_.reserve(reserve_size);
  int32_t multiplier = s.buffer_read_at % 2 == 0 ? 1 : -1;

  for (uint32_t i = 0; prev != new_write_at; i++) {
    int32_t delta = s.buffer[s.buffer_read_at] - s.buffer[prev];

    ESP_LOGVV(TAG, "  i=%u buffer[%u]=%u - buffer[%u]=%u -> %d", i, s.buffer_read_at, s.buffer[s.buffer_read_at], prev,
              s.buffer[prev], multiplier * delta);
    this->RemoteReceiverBase::temp_.push_back(multiplier * delta);
    prev = s.buffer_read_at;
    s.buffer_read_at = (s.buffer_read_at + 1) % s.buffer_size;
    multiplier *= -1;
  }
  s.buffer_read_at = (s.buffer_size + s.buffer_read_at - 1) % s.buffer_size;
  this->RemoteReceiverBase::temp_.push_back(this->end_pulse_us_ * multiplier);

  this->call_listeners_dumpers_();
}

}  // namespace tuya_rf
}  // namespace esphome

#endif
