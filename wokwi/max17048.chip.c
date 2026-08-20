// MAX17048 fuel gauge model for WMB+ TinyS3[D] Wokwi testing.
//
// MODELLED (faithful to the Analog Devices MAX17048/MAX17049 datasheet):
//   0x02 VCELL   read   78.125 uV/LSB  (encoded as (v / 0.00125) << 4)
//   0x04 SOC     read   1/256 % per LSB
//   0x06 MODE    r/w    stored; QuickStart bit re-seeds SOC to the attr value
//   0x08 VERSION read   0x0012
//   0x0A HIBRT   r/w    stored (hibernate thresholds; no behavioural effect)
//   0x0C CONFIG  r/w    RCOMP | SLEEP | ALSC | ALRT | ATHD  -> drives ALRT pin
//   0x14 VALRT   r/w    VALRTMIN/VALRTMAX, 20 mV/LSB        -> drives ALRT pin
//   0x16 CRATE   read   TRUTHFUL: derived from the actual SOC slope,
//                       0.208 %/hr per LSB, signed. Firmware that computes
//                       runtime-to-empty from CRATE can be checked against the
//                       dischargePctPerHour control as ground truth.
//   0x18 VRESET  r/w    stored (no behavioural effect)
//   0x1A STATUS  r/w    RI / VH / VL / HD / SC bits; host clears by writing
//   ALRT pin     open-drain, active low; deasserts when CONFIG.ALRT is cleared
//
// NOT MODELLED: ModelGauge cell learning, real OCV->SOC curve, temperature
// compensation (RCOMP is stored but inert), sleep current, VRESET detach
// detection. SOC follows dischargePctPerHour linearly - it is a test signal,
// not a battery model. Do not use this to validate SOC accuracy claims.
#include "wokwi-api.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define REG_VCELL 0x02
#define REG_SOC 0x04
#define REG_MODE 0x06
#define REG_VERSION 0x08
#define REG_HIBRT 0x0A
#define REG_CONFIG 0x0C
#define REG_VALRT 0x14
#define REG_CRATE 0x16
#define REG_VRESET 0x18
#define REG_STATUS 0x1A

#define STATUS_RI 0x0100  // reset indicator
#define STATUS_VH 0x0200  // voltage high
#define STATUS_VL 0x0400  // voltage low
#define STATUS_HD 0x1000  // SOC low (crossed ATHD)
#define STATUS_SC 0x2000  // SOC changed by >= 1%

#define CONFIG_ALSC 0x0040
#define CONFIG_ALRT 0x0020
#define CONFIG_ATHD 0x001F

typedef struct {
  uint8_t selected_reg;
  uint8_t read_index;
  uint8_t write_index;
  uint16_t write_value;

  uint32_t voltage_attr;
  uint32_t soc_attr;
  uint32_t discharge_attr;

  pin_t alrt_pin;

  uint16_t reg_mode;
  uint16_t reg_hibrt;
  uint16_t reg_config;
  uint16_t reg_valrt;
  uint16_t reg_vreset;
  uint16_t reg_status;

  double soc_pct;          // live SOC, drifts with dischargePctPerHour
  double last_tick_nanos;
  double crate_pct_per_hr; // truthful slope, published in CRATE
  uint8_t last_alert_soc;  // whole-% at last SC alert
} chip_state_t;

static float clampf(float v, float lo, float hi) {
  return fmaxf(lo, fminf(hi, v));
}

static void update_alrt(chip_state_t *chip) {
  const bool asserted = (chip->reg_config & CONFIG_ALRT) != 0;
  // Open-drain, active low: pulled low when an alert is latched.
  pin_write(chip->alrt_pin, asserted ? LOW : HIGH);
}

static void raise_alert(chip_state_t *chip, uint16_t status_bit) {
  chip->reg_status |= status_bit;
  chip->reg_config |= CONFIG_ALRT;
  update_alrt(chip);
}

static void on_tick(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  const double now = (double)get_sim_nanos();
  const double elapsed_hours = (now - chip->last_tick_nanos) / 3.6e12;
  chip->last_tick_nanos = now;

  const double rate = (double)attr_read_float(chip->discharge_attr);
  chip->crate_pct_per_hr = -rate;  // discharging -> negative CRATE

  if (rate != 0.0) {
    chip->soc_pct -= rate * elapsed_hours;
  } else {
    // No drift configured: follow the soc control directly so a scenario can
    // drive SOC with set-control.
    chip->soc_pct = (double)attr_read_float(chip->soc_attr);
  }
  chip->soc_pct = fmax(0.0, fmin(110.0, chip->soc_pct));

  const uint8_t whole = (uint8_t)chip->soc_pct;
  const uint8_t athd = 32 - (uint8_t)(chip->reg_config & CONFIG_ATHD);

  if (whole < athd && !(chip->reg_status & STATUS_HD)) {
    raise_alert(chip, STATUS_HD);
  }
  if ((chip->reg_config & CONFIG_ALSC) && whole != chip->last_alert_soc) {
    chip->last_alert_soc = whole;
    raise_alert(chip, STATUS_SC);
  }

  const float voltage = clampf(attr_read_float(chip->voltage_attr), 2.5f, 5.0f);
  const uint8_t valrt_min = (uint8_t)(chip->reg_valrt >> 8);
  const uint8_t valrt_max = (uint8_t)(chip->reg_valrt & 0xff);
  if (voltage < valrt_min * 0.02f && !(chip->reg_status & STATUS_VL)) {
    raise_alert(chip, STATUS_VL);
  }
  if (voltage > valrt_max * 0.02f && !(chip->reg_status & STATUS_VH)) {
    raise_alert(chip, STATUS_VH);
  }
}

static uint16_t read_register(chip_state_t *chip) {
  switch (chip->selected_reg) {
    case REG_VCELL: {
      const float v = clampf(attr_read_float(chip->voltage_attr), 2.5f, 5.0f);
      return (uint16_t)((uint16_t)lroundf(v / 0.00125f) << 4);
    }
    case REG_SOC: {
      const double soc = fmax(0.0, fmin(110.0, chip->soc_pct));
      const uint8_t whole = (uint8_t)soc;
      const uint8_t frac = (uint8_t)lround((soc - whole) * 256.0);
      return ((uint16_t)whole << 8) | frac;
    }
    case REG_MODE: return chip->reg_mode;
    case REG_VERSION: return 0x0012;
    case REG_HIBRT: return chip->reg_hibrt;
    case REG_CONFIG: return chip->reg_config;
    case REG_VALRT: return chip->reg_valrt;
    case REG_CRATE: {
      // 0.208 %/hr per LSB, signed.
      const double lsb = chip->crate_pct_per_hr / 0.208;
      const long clamped = lround(fmax(-32768.0, fmin(32767.0, lsb)));
      return (uint16_t)(int16_t)clamped;
    }
    case REG_VRESET: return chip->reg_vreset;
    case REG_STATUS: return chip->reg_status;
    default: return 0x0000;
  }
}

static void commit_write(chip_state_t *chip) {
  switch (chip->selected_reg) {
    case REG_MODE:
      chip->reg_mode = chip->write_value;
      if (chip->write_value & 0x4000) {  // QuickStart
        chip->soc_pct = (double)attr_read_float(chip->soc_attr);
      }
      break;
    case REG_HIBRT: chip->reg_hibrt = chip->write_value; break;
    case REG_CONFIG:
      chip->reg_config = chip->write_value;
      update_alrt(chip);
      break;
    case REG_VALRT: chip->reg_valrt = chip->write_value; break;
    case REG_VRESET: chip->reg_vreset = chip->write_value; break;
    case REG_STATUS: chip->reg_status &= chip->write_value; break;
    default: break;
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
  const uint16_t value = read_register(chip);
  const uint8_t byte = (chip->read_index == 0) ? (uint8_t)(value >> 8) : (uint8_t)(value & 0xff);
  chip->read_index++;
  return byte;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (chip->write_index == 0) {
    chip->selected_reg = data;
  } else if (chip->write_index == 1) {
    chip->write_value = (uint16_t)data << 8;
  } else if (chip->write_index == 2) {
    chip->write_value |= data;
    commit_write(chip);
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
  chip->discharge_attr = attr_init_float("dischargePctPerHour", 0.0f);
  chip->alrt_pin = pin_init("ALRT", OUTPUT_HIGH);

  chip->reg_mode = 0x0000;
  chip->reg_hibrt = 0x8030;   // datasheet power-up default
  chip->reg_config = 0x971C;  // RCOMP=0x97, ATHD=0x1C -> 4%
  chip->reg_valrt = 0x00FF;   // min 0 V, max 5.10 V (alerts effectively off)
  chip->reg_vreset = 0x9600;
  chip->reg_status = STATUS_RI;
  chip->soc_pct = (double)attr_read_float(chip->soc_attr);
  chip->last_alert_soc = (uint8_t)chip->soc_pct;
  chip->last_tick_nanos = (double)get_sim_nanos();
  update_alrt(chip);

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

  const timer_config_t timer = { .callback = on_tick, .user_data = chip };
  timer_start(timer_init(&timer), 100000, true);  // 10 Hz SOC/alert update
}
