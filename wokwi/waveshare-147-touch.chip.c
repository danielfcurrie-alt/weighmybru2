// Wokwi model for Waveshare's 1.47-inch Touch LCD.
//
// MODELLED:
//   JD9853 SPI framing, reset/backlight/display state, RGB565 address windows,
//   rotation, and framebuffer rendering
//   AXS5106L I2C address 0x63 and two-point coordinate registers
//
// NOT MODELLED:
//   LCD readback, touch pressure/gestures, electrical timing, controller power
//   consumption, or analog behavior.
#include "wokwi-api.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define AXS5106_ADDRESS 0x63
#define LCD_WIDTH 172
#define LCD_HEIGHT 320
#define LCD_X_OFFSET 34
#define SPI_BUFFER_SIZE 4096

typedef struct {
  pin_t lcd_reset;
  pin_t lcd_dc;
  pin_t lcd_cs;
  pin_t lcd_backlight;
  pin_t touch_reset;
  pin_t touch_interrupt;

  spi_dev_t spi;
  uint8_t spi_buffer[SPI_BUFFER_SIZE];
  bool selected;
  bool spi_data_mode;
  uint8_t current_command;
  uint8_t command_data[4];
  uint8_t command_data_count;
  uint32_t spi_byte_count;

  bool sleep_out;
  bool display_on;
  bool backlight_on;
  bool rgb565;
  bool inversion_on;
  uint8_t madctl;
  uint16_t column_start;
  uint16_t column_end;
  uint16_t row_start;
  uint16_t row_end;
  uint16_t write_column;
  uint16_t write_row;
  uint8_t pixel_high_byte;
  bool pixel_high_byte_pending;

  buffer_t framebuffer;
  uint32_t framebuffer_width;
  uint32_t framebuffer_height;
  uint16_t *gram;

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
  return clamp_coordinate(attr_read_float(attr), LCD_WIDTH - 1);
}

static uint16_t touch_y(chip_state_t *chip, uint8_t point) {
  const uint32_t attr = point == 0 ? chip->touch1_y_attr : chip->touch2_y_attr;
  return clamp_coordinate(attr_read_float(attr), LCD_HEIGHT - 1);
}

static void update_touch_interrupt(chip_state_t *chip) {
  // The Waveshare demo treats TP_INT as active low.
  pin_write(chip->touch_interrupt, touch_count(chip) > 0 ? LOW : HIGH);
}

static void rgb565_to_rgba(uint16_t color, uint8_t *rgba) {
  const uint8_t red = (uint8_t)((color >> 11) & 0x1f);
  const uint8_t green = (uint8_t)((color >> 5) & 0x3f);
  const uint8_t blue = (uint8_t)(color & 0x1f);
  rgba[0] = (uint8_t)((red << 3) | (red >> 2));
  rgba[1] = (uint8_t)((green << 2) | (green >> 4));
  rgba[2] = (uint8_t)((blue << 3) | (blue >> 2));
  rgba[3] = 0xff;
}

static bool panel_visible(const chip_state_t *chip) {
  return chip->sleep_out && chip->display_on && chip->backlight_on;
}

static void write_visible_pixel(chip_state_t *chip, uint16_t x, uint16_t y,
                                uint16_t color) {
  if (!panel_visible(chip) || x >= LCD_WIDTH || y >= LCD_HEIGHT) {
    return;
  }
  uint8_t rgba[4];
  rgb565_to_rgba(color, rgba);
  const uint32_t offset = ((uint32_t)y * LCD_WIDTH + x) * 4;
  buffer_write(chip->framebuffer, offset, rgba, sizeof(rgba));
}

static void redraw_framebuffer(chip_state_t *chip) {
  uint8_t row[LCD_WIDTH * 4];
  const bool visible = panel_visible(chip);
  for (uint16_t y = 0; y < LCD_HEIGHT; y++) {
    for (uint16_t x = 0; x < LCD_WIDTH; x++) {
      const uint16_t color = visible ? chip->gram[(uint32_t)y * LCD_WIDTH + x] : 0;
      rgb565_to_rgba(color, &row[x * 4]);
    }
    buffer_write(chip->framebuffer, (uint32_t)y * LCD_WIDTH * 4,
                 row, sizeof(row));
  }
}

static bool map_controller_pixel(const chip_state_t *chip, uint16_t column,
                                 uint16_t row, uint16_t *x, uint16_t *y) {
  switch (chip->madctl & 0xe0) {
    case 0x60:  // MX + MV, landscape clockwise
      if (row < LCD_X_OFFSET || row >= LCD_X_OFFSET + LCD_WIDTH ||
          column >= LCD_HEIGHT) {
        return false;
      }
      *x = row - LCD_X_OFFSET;
      *y = LCD_HEIGHT - 1 - column;
      return true;
    case 0xc0:  // MX + MY, portrait upside-down
      if (column < LCD_X_OFFSET || column >= LCD_X_OFFSET + LCD_WIDTH ||
          row >= LCD_HEIGHT) {
        return false;
      }
      *x = LCD_WIDTH - 1 - (column - LCD_X_OFFSET);
      *y = LCD_HEIGHT - 1 - row;
      return true;
    case 0xa0:  // MY + MV, landscape counter-clockwise
      if (row < LCD_X_OFFSET || row >= LCD_X_OFFSET + LCD_WIDTH ||
          column >= LCD_HEIGHT) {
        return false;
      }
      *x = LCD_WIDTH - 1 - (row - LCD_X_OFFSET);
      *y = column;
      return true;
    default:
      if (column < LCD_X_OFFSET || column >= LCD_X_OFFSET + LCD_WIDTH ||
          row >= LCD_HEIGHT) {
        return false;
      }
      *x = column - LCD_X_OFFSET;
      *y = row;
      return true;
  }
}

static void advance_write_cursor(chip_state_t *chip) {
  if (chip->write_column < chip->column_end) {
    chip->write_column++;
    return;
  }
  chip->write_column = chip->column_start;
  if (chip->write_row < chip->row_end) {
    chip->write_row++;
  } else {
    chip->write_row = chip->row_start;
  }
}

static void accept_pixel_byte(chip_state_t *chip, uint8_t value) {
  if (!chip->rgb565) {
    return;
  }
  if (!chip->pixel_high_byte_pending) {
    chip->pixel_high_byte = value;
    chip->pixel_high_byte_pending = true;
    return;
  }

  const uint16_t color = ((uint16_t)chip->pixel_high_byte << 8) | value;
  uint16_t x = 0;
  uint16_t y = 0;
  if (map_controller_pixel(chip, chip->write_column, chip->write_row, &x, &y)) {
    chip->gram[(uint32_t)y * LCD_WIDTH + x] = color;
    write_visible_pixel(chip, x, y, color);
  }
  chip->pixel_high_byte_pending = false;
  advance_write_cursor(chip);
}

static void accept_command_data(chip_state_t *chip, uint8_t value) {
  if (chip->current_command == 0x2c) {
    accept_pixel_byte(chip, value);
    return;
  }
  if (chip->command_data_count < sizeof(chip->command_data)) {
    chip->command_data[chip->command_data_count++] = value;
  }
  switch (chip->current_command) {
    case 0x2a:
      if (chip->command_data_count == 4) {
        chip->column_start = ((uint16_t)chip->command_data[0] << 8) |
                             chip->command_data[1];
        chip->column_end = ((uint16_t)chip->command_data[2] << 8) |
                           chip->command_data[3];
      }
      break;
    case 0x2b:
      if (chip->command_data_count == 4) {
        chip->row_start = ((uint16_t)chip->command_data[0] << 8) |
                          chip->command_data[1];
        chip->row_end = ((uint16_t)chip->command_data[2] << 8) |
                        chip->command_data[3];
      }
      break;
    case 0x36:
      chip->madctl = value;
      break;
    case 0x3a:
      chip->rgb565 = value == 0x05;
      break;
    default:
      break;
  }
}

static void accept_spi_byte(chip_state_t *chip, uint8_t value, bool data_mode) {
  chip->spi_byte_count++;
  if (data_mode) {
    accept_command_data(chip, value);
    return;
  }

  chip->current_command = value;
  chip->command_data_count = 0;
  chip->pixel_high_byte_pending = false;
  switch (value) {
    case 0x11:
      chip->sleep_out = true;
      redraw_framebuffer(chip);
      break;
    case 0x20:
      chip->inversion_on = false;
      break;
    case 0x21:
      chip->inversion_on = true;
      break;
    case 0x28:
      chip->display_on = false;
      redraw_framebuffer(chip);
      break;
    case 0x29:
      chip->display_on = true;
      redraw_framebuffer(chip);
      break;
    case 0x2c:
      chip->write_column = chip->column_start;
      chip->write_row = chip->row_start;
      break;
    default:
      break;
  }
}

static void process_spi_bytes(chip_state_t *chip, uint8_t *buffer, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    accept_spi_byte(chip, buffer[i], chip->spi_data_mode);
  }
}

static void on_spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  chip_state_t *chip = (chip_state_t *)user_data;
  process_spi_bytes(chip, buffer, count);
  if (chip->selected) {
    chip->spi_data_mode = pin_read(chip->lcd_dc) == HIGH;
    spi_start(chip->spi, chip->spi_buffer, sizeof(chip->spi_buffer));
  }
}

static void on_lcd_cs_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  chip_state_t *chip = (chip_state_t *)user_data;
  if (value == LOW && !chip->selected) {
    chip->selected = true;
    chip->spi_data_mode = pin_read(chip->lcd_dc) == HIGH;
    spi_start(chip->spi, chip->spi_buffer, sizeof(chip->spi_buffer));
  } else if (value == HIGH && chip->selected) {
    chip->selected = false;
    spi_stop(chip->spi);
  }
}

static void on_lcd_dc_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  (void)value;
  chip_state_t *chip = (chip_state_t *)user_data;
  if (chip->selected) {
    // Flush bytes received under the previous D/C level. The done callback
    // restarts reception using the new level.
    spi_stop(chip->spi);
  }
}

static void reset_lcd_state(chip_state_t *chip, bool clear_gram) {
  chip->current_command = 0;
  chip->command_data_count = 0;
  chip->spi_byte_count = 0;
  chip->sleep_out = false;
  chip->display_on = false;
  chip->rgb565 = false;
  chip->inversion_on = false;
  chip->madctl = 0;
  chip->column_start = LCD_X_OFFSET;
  chip->column_end = LCD_X_OFFSET + LCD_WIDTH - 1;
  chip->row_start = 0;
  chip->row_end = LCD_HEIGHT - 1;
  chip->write_column = chip->column_start;
  chip->write_row = chip->row_start;
  chip->pixel_high_byte_pending = false;
  if (clear_gram && chip->gram != NULL) {
    memset(chip->gram, 0, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
  }
  redraw_framebuffer(chip);
}

static void on_lcd_reset_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  if (value == LOW) {
    reset_lcd_state((chip_state_t *)user_data, true);
  }
}

static void on_lcd_backlight_change(void *user_data, pin_t pin, uint32_t value) {
  (void)pin;
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->backlight_on = value == HIGH;
  redraw_framebuffer(chip);
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
    case 0x0a: return (uint8_t)x2;
    case 0x0b: return (uint8_t)((y2 >> 8) & 0x0f);
    case 0x0c: return (uint8_t)y2;
    default: return 0;
  }
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  const uint8_t value = touch_register(
      chip, (uint8_t)(chip->selected_register + chip->i2c_read_index));
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

  chip->framebuffer = framebuffer_init(&chip->framebuffer_width,
                                       &chip->framebuffer_height);
  chip->gram = (uint16_t *)calloc(LCD_WIDTH * LCD_HEIGHT, sizeof(uint16_t));

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
  const pin_t miso = pin_init("MISO", OUTPUT_LOW);
  const pin_t mosi = pin_init("MOSI", INPUT);
  const pin_t sclk = pin_init("SCLK", INPUT);
  (void)pin_init("GND", INPUT);
  (void)pin_init("VCC", INPUT);

  const spi_config_t spi_config = {
    .sck = sclk,
    .mosi = mosi,
    .miso = miso,
    .mode = 0,
    .done = on_spi_done,
    .user_data = chip,
  };
  chip->spi = spi_init(&spi_config);

  chip->backlight_on = pin_read(chip->lcd_backlight) == HIGH;
  reset_lcd_state(chip, true);
  update_touch_interrupt(chip);

  const pin_watch_config_t cs_watch = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_lcd_cs_change,
  };
  pin_watch(chip->lcd_cs, &cs_watch);

  const pin_watch_config_t dc_watch = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_lcd_dc_change,
  };
  pin_watch(chip->lcd_dc, &dc_watch);

  const pin_watch_config_t reset_watch = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_lcd_reset_change,
  };
  pin_watch(chip->lcd_reset, &reset_watch);

  const pin_watch_config_t backlight_watch = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_lcd_backlight_change,
  };
  pin_watch(chip->lcd_backlight, &backlight_watch);

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
