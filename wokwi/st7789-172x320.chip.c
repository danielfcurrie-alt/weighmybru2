#include "wokwi-api.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
  pin_t sck;
  pin_t mosi;
  pin_t res;
  pin_t dc;
  pin_t cs;
  pin_t bl;
  uint32_t bit_count;
  bool selected;
} chip_state_t;

static void on_cs_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->selected = value == LOW;
}

static void on_sck_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  chip_state_t *chip = (chip_state_t *)user_data;
  if (!chip->selected || value != HIGH) {
    return;
  }
  chip->bit_count++;
}

void chip_init(void) {
  chip_state_t *chip = (chip_state_t *)calloc(1, sizeof(chip_state_t));
  (void)pin_init("GND", INPUT);
  (void)pin_init("VDD", INPUT);
  chip->sck = pin_init("SCL", INPUT);
  chip->mosi = pin_init("SDA", INPUT);
  chip->res = pin_init("RES", INPUT_PULLUP);
  chip->dc = pin_init("DC", INPUT);
  chip->cs = pin_init("CS", INPUT_PULLUP);
  chip->bl = pin_init("BL", INPUT);
  (void)chip->mosi;
  (void)chip->res;
  (void)chip->dc;
  (void)chip->bl;
  (void)attr_init("width", 172);
  (void)attr_init("height", 320);

  const pin_watch_config_t cs_watch = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_cs_change,
  };
  pin_watch(chip->cs, &cs_watch);

  const pin_watch_config_t sck_watch = {
    .user_data = chip,
    .edge = RISING,
    .pin_change = on_sck_change,
  };
  pin_watch(chip->sck, &sck_watch);
}
