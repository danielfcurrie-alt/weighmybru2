// LIS2DW12 accelerometer model for WMB+ TinyS3[D] Wokwi testing.
//
// MODELLED:
//   0x0F WHO_AM_I              read 0x44
//   0x20..0x25 CTRL1..CTRL6   r/w, round-trip
//   0x21 CTRL2.SOFT_RESET     write bit 6 resets user registers
//   0x21 CTRL2.IF_ADD_INC     controls sequential reads, as does I2C subaddr bit 7
//   0x25 CTRL6.FS             selects +/-2/4/8/16 g output scaling
//   0x26 OUT_T                signed 8-bit temperature proxy, 1 C/LSB from 25 C
//   0x27 STATUS               data-ready from dataReady control
//   0x28..0x2D OUT_X/Y/Z      little-endian signed acceleration samples
//   0x2E..0x3F FIFO/event/config registers round-trip, with STATUS_DUP and
//                               ALL_INT_SRC reflecting data-ready
//   INT1 pin                  active high when CTRL4 routes data-ready to INT1
//
//   pattern control           deterministic quiet/vibration/knock/cup/double-tap
//
// NOT MODELLED: real ODR timing/noise, FIFO depth, filters, hardware tap/
// freefall/6D engines, wake-up thresholds, self-test, offset correction, power
// draw, or the second I2C address strap. Patterns are test waveforms, not a
// mechanical model of the finished enclosure.
#include "wokwi-api.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REG_WHO_AM_I 0x0F
#define REG_OUT_T_H 0x0E
#define REG_CTRL1 0x20
#define REG_CTRL2 0x21
#define REG_CTRL3 0x22
#define REG_CTRL4_INT1_PAD_CTRL 0x23
#define REG_CTRL5_INT2_PAD_CTRL 0x24
#define REG_CTRL6 0x25
#define REG_OUT_T 0x26
#define REG_STATUS 0x27
#define REG_OUT_X_L 0x28
#define REG_OUT_X_H 0x29
#define REG_OUT_Y_L 0x2A
#define REG_OUT_Y_H 0x2B
#define REG_OUT_Z_L 0x2C
#define REG_OUT_Z_H 0x2D
#define REG_FIFO_CTRL 0x2E
#define REG_FIFO_SAMPLES 0x2F
#define REG_TAP_THS_X 0x30
#define REG_CTRL7 0x3F

#define CTRL2_IF_ADD_INC 0x04
#define CTRL2_SOFT_RESET 0x40
#define STATUS_ZYXDA 0x01
#define STATUS_DUP_DRDY 0x01
#define ALL_INT_SRC_DRDY 0x01
#define CTRL4_INT1_DRDY 0x01

typedef struct {
  uint8_t selected_reg;
  uint8_t read_index;
  uint8_t write_index;
  bool auto_increment;
  uint8_t regs[256];
  uint32_t ax_attr;
  uint32_t ay_attr;
  uint32_t az_attr;
  uint32_t temp_attr;
  uint32_t data_ready_attr;
  uint32_t pattern_attr;
  uint32_t amplitude_attr;
  pin_t int1_pin;
} chip_state_t;

static float clampf_local(float value, float minimum, float maximum) {
  return fmaxf(minimum, fminf(maximum, value));
}

static void load_reset_defaults(chip_state_t *chip) {
  memset(chip->regs, 0, sizeof(chip->regs));
  chip->regs[REG_CTRL1] = 0x00;
  chip->regs[REG_CTRL2] = CTRL2_IF_ADD_INC;
  chip->regs[REG_CTRL3] = 0x00;
  chip->regs[REG_CTRL4_INT1_PAD_CTRL] = 0x00;
  chip->regs[REG_CTRL5_INT2_PAD_CTRL] = 0x00;
  chip->regs[REG_CTRL6] = 0x00;
  chip->regs[REG_FIFO_CTRL] = 0x00;
  for (uint8_t reg = REG_TAP_THS_X; reg <= REG_CTRL7; reg++) {
    chip->regs[reg] = 0x00;
  }
}

static bool data_ready(chip_state_t *chip) {
  return attr_read_float(chip->data_ready_attr) >= 0.5f;
}

static void update_int1(chip_state_t *chip) {
  const bool routed = (chip->regs[REG_CTRL4_INT1_PAD_CTRL] & CTRL4_INT1_DRDY) != 0;
  pin_write(chip->int1_pin, (routed && data_ready(chip)) ? HIGH : LOW);
}

static float pulse(float age_ms, float width_ms) {
  if (age_ms < 0.0f || age_ms > width_ms) {
    return 0.0f;
  }
  const float normalized = age_ms / width_ms;
  return normalized < 0.5f ? normalized * 2.0f : (1.0f - normalized) * 2.0f;
}

static float pattern_offset(chip_state_t *chip, uint8_t axis) {
  const int pattern = (int)lroundf(attr_read_float(chip->pattern_attr));
  const float amplitude = clampf_local(attr_read_float(chip->amplitude_attr), 0.0f, 2.0f);
  const float time_ms = (float)(get_sim_nanos() / 1000000ULL);
  const float time_s = time_ms / 1000.0f;
  const float pi = 3.14159265358979323846f;

  switch (pattern) {
    case 1: // quiet sensor floor
      return axis == 0 ? 0.006f * amplitude * sinf(2.0f * pi * 1.7f * time_s) : 0.0f;
    case 2: // sustained machine vibration
      if (axis == 0) {
        return amplitude * sinf(2.0f * pi * 19.0f * time_s);
      }
      if (axis == 1) {
        return amplitude * 0.45f * sinf(2.0f * pi * 23.0f * time_s + 0.8f);
      }
      return amplitude * 0.20f * sinf(2.0f * pi * 17.0f * time_s + 1.4f);
    case 3: { // one knock every 1.5 seconds
      const float phase_ms = fmodf(time_ms, 1500.0f);
      const float value = amplitude * pulse(phase_ms - 350.0f, 45.0f);
      return axis == 0 ? value : (axis == 2 ? value * 0.35f : 0.0f);
    }
    case 4: { // cup placement with a damped 11 Hz ring
      const float phase_ms = fmodf(time_ms, 3000.0f);
      const float age_ms = phase_ms - 400.0f;
      if (age_ms < 0.0f || age_ms > 700.0f) {
        return 0.0f;
      }
      const float ring = amplitude * expf(-age_ms / 230.0f) *
                         sinf(2.0f * pi * 11.0f * age_ms / 1000.0f);
      return axis == 0 ? ring : (axis == 2 ? ring * 0.30f : 0.0f);
    }
    case 5: { // two short knocks separated by 250 ms
      const float phase_ms = fmodf(time_ms, 2000.0f);
      const float value = amplitude * (pulse(phase_ms - 400.0f, 45.0f) +
                                       pulse(phase_ms - 650.0f, 45.0f));
      return axis == 0 ? value : (axis == 2 ? value * 0.35f : 0.0f);
    }
    default:
      return 0.0f;
  }
}

static int16_t axis_raw(chip_state_t *chip, uint32_t attr_id, uint8_t axis) {
  const uint8_t fs = (chip->regs[REG_CTRL6] >> 4) & 0x03;
  const float full_scale_g = (float)(2 << fs);
  const float counts_per_g = 4096.0f / (float)(1 << fs);
  const float max_g = full_scale_g - (1.0f / counts_per_g);
  const float g = clampf_local(attr_read_float(attr_id) + pattern_offset(chip, axis),
                               -full_scale_g, max_g);
  // High-performance samples are 14-bit values left-aligned in 16-bit output.
  return (int16_t)(lroundf(g * counts_per_g) * 4);
}

static uint8_t axis_byte(chip_state_t *chip, uint32_t attr_id, uint8_t axis, bool high_byte) {
  const int16_t value = axis_raw(chip, attr_id, axis);
  return high_byte ? (uint8_t)((value >> 8) & 0xff) : (uint8_t)(value & 0xff);
}

static uint8_t read_register(chip_state_t *chip, uint8_t reg) {
  switch (reg) {
    case REG_WHO_AM_I:
      return 0x44;
    case REG_OUT_T_H:
    case REG_OUT_T: {
      const float temp_c = clampf_local(attr_read_float(chip->temp_attr), -40.0f, 85.0f);
      return (uint8_t)(int8_t)lroundf(temp_c - 25.0f);
    }
    case REG_STATUS:
      return data_ready(chip) ? STATUS_ZYXDA : 0x00;
    case REG_OUT_X_L:
      return axis_byte(chip, chip->ax_attr, 0, false);
    case REG_OUT_X_H:
      return axis_byte(chip, chip->ax_attr, 0, true);
    case REG_OUT_Y_L:
      return axis_byte(chip, chip->ay_attr, 1, false);
    case REG_OUT_Y_H:
      return axis_byte(chip, chip->ay_attr, 1, true);
    case REG_OUT_Z_L:
      return axis_byte(chip, chip->az_attr, 2, false);
    case REG_OUT_Z_H:
      return axis_byte(chip, chip->az_attr, 2, true);
    case REG_FIFO_SAMPLES:
      return 0x00;
    case 0x37: // STATUS_DUP
      return data_ready(chip) ? STATUS_DUP_DRDY : 0x00;
    case 0x3B: // ALL_INT_SRC
      return data_ready(chip) ? ALL_INT_SRC_DRDY : 0x00;
    default:
      return chip->regs[reg];
  }
}

static void write_register(chip_state_t *chip, uint8_t reg, uint8_t value) {
  if (reg == REG_WHO_AM_I || reg == REG_OUT_T || reg == REG_STATUS ||
      (reg >= REG_OUT_X_L && reg <= REG_OUT_Z_H) || reg == REG_FIFO_SAMPLES) {
    return;
  }

  chip->regs[reg] = value;
  if (reg == REG_CTRL2 && (value & CTRL2_SOFT_RESET)) {
    load_reset_defaults(chip);
  }
  update_int1(chip);
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (!read) {
    chip->write_index = 0;
  }
  chip->read_index = 0;
  return address == 0x19;
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  const uint8_t reg = chip->auto_increment
      ? (uint8_t)(chip->selected_reg + chip->read_index)
      : chip->selected_reg;
  chip->read_index++;
  update_int1(chip);
  return read_register(chip, reg);
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (chip->write_index == 0) {
    chip->selected_reg = data & 0x7f;
    chip->auto_increment = (data & 0x80) || (chip->regs[REG_CTRL2] & CTRL2_IF_ADD_INC);
  } else {
    write_register(chip, chip->selected_reg, data);
    if (chip->auto_increment) {
      chip->selected_reg++;
    }
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
  chip->temp_attr = attr_init_float("tempC", 25.0f);
  chip->data_ready_attr = attr_init_float("dataReady", 1.0f);
  chip->pattern_attr = attr_init_float("pattern", 0.0f);
  chip->amplitude_attr = attr_init_float("amplitudeG", 0.65f);
  chip->int1_pin = pin_init("INT1", OUTPUT_LOW);
  load_reset_defaults(chip);

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

  update_int1(chip);
  (void)pin_init("VCC", INPUT);
  (void)pin_init("GND", INPUT);
}
