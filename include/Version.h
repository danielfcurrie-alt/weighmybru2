#ifndef VERSION_H
#define VERSION_H

// Manual version control (update these for releases)
#define WEIGHMYBRU_VERSION_MAJOR 0
#define WEIGHMYBRU_VERSION_MINOR 2
#define WEIGHMYBRU_VERSION_PATCH 0
#define WEIGHMYBRU_VERSION_PRERELEASE "beta.9"  // "", "beta", "rc1", etc.

// User-facing firmware identity. Keep internal WEIGHMYBRU_* symbols stable so
// upstream code can merge cleanly, but present this build as WMB+.
#define WMB_PLUS_FIRMWARE_NAME "WMB+"
#define WMB_PLUS_BLE_DEVICE_NAME "WeighMyBru+"
#define WMB_PLUS_AP_SSID "WMBPlus-AP"
#define WMB_PLUS_MDNS_HOSTNAME "wmb"
#define WMB_PLUS_MDNS_URL "http://wmb.local"
#define WMB_PLUS_LEGACY_MDNS_URL "http://wmbplus.local"

// Automatic build info (filled by build system)
#ifndef WEIGHMYBRU_BUILD_NUMBER
#define WEIGHMYBRU_BUILD_NUMBER 0
#endif

#ifndef WEIGHMYBRU_COMMIT_HASH
#define WEIGHMYBRU_COMMIT_HASH "unknown"
#endif

// Helper macros for string conversion
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Version string construction
#ifdef WEIGHMYBRU_VERSION_PRERELEASE
  #define WEIGHMYBRU_VERSION_STRING TOSTRING(WEIGHMYBRU_VERSION_MAJOR) "." TOSTRING(WEIGHMYBRU_VERSION_MINOR) "." TOSTRING(WEIGHMYBRU_VERSION_PATCH) "-" WEIGHMYBRU_VERSION_PRERELEASE
#else
  #define WEIGHMYBRU_VERSION_STRING TOSTRING(WEIGHMYBRU_VERSION_MAJOR) "." TOSTRING(WEIGHMYBRU_VERSION_MINOR) "." TOSTRING(WEIGHMYBRU_VERSION_PATCH)
#endif

#define WEIGHMYBRU_FULL_VERSION WEIGHMYBRU_VERSION_STRING "+build." TOSTRING(WEIGHMYBRU_BUILD_NUMBER) "." WEIGHMYBRU_COMMIT_HASH
#ifndef WEIGHMYBRU_BUILD_DATE
#define WEIGHMYBRU_BUILD_DATE __DATE__
#endif

#ifndef WEIGHMYBRU_BUILD_TIME
#define WEIGHMYBRU_BUILD_TIME __TIME__
#endif

// Board identification
#ifdef BOARD_SUPERMINI
  #define WEIGHMYBRU_BOARD_NAME "ESP32-S3 Supermini"
#elif defined(BOARD_XIAO)
  #define WEIGHMYBRU_BOARD_NAME "XIAO ESP32S3"
#elif defined(BOARD_TINYS3D)
  #define WEIGHMYBRU_BOARD_NAME "TinyS3[D]"
#else
  #define WEIGHMYBRU_BOARD_NAME "Unknown ESP32"
#endif

#endif // VERSION_H
