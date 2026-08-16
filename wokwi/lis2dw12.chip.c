#include "wokwi-api.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint8_t selected_reg;
  uint8_t read_index;
  uint8_t write_index;
  uint8_t regs[256];
  uint32_t ax_attr;
  uint32_t ay_attr;
  uint32_t az_attr;
} chip_state_t;

static int16_t accel_raw(uint32_t attr_id) {
  const float g = fmaxf(-4.0f, fminf(4.0f, attr_read_float(attr_id)));
  return (int16_t)lroundf(g * 4096.0f);
}

static uint8_t read_register(chip_state_t *chip, uint8_t reg) {
  switch (reg) {
    case 0x0f:
      return 0x44; // LIS2DW12 WHO_AM_I
    case 0x27:
      return 0x01; // STATUS: new data available
    case 0x28: {
      const int16_t value = accel_raw(chip->ax_attr);
      return (uint8_t)(value & 0xff);
    }
    case 0x29: {
      const int16_t value = accel_raw(chip->ax_attr);
      return (uint8_t)((value >> 8) & 0xff);
    }
    case 0x2a: {
      const int16_t value = accel_raw(chip->ay_attr);
      return (uint8_t)(value & 0xff);
    }
    case 0x2b: {
      const int16_t value = accel_raw(chip->ay_attr);
      return (uint8_t)((value >> 8) & 0xff);
    }
    case 0x2c: {
      const int16_t value = accel_raw(chip->az_attr);
      return (uint8_t)(value & 0xff);
    }
    case 0x2d: {
      const int16_t value = accel_raw(chip->az_attr);
      return (uint8_t)((value >> 8) & 0xff);
    }
    default:
      return chip->regs[reg];
  }
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (!read) {
    chip->write_index = 0;
  }
  chip->read_index = 0;
  return address == 0x19 || address == 0x18;
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  const uint8_t reg = chip->selected_reg + chip->read_index;
  chip->read_index++;
  return read_register(chip, reg);
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (chip->write_index == 0) {
    chip->selected_reg = data & 0x7f;
  } else {
    chip->regs[chip->selected_reg++] = data;
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
  chip->ax_attr = attr_init_float("ax", 0.0f);
  chip->ay_attr = attr_init_float("ay", 0.0f);
  chip->az_attr = attr_init_float("az", 1.0f);

  const i2c_config_t i2c = {
    .address = 0x19,
    .scl = pin_init("SCL", INPUT_PULLUP),
    .sda = pin_init("SDA", INPUT_PULLUP),
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
    .user_data = chip,
  };
  i2c_init(&i2c);

  pin_t int1 = pin_init("INT1", OUTPUT_LOW);
  pin_write(int1, LOW);
  (void)pin_init("VCC", INPUT);
  (void)pin_init("GND", INPUT);
}
