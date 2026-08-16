#include "wokwi-api.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
  pin_t dout;
  pin_t sck;
  timer_t ready_timer;
  uint32_t sps_attr;
  uint32_t actual_sps_attr;
  uint32_t jitter_micros_attr;
  uint32_t weight_attr;
  uint32_t load_after_ms_attr;
  uint32_t load_weight_attr;
  uint32_t offset_attr;
  uint32_t counts_per_gram_attr;
  uint32_t glitch_every_attr;
  uint32_t glitch_counts_attr;
  uint32_t missed_ready_every_attr;
  uint32_t bit_index;
  uint32_t pulse_count;
  uint32_t conversion_count;
  uint32_t frame;
  bool ready;
  bool powered_down;
  uint64_t sck_high_since_ns;
} chip_state_t;

static uint32_t sample_interval_micros(chip_state_t *chip) {
  const uint32_t requested_mode = attr_read(chip->sps_attr);
  const float actual_sps = attr_read_float(chip->actual_sps_attr);
  float rate = actual_sps > 0.1f ? actual_sps : (requested_mode >= 80 ? 80.0f : 10.0f);

  if (rate < 1.0f) {
    rate = 1.0f;
  }

  int32_t interval = (int32_t)llroundf(1000000.0f / rate);
  const int32_t jitter = (int32_t)attr_read(chip->jitter_micros_attr);
  if (jitter > 0) {
    // Deterministic low-frequency oscillator wander: enough to make tests
    // realistic, but repeatable so CI failures are meaningful.
    static const int8_t pattern[] = {0, 1, -1, 0, 2, -2, 1, -1};
    const size_t index = chip->conversion_count % (sizeof(pattern) / sizeof(pattern[0]));
    interval += pattern[index] * jitter;
  }

  return interval < 1000 ? 1000U : (uint32_t)interval;
}

static int32_t clamp_raw(int64_t raw) {
  if (raw > 0x7FFFFF) {
    return 0x7FFFFF;
  }
  if (raw < -0x800000) {
    return -0x800000;
  }
  return (int32_t)raw;
}

static uint32_t encode_raw24(int32_t raw) {
  return ((uint32_t)raw) & 0x00FFFFFFU;
}

static void schedule_next_ready(chip_state_t *chip);

static int32_t current_raw_counts(chip_state_t *chip) {
  float weight = attr_read_float(chip->weight_attr);
  const uint32_t load_after_ms = attr_read(chip->load_after_ms_attr);
  if (load_after_ms > 0 && (get_sim_nanos() / 1000000ULL) >= load_after_ms) {
    weight = attr_read_float(chip->load_weight_attr);
  }
  const int32_t offset = (int32_t)attr_read(chip->offset_attr);
  const float counts_per_gram = attr_read_float(chip->counts_per_gram_attr);
  int64_t raw = (int64_t)llroundf((float)offset + weight * counts_per_gram);

  const uint32_t glitch_every = attr_read(chip->glitch_every_attr);
  if (glitch_every > 0 && chip->conversion_count > 0 && (chip->conversion_count % glitch_every) == 0) {
    raw += (int32_t)attr_read(chip->glitch_counts_attr);
  }

  return clamp_raw(raw);
}

static void drive_ready(chip_state_t *chip) {
  const uint32_t missed_ready_every = attr_read(chip->missed_ready_every_attr);
  if (missed_ready_every > 0 && chip->conversion_count > 0 &&
      (chip->conversion_count % missed_ready_every) == 0) {
    // Leave DOUT high for this conversion. This exercises firmware timeout /
    // level-recovery paths without corrupting the serial data bits.
    schedule_next_ready(chip);
    return;
  }

  chip->ready = true;
  chip->bit_index = 0;
  chip->pulse_count = 0;
  chip->frame = encode_raw24(current_raw_counts(chip));
  pin_write(chip->dout, LOW);
}

static void schedule_next_ready(chip_state_t *chip) {
  timer_start(chip->ready_timer, sample_interval_micros(chip), false);
}

static void on_ready_timer(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (chip->powered_down || chip->ready) {
    return;
  }
  chip->conversion_count++;
  drive_ready(chip);
}

static void complete_read(chip_state_t *chip) {
  chip->ready = false;
  chip->bit_index = 0;
  chip->pulse_count = 0;
  pin_write(chip->dout, HIGH);
  schedule_next_ready(chip);
}

static void on_sck_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  chip_state_t *chip = (chip_state_t *)user_data;

  if (value == HIGH) {
    chip->sck_high_since_ns = get_sim_nanos();
    if (chip->powered_down) {
      return;
    }

    if (!chip->ready) {
      return;
    }

    chip->pulse_count++;
    if (chip->pulse_count <= 24) {
      const uint32_t shift = 24U - chip->pulse_count;
      const uint32_t bit = (chip->frame >> shift) & 1U;
      pin_write(chip->dout, bit ? HIGH : LOW);
      chip->bit_index = chip->pulse_count;
    } else {
      complete_read(chip);
    }
    return;
  }

  const uint64_t now_ns = get_sim_nanos();
  const uint64_t high_duration_ns = chip->sck_high_since_ns == 0 ? 0 : now_ns - chip->sck_high_since_ns;
  chip->sck_high_since_ns = 0;

  if (high_duration_ns > 60000U) {
    chip->powered_down = true;
    chip->ready = false;
    timer_stop(chip->ready_timer);
    pin_write(chip->dout, HIGH);
    return;
  }

  if (chip->powered_down) {
    chip->powered_down = false;
    schedule_next_ready(chip);
  }
}

void chip_init(void) {
  chip_state_t *chip = (chip_state_t *)calloc(1, sizeof(chip_state_t));
  chip->dout = pin_init("DT", OUTPUT_HIGH);
  chip->sck = pin_init("SCK", INPUT);
  (void)pin_init("VCC", INPUT);
  (void)pin_init("GND", INPUT);

  chip->sps_attr = attr_init("sps", 80);
  chip->actual_sps_attr = attr_init_float("actualSps", 94.22f);
  chip->jitter_micros_attr = attr_init("jitterMicros", 0);
  chip->weight_attr = attr_init_float("weight", 0.0f);
  chip->load_after_ms_attr = attr_init("loadAfterMs", 0);
  chip->load_weight_attr = attr_init_float("loadWeight", 0.0f);
  chip->offset_attr = attr_init("offset", 0);
  chip->counts_per_gram_attr = attr_init_float("countsPerGram", 4200.0f);
  chip->glitch_every_attr = attr_init("glitchEvery", 0);
  chip->glitch_counts_attr = attr_init("glitchCounts", 0);
  chip->missed_ready_every_attr = attr_init("missedReadyEvery", 0);

  const timer_config_t timer_config = {
    .user_data = chip,
    .callback = on_ready_timer,
    .reserved = {0},
  };
  chip->ready_timer = timer_init(&timer_config);

  const pin_watch_config_t watch_config = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_sck_change,
  };
  pin_watch(chip->sck, &watch_config);

  schedule_next_ready(chip);
}
