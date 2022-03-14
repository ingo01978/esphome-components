#pragma once

#include "esphome/core/component.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/display/display_buffer.h"

namespace esphome {
namespace it8951e {

typedef struct
{
    uint16_t usPanelW;
    uint16_t usPanelH;
    uint16_t usImgBufAddrL;
    uint16_t usImgBufAddrH;
    uint16_t usFWVersion[8];   //16 Bytes String
    uint16_t usLUTVersion[8];   //16 Bytes String
}IT8951DevInfo;

typedef struct IT8951AreaImgInfo
{
    uint16_t usX;
    uint16_t usY;
    uint16_t usWidth;
    uint16_t usHeight;
}IT8951AreaImgInfo;

typedef struct IT8951LdImgInfo
{
    uint16_t usEndianType; //little or Big Endian
    uint16_t usPixelFormat; //bpp
    uint16_t usRotate; //Rotate mode
    uint32_t ulStartFBAddr; //Start address of source Frame buffer
    uint32_t ulImgBufBaseAddr;//Base address of target image buffer
    
}IT8951LdImgInfo;


class it8951e : public PollingComponent,
                        public display::DisplayBuffer,
                        public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                              spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_2MHZ> {
 public:
  float get_setup_priority() const override;
  void set_reset_pin(GPIOPin *reset) { this->reset_pin_ = reset; }
  void set_busy_pin(GPIOPin *busy) { this->busy_pin_ = busy; }
  void set_en_pin(GPIOPin *en) { this->en_pin_ = en; }

  void display();
  void initialize();
  void deep_sleep();

  void update() override;

  void fill(Color color) override;

  void setup() override {
    this->setup_pins_();
    this->initialize();
  }

  void on_safe_shutdown() override;

 protected:
  void draw_absolute_pixel_internal(int x, int y, Color color) override;

  bool wait_until_idle_();

  void setup_pins_();

  void reset_() {
    if (this->reset_pin_ != nullptr) {
      this->reset_pin_->digital_write(false);
      delay(500);  // NOLINT
      this->reset_pin_->digital_write(true);
      delay(200);  // NOLINT
    }
  }

  uint32_t get_buffer_length_();

  GPIOPin *reset_pin_{nullptr};
  GPIOPin *cs_pin_;
  GPIOPin *busy_pin_{nullptr};
  GPIOPin *en_pin_{nullptr};
  virtual int idle_timeout_() { return 1000; }  // NOLINT(readability-identifier-naming)

  IT8951DevInfo gstI80DevInfo;
  uint8_t* gpFrameBuf;
  uint32_t gulImgBufAddr;
};

}  // namespace it8951e
}  // namespace esphome