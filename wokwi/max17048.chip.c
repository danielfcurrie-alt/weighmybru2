#include "wokwi-api.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
  uint8_t selected_reg;
  uint8_t read_index;
  uint8_t write_index;
  uint32_t voltage_attr;
  uint32_t soc_attr;
} chip_state_t;

static uint16_t max17048_register_value(chip_state_t *chip) {
  switch (chip->selected_reg) {
    case 0x02: {
      const float voltage = attr_read_float(chip->voltage_attr);
      const float clamped = fmaxf(2.5f, fminf(5.0f, voltage));
      const uint16_t counts = (uint16_t)lroundf(clamped / 0.00125f);
      return (uint16_t)(counts << 4);
    }
    case 0x04: {
      const float soc = attr_read_float(chip->soc_attr);
      const float clamped = fmaxf(0.0f, fminf(110.0f, soc));
      const uint8_t whole = (uint8_t)clamped;
      const uint8_t fractional = (uint8_t)lroundf((clamped - whole) * 256.0f);
      return ((uint16_t)whole << 8) | fractional;
    }
    case 0x08:
      return 0x0012;
    default:
      return 0x0000;
  }
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (!read) {
    chip->write_index = 0;
  }
  chip->read_index = 0;
  return address == 0x36;
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  const uint16_t value = max17048_register_value(chip);
  const uint8_t byte = (chip->read_index == 0) ? (uint8_t)(value >> 8) : (uint8_t)(value & 0xff);
  chip->read_index++;
  return byte;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (chip->write_index == 0) {
    chip->selected_reg = data;
  }
  chip->write_index++;
  return true;
}

static void on_i2c_disconnect(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->write_index = 0;
}

void chip_init(void) {
  chip_state_t *chip = (chip_state_t *)calloc(1, sizeof(chip_state_t));
  chip->selected_reg = 0x00;
  chip->voltage_attr = attr_init_float("voltage", 3.90f);
  chip->soc_attr = attr_init_float("soc", 70.0f);

  const i2c_config_t i2c = {
    .address = 0x36,
    .scl = pin_init("SCL", INPUT_PULLUP),
    .sda = pin_init("SDA", INPUT_PULLUP),
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
    .user_data = chip,
  };
  i2c_init(&i2c);
}
