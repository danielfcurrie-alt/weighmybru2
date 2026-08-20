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

// Pin definitions. The WMB+ reference wiring keeps the HX711, touch sensors,
// and OLED on the same GPIOs across supported ESP32-S3 boards.
#define HX711_DATA_PIN      5   // GPIO5 - HX711 Data pin
#define HX711_CLOCK_PIN     6   // GPIO6 - HX711 Clock pin  
#define TOUCH_TARE_PIN      4   // GPIO4 - Touch sensor for tare (T0)
#define TOUCH_SLEEP_PIN     3   // GPIO3 - Touch sensor for sleep functionality
#define I2C_SDA_PIN         8   // GPIO8 - I2C Data pin for display
#define I2C_SCL_PIN         9   // GPIO9 - I2C Clock pin for display

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
