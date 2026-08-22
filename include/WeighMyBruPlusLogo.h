#ifndef WEIGHMYBRU_PLUS_LOGO_H
#define WEIGHMYBRU_PLUS_LOGO_H

#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include <Arduino.h>

namespace WeighMyBruPlusLogo {
constexpr uint16_t WIDTH = 164;
constexpr uint16_t HEIGHT = 164;
constexpr size_t COLOR_COUNT = 16;
constexpr size_t PACKED_BYTE_COUNT =
    static_cast<size_t>(WIDTH) * HEIGHT / 2;

extern const uint16_t PALETTE[COLOR_COUNT] PROGMEM;
extern const uint8_t PIXELS[PACKED_BYTE_COUNT] PROGMEM;
}  // namespace WeighMyBruPlusLogo

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
#endif
