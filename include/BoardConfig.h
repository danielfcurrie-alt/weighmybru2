#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Board identification and pin configuration

#ifdef BOARD_SUPERMINI
  #define BOARD_NAME "ESP32-S3-DevKitC-1 (SuperMini)"
  #define BOARD_TYPE_SUPERMINI
  
#elif defined(BOARD_TINYS3D)
  #define BOARD_NAME "TinyS3[D]"
  #define BOARD_TYPE_TINYS3D

#elif defined(BOARD_XIAO)
  #define BOARD_NAME "XIAO ESP32S3"
  #define BOARD_TYPE_XIAO
  
#else
  #define BOARD_NAME "ESP32-S3 (Unknown)"
  #define BOARD_TYPE_SUPERMINI  // Default fallback
  
#endif

// Builder-confirmed TinyS3[D] production wiring. XIAO and SuperMini retain the
// existing WMB+ reference wiring below.
#ifdef BOARD_TYPE_TINYS3D
  #define HX711_DATA_PIN          21
  #define HX711_CLOCK_PIN         2
  #define TOUCH_TARE_PIN          44
  #define TOUCH_SLEEP_PIN         37
  #define I2C_SDA_PIN             8
  #define I2C_SCL_PIN             9
  #define TINYS3D_LIS_INT_PIN     -1
  #define TINYS3D_LCD_SCLK_PIN    36
  #define TINYS3D_LCD_MOSI_PIN    35
  #define TINYS3D_LCD_MISO_PIN    -1
  #define TINYS3D_LCD_CS_PIN      34
  #define TINYS3D_LCD_DC_PIN      1
  #define TINYS3D_LCD_RST_PIN     4
  #define TINYS3D_LCD_BL_PIN      5
  #define TINYS3D_TOUCH_RST_PIN   7
  #define TINYS3D_TOUCH_INT_PIN   6
#else
  #define HX711_DATA_PIN          5
  #define HX711_CLOCK_PIN         6
  #define TOUCH_TARE_PIN          4
  #define TOUCH_SLEEP_PIN         3
  #define I2C_SDA_PIN             8
  #define I2C_SCL_PIN             9
#endif

#define BATTERY_PIN_NONE    255

#ifdef BOARD_TYPE_TINYS3D
  // TinyS3[D] replaces the old TinyS3 VBAT ADC sense with an I2C MAX17048
  // fuel gauge on the default I2C bus. VBUS_SENSE is provided by the Arduino
  // TinyS3 variant and maps to GPIO33.
  #define BATTERY_PIN              BATTERY_PIN_NONE
  #define HAS_ADC_BATTERY          0
  #define HAS_I2C_FUEL_GAUGE       1
  #define FUEL_GAUGE_MAX17048_ADDR 0x36
  #define HAS_USB_POWER_SENSE      1
  #ifndef USB_POWER_SENSE_PIN
    #define USB_POWER_SENSE_PIN    33
  #endif
  #define HAS_BOARD_RGB_STATUS_LED 1
  #define HAS_RF_ANTENNA_SWITCH    1
  #define RF_ANTENNA_SWITCH_PIN    38
  #ifndef RGB_PWR
    #define RGB_PWR 17
  #endif
  #ifndef RGB_BUILTIN
    #define RGB_BUILTIN 18
  #endif
#elif defined(BOARD_TYPE_XIAO)
  #define BATTERY_PIN              7   // GPIO7 - Battery voltage monitoring (ADC1_CH6)
  #define HAS_ADC_BATTERY          1
  #define HAS_I2C_FUEL_GAUGE       0
  #define FUEL_GAUGE_MAX17048_ADDR 0x36
  #define HAS_USB_POWER_SENSE      0
  #define USB_POWER_SENSE_PIN      BATTERY_PIN_NONE
  #define HAS_BOARD_RGB_STATUS_LED 0
  #define HAS_RF_ANTENNA_SWITCH    0
  #define RF_ANTENNA_SWITCH_PIN    BATTERY_PIN_NONE
#else
  #define BATTERY_PIN              7   // GPIO7 - Battery voltage monitoring (ADC1_CH6)
  #define HAS_ADC_BATTERY          1
  #define HAS_I2C_FUEL_GAUGE       0
  #define FUEL_GAUGE_MAX17048_ADDR 0x36
  #define HAS_USB_POWER_SENSE      0
  #define USB_POWER_SENSE_PIN      BATTERY_PIN_NONE
  #define HAS_BOARD_RGB_STATUS_LED 0
  #define HAS_RF_ANTENNA_SWITCH    0
  #define RF_ANTENNA_SWITCH_PIN    BATTERY_PIN_NONE
#endif

// Board-specific configurations
#ifdef BOARD_TYPE_SUPERMINI
  #define FLASH_SIZE_MB       4
  #define BOARD_DESCRIPTION   "ESP32-S3 SuperMini with 4MB Flash"
  
#elif defined(BOARD_TYPE_XIAO)
  #define FLASH_SIZE_MB       8
  #define BOARD_DESCRIPTION   "XIAO ESP32S3 with 8MB Flash"

#elif defined(BOARD_TYPE_TINYS3D)
  #define FLASH_SIZE_MB       8
  #define BOARD_DESCRIPTION   "Unexpected Maker TinyS3[D] with 8MB Flash and MAX17048 fuel gauge"
  
#endif

// Common ESP32-S3 features available on both boards
#define HAS_WIFI            true
#define HAS_BLUETOOTH       true
#define HAS_PSRAM           true
#define HAS_TOUCH_SENSOR    true
#define ADC_RESOLUTION      12    // 12-bit ADC
#define PWM_RESOLUTION      8     // 8-bit PWM

#ifndef WMBP_SIMULATION_MODE
  #define WMBP_SIMULATION_MODE 0
#endif

#ifndef WMBP_ACQUISITION_TASK
  #define WMBP_ACQUISITION_TASK 1
#endif

#ifndef WMBP_ACQUISITION_TASK_PRIORITY
  #define WMBP_ACQUISITION_TASK_PRIORITY 19
#endif

#ifndef WMBP_ACQUISITION_TASK_CORE
  #define WMBP_ACQUISITION_TASK_CORE 1
#endif

#ifndef WMBP_ACQUISITION_TASK_STACK
  #define WMBP_ACQUISITION_TASK_STACK 4096
#endif

#ifndef WMBP_ACQUISITION_DOUT_INTERRUPT
  #define WMBP_ACQUISITION_DOUT_INTERRUPT 0
#endif

#ifndef WMBP_HIGH_RATE_TARE_SAMPLES
  #define WMBP_HIGH_RATE_TARE_SAMPLES 4
#endif

#ifndef WMBP_DISABLE_CRITICAL_BATTERY_SLEEP
  #define WMBP_DISABLE_CRITICAL_BATTERY_SLEEP 0
#endif

#ifndef WMBP_WOKWI_RUNTIME_HARNESS
  #define WMBP_WOKWI_RUNTIME_HARNESS 0
#endif

#ifndef WMBP_WOKWI_SOURCE_STREAM
  #define WMBP_WOKWI_SOURCE_STREAM 1
#endif

#ifndef WMBP_TINY_WOKWI_PERIPHERAL_HARNESS
  #define WMBP_TINY_WOKWI_PERIPHERAL_HARNESS 1
#endif

// Diagnostic event log policy. This stores exception/error events only, not raw
// samples. XIAO and TinyS3[D] should use PSRAM when the runtime reports it is
// present and enough free PSRAM is available.
#if defined(BOARD_TYPE_XIAO) || defined(BOARD_TYPE_TINYS3D)
  #define DIAGNOSTIC_EVENT_LOG_PSRAM_CAPACITY 512
  #define DIAGNOSTIC_EVENT_LOG_HEAP_FALLBACK_CAPACITY 32
#else
  #define DIAGNOSTIC_EVENT_LOG_PSRAM_CAPACITY 256
  #define DIAGNOSTIC_EVENT_LOG_HEAP_FALLBACK_CAPACITY 24
#endif

#endif // BOARD_CONFIG_H
