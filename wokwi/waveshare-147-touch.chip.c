// Protocol-level model for Waveshare's 1.47-inch Touch LCD.
//
// MODELLED:
//   JD9853 SPI byte framing, reset, backlight, and core initialization commands
//   AXS5106L I2C address 0x63 and two-point coordinate registers
//
// NOT MODELLED:
//   Pixel rendering, LCD readback, touch pressure, gestures, electrical timing,
//   controller power consumption, or analog behavior.
#include "wokwi-api.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define AXS5106_ADDRESS 0x63

typedef struct {
  pin_t sclk;
  pin_t mosi;
  pin_t miso;
  pin_t lcd_reset;
  pin_t lcd_dc;
  pin_t lcd_cs;
  pin_t lcd_backlight;
  pin_t touch_reset;
  pin_t touch_interrupt;

  uint8_t spi_byte;
  uint8_t spi_bit_count;
  uint8_t current_command;
  uint32_t spi_byte_count;
  bool selected;
  bool sleep_out;
  bool display_on;
  bool rgb565;

  uint8_t selected_register;
  uint8_t i2c_read_index;
  uint8_t i2c_write_index;
  uint32_t touch_count_attr;
  uint32_t touch1_x_attr;
  uint32_t touch1_y_attr;
  uint32_t touch2_x_attr;
  uint32_t touch2_y_attr;
} chip_state_t;

static uint16_t clamp_coordinate(float value, uint16_t maximum) {
  if (value <= 0.0f) {
    return 0;
  }
  if (value >= (float)maximum) {
    return maximum;
  }
  return (uint16_t)value;
}

static uint8_t touch_count(chip_state_t *chip) {
  const float value = attr_read_float(chip->touch_count_attr);
  if (value < 0.5f) {
    return 0;
  }
  return value < 1.5f ? 1 : 2;
}

static uint16_t touch_x(chip_state_t *chip, uint8_t point) {
  const uint32_t attr = point == 0 ? chip->touch1_x_attr : chip->touch2_x_attr;
  return clamp_coordinate(attr_read_float(attr), 171);
}

static uint16_t touch_y(chip_state_t *chip, uint8_t point) {
  const uint32_t attr = point == 0 ? chip->touch1_y_attr : chip->touch2_y_attr;
  return clamp_coordinate(attr_read_float(attr), 319);
}

static void update_touch_interrupt(chip_state_t *chip) {
  // The Waveshare demo treats TP_INT as active low.
  pin_write(chip->touch_interrupt, touch_count(chip) > 0 ? LOW : HIGH);
}

static void reset_lcd_state(chip_state_t *chip) {
  chip->spi_byte = 0;
  chip->spi_bit_count = 0;
  chip->current_command = 0;
  chip->spi_byte_count = 0;
  chip->sleep_out = false;
  chip->display_on = false;
  chip->rgb565 = false;
}

static void accept_spi_byte(chip_state_t *chip, uint8_t value) {
  chip->spi_byte_count++;
  if (pin_read(chip->lcd_dc) == LOW) {
    chip->current_command = value;
    if (value == 0x11) {
      chip->sleep_out = true;
    } else if (value == 0x28) {
      chip->display_on = false;
    } else if (value == 0x29) {
      chip->display_on = true;
    }
    return;
  }

  if (chip->current_command == 0x3A) {
    chip->rgb565 = value == 0x05;
  }
}

static void on_lcd_cs_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->selected = value == LOW;
  if (!chip->selected) {
    chip->spi_byte = 0;
    chip->spi_bit_count = 0;
  }
}

static void on_lcd_clock_rise(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  (void)value;
  chip_state_t *chip = (chip_state_t *)user_data;
  if (!chip->selected || pin_read(chip->lcd_reset) == LOW) {
    return;
  }

  chip->spi_byte = (uint8_t)((chip->spi_byte << 1) |
                             (pin_read(chip->mosi) == HIGH ? 1 : 0));
  chip->spi_bit_count++;
  if (chip->spi_bit_count == 8) {
    accept_spi_byte(chip, chip->spi_byte);
    chip->spi_byte = 0;
    chip->spi_bit_count = 0;
  }
}

static void on_lcd_reset_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  if (value == LOW) {
    reset_lcd_state((chip_state_t *)user_data);
  }
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->i2c_read_index = 0;
  if (!read) {
    chip->i2c_write_index = 0;
  }
  update_touch_interrupt(chip);
  return address == AXS5106_ADDRESS && pin_read(chip->touch_reset) == HIGH;
}

static uint8_t touch_register(chip_state_t *chip, uint8_t reg) {
  const uint16_t x1 = touch_x(chip, 0);
  const uint16_t y1 = touch_y(chip, 0);
  const uint16_t x2 = touch_x(chip, 1);
  const uint16_t y2 = touch_y(chip, 1);
  switch (reg) {
    case 0x02: return touch_count(chip);
    case 0x03: return (uint8_t)((x1 >> 8) & 0x0f);
    case 0x04: return (uint8_t)x1;
    case 0x05: return (uint8_t)((y1 >> 8) & 0x0f);
    case 0x06: return (uint8_t)y1;
    case 0x09: return (uint8_t)((x2 >> 8) & 0x0f);
    case 0x0A: return (uint8_t)x2;
    case 0x0B: return (uint8_t)((y2 >> 8) & 0x0f);
    case 0x0C: return (uint8_t)y2;
    default: return 0;
  }
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  const uint8_t value = touch_register(chip,
      (uint8_t)(chip->selected_register + chip->i2c_read_index));
  chip->i2c_read_index++;
  update_touch_interrupt(chip);
  return value;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (chip->i2c_write_index == 0) {
    chip->selected_register = data;
  }
  chip->i2c_write_index++;
  return true;
}

static void on_i2c_disconnect(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->i2c_write_index = 0;
  update_touch_interrupt(chip);
}

void chip_init(void) {
  chip_state_t *chip = (chip_state_t *)calloc(1, sizeof(chip_state_t));

  chip->touch_count_attr = attr_init_float("touchCount", 0.0f);
  chip->touch1_x_attr = attr_init_float("touch1X", 86.0f);
  chip->touch1_y_attr = attr_init_float("touch1Y", 160.0f);
  chip->touch2_x_attr = attr_init_float("touch2X", 43.0f);
  chip->touch2_y_attr = attr_init_float("touch2Y", 80.0f);

  chip->touch_reset = pin_init("TP_RST", INPUT_PULLUP);
  const pin_t touch_sda = pin_init("TP_SDA", INPUT_PULLUP);
  const pin_t touch_scl = pin_init("TP_SCL", INPUT_PULLUP);
  chip->touch_interrupt = pin_init("TP_INT", OUTPUT_HIGH);
  chip->lcd_backlight = pin_init("LCD_BL", INPUT);
  chip->lcd_reset = pin_init("LCD_RST", INPUT_PULLUP);
  chip->lcd_cs = pin_init("LCD_CS", INPUT_PULLUP);
  chip->lcd_dc = pin_init("LCD_DC", INPUT);
  chip->miso = pin_init("MISO", OUTPUT_LOW);
  chip->mosi = pin_init("MOSI", INPUT);
  chip->sclk = pin_init("SCLK", INPUT);
  (void)pin_init("GND", INPUT);
  (void)pin_init("VCC", INPUT);
  (void)chip->lcd_backlight;
  (void)chip->miso;

  reset_lcd_state(chip);
  update_touch_interrupt(chip);

  const pin_watch_config_t cs_watch = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_lcd_cs_change,
  };
  pin_watch(chip->lcd_cs, &cs_watch);

  const pin_watch_config_t clock_watch = {
    .user_data = chip,
    .edge = RISING,
    .pin_change = on_lcd_clock_rise,
  };
  pin_watch(chip->sclk, &clock_watch);

  const pin_watch_config_t reset_watch = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_lcd_reset_change,
  };
  pin_watch(chip->lcd_reset, &reset_watch);

  const i2c_config_t i2c = {
    .address = AXS5106_ADDRESS,
    .scl = touch_scl,
    .sda = touch_sda,
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
    .user_data = chip,
  };
  i2c_init(&i2c);
}
