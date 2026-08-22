#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include "Waveshare147Touch.h"

namespace {
struct InitCommand {
    uint8_t command;
    uint8_t data[32];
    uint8_t dataLength;
    uint16_t delayMs;
};

// Waveshare's JD9853 sequence for the 172x320 1.47-inch Touch LCD.
constexpr InitCommand JD9853_INIT[] = {
    {0x11, {}, 0, 120},
    {0xDF, {0x98, 0x53}, 2, 0},
    {0xDF, {0x98, 0x53}, 2, 0},
    {0xB2, {0x23}, 1, 0},
    {0xB7, {0x00, 0x47, 0x00, 0x6F}, 4, 0},
    {0xBB, {0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0}, 6, 0},
    {0xC0, {0x44, 0xA4}, 2, 0},
    {0xC1, {0x16}, 1, 0},
    {0xC3, {0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77}, 8, 0},
    {0xC4, {0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82}, 12, 0},
    {0xC8, {0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
            0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
            0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
            0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00}, 32, 0},
    {0xD0, {0x04, 0x06, 0x6B, 0x0F, 0x00}, 5, 0},
    {0xD7, {0x00, 0x30}, 2, 0},
    {0xE6, {0x14}, 1, 0},
    {0xDE, {0x01}, 1, 0},
    {0xB7, {0x03, 0x13, 0xEF, 0x35, 0x35}, 5, 0},
    {0xC1, {0x14, 0x15, 0xC0}, 3, 0},
    {0xC2, {0x06, 0x3A}, 2, 0},
    {0xC4, {0x72, 0x12}, 2, 0},
    {0xBE, {0x00}, 1, 0},
    {0xDE, {0x02}, 1, 0},
    {0xE5, {0x00, 0x02, 0x00}, 3, 0},
    {0xE5, {0x01, 0x02, 0x00}, 3, 0},
    {0xDE, {0x00}, 1, 0},
    {0x35, {0x00}, 1, 0},
    {0x3A, {0x05}, 1, 0},
    {0x2A, {0x00, 0x22, 0x00, 0xCD}, 4, 0},
    {0x2B, {0x00, 0x00, 0x01, 0x3F}, 4, 0},
    {0xDE, {0x02}, 1, 0},
    {0xE5, {0x00, 0x02, 0x00}, 3, 0},
    {0xDE, {0x00}, 1, 0},
    {0x29, {}, 0, 0},
};

constexpr uint8_t CMD_COLUMN_ADDRESS_SET = 0x2A;
constexpr uint8_t CMD_ROW_ADDRESS_SET = 0x2B;
constexpr uint8_t CMD_MEMORY_WRITE = 0x2C;
constexpr uint8_t CMD_MEMORY_ACCESS_CONTROL = 0x36;
constexpr uint8_t CMD_DISPLAY_OFF = 0x28;
constexpr uint8_t CMD_DISPLAY_ON = 0x29;
constexpr uint8_t CMD_INVERSION_OFF = 0x20;
constexpr uint8_t CMD_INVERSION_ON = 0x21;
constexpr uint8_t MADCTL_MIRROR_Y = 0x80;
constexpr uint8_t MADCTL_MIRROR_X = 0x40;
constexpr uint8_t MADCTL_SWAP_XY = 0x20;

constexpr uint8_t TOUCH_POINTS_REGISTER = 0x01;
}

JD9853DisplayDriver::JD9853DisplayDriver(SPIClass& spi)
    : spi(spi), initialized(false), currentRotation(0), xOffset(34), yOffset(0) {}

bool JD9853DisplayDriver::begin(const Config& newConfig) {
    if (newConfig.sclkPin < 0 || newConfig.mosiPin < 0 ||
        newConfig.chipSelectPin < 0 || newConfig.dataCommandPin < 0 ||
        newConfig.resetPin < 0) {
        return false;
    }

    config = newConfig;
    pinMode(config.chipSelectPin, OUTPUT);
    pinMode(config.dataCommandPin, OUTPUT);
    pinMode(config.resetPin, OUTPUT);
    digitalWrite(config.chipSelectPin, HIGH);

    if (config.backlightPin >= 0) {
        pinMode(config.backlightPin, OUTPUT);
        setBacklight(false);
    }

    spi.begin(config.sclkPin, config.misoPin, config.mosiPin, config.chipSelectPin);
    hardwareReset();
    runInitializationSequence();
    initialized = true;
    setRotation(config.rotation);
    invertDisplay(true);
    setDisplayEnabled(true);
    setBacklight(true);
    return true;
}

void JD9853DisplayDriver::setRotation(uint8_t rotation) {
    currentRotation = rotation & 0x03;
    uint8_t madctl = 0;
    switch (currentRotation) {
        case 1:
            madctl = MADCTL_MIRROR_X | MADCTL_SWAP_XY;
            xOffset = 0;
            yOffset = 34;
            break;
        case 2:
            madctl = MADCTL_MIRROR_X | MADCTL_MIRROR_Y;
            xOffset = 34;
            yOffset = 0;
            break;
        case 3:
            madctl = MADCTL_MIRROR_Y | MADCTL_SWAP_XY;
            xOffset = 0;
            yOffset = 34;
            break;
        default:
            xOffset = 34;
            yOffset = 0;
            break;
    }
    if (initialized) {
        writeCommand(CMD_MEMORY_ACCESS_CONTROL, &madctl, 1);
    }
}

uint16_t JD9853DisplayDriver::width() const {
    return (currentRotation & 1) ? PORTRAIT_HEIGHT : PORTRAIT_WIDTH;
}

uint16_t JD9853DisplayDriver::height() const {
    return (currentRotation & 1) ? PORTRAIT_WIDTH : PORTRAIT_HEIGHT;
}

void JD9853DisplayDriver::setBacklight(bool enabled) {
    if (config.backlightPin < 0) {
        return;
    }
    const bool level = config.backlightActiveHigh ? enabled : !enabled;
    digitalWrite(config.backlightPin, level ? HIGH : LOW);
}

void JD9853DisplayDriver::setDisplayEnabled(bool enabled) {
    if (initialized) {
        writeCommand(enabled ? CMD_DISPLAY_ON : CMD_DISPLAY_OFF);
    }
}

void JD9853DisplayDriver::invertDisplay(bool inverted) {
    if (initialized) {
        writeCommand(inverted ? CMD_INVERSION_ON : CMD_INVERSION_OFF);
    }
}

bool JD9853DisplayDriver::fill(uint16_t color) {
    return fillRect(0, 0, width(), height(), color);
}

bool JD9853DisplayDriver::fillRect(int16_t x, int16_t y, int16_t rectWidth,
                                   int16_t rectHeight, uint16_t color) {
    if (!initialized || rectWidth <= 0 || rectHeight <= 0 ||
        x >= static_cast<int16_t>(width()) || y >= static_cast<int16_t>(height())) {
        return false;
    }

    if (x < 0) {
        rectWidth += x;
        x = 0;
    }
    if (y < 0) {
        rectHeight += y;
        y = 0;
    }
    rectWidth = min(rectWidth, static_cast<int16_t>(width() - x));
    rectHeight = min(rectHeight, static_cast<int16_t>(height() - y));
    if (rectWidth <= 0 || rectHeight <= 0) {
        return false;
    }

    setAddressWindow(x, y, rectWidth, rectHeight);
    beginDataWrite();
    const size_t count = static_cast<size_t>(rectWidth) * rectHeight;
    for (size_t i = 0; i < count; i++) {
        spi.transfer16(color);
    }
    endDataWrite();
    return true;
}

bool JD9853DisplayDriver::drawRGB565(int16_t x, int16_t y, int16_t imageWidth,
                                     int16_t imageHeight, const uint16_t* pixels,
                                     size_t pixelCount) {
    if (!initialized || pixels == nullptr || x < 0 || y < 0 ||
        imageWidth <= 0 || imageHeight <= 0 ||
        x + imageWidth > width() || y + imageHeight > height()) {
        return false;
    }
    const size_t requiredPixels = static_cast<size_t>(imageWidth) * imageHeight;
    if (pixelCount < requiredPixels) {
        return false;
    }

    setAddressWindow(x, y, imageWidth, imageHeight);
    beginDataWrite();
    for (size_t i = 0; i < requiredPixels; i++) {
        spi.transfer16(pixels[i]);
    }
    endDataWrite();
    return true;
}

void JD9853DisplayDriver::hardwareReset() {
    digitalWrite(config.resetPin, LOW);
    delay(10);
    digitalWrite(config.resetPin, HIGH);
    delay(10);
}

void JD9853DisplayDriver::runInitializationSequence() {
    for (const InitCommand& init : JD9853_INIT) {
        writeCommand(init.command, init.data, init.dataLength);
        if (init.delayMs > 0) {
            delay(init.delayMs);
        }
    }
}

void JD9853DisplayDriver::writeCommand(uint8_t command, const uint8_t* data, size_t length) {
    spi.beginTransaction(SPISettings(config.spiFrequencyHz, MSBFIRST, SPI_MODE0));
    digitalWrite(config.chipSelectPin, LOW);
    digitalWrite(config.dataCommandPin, LOW);
    spi.transfer(command);
    if (data != nullptr && length > 0) {
        digitalWrite(config.dataCommandPin, HIGH);
        spi.transferBytes(data, nullptr, length);
    }
    digitalWrite(config.chipSelectPin, HIGH);
    spi.endTransaction();
}

void JD9853DisplayDriver::setAddressWindow(uint16_t x, uint16_t y, uint16_t windowWidth,
                                           uint16_t windowHeight) {
    const uint16_t xStart = x + xOffset;
    const uint16_t xEnd = xStart + windowWidth - 1;
    const uint16_t yStart = y + yOffset;
    const uint16_t yEnd = yStart + windowHeight - 1;
    const uint8_t columns[] = {
        static_cast<uint8_t>(xStart >> 8), static_cast<uint8_t>(xStart),
        static_cast<uint8_t>(xEnd >> 8), static_cast<uint8_t>(xEnd),
    };
    const uint8_t rows[] = {
        static_cast<uint8_t>(yStart >> 8), static_cast<uint8_t>(yStart),
        static_cast<uint8_t>(yEnd >> 8), static_cast<uint8_t>(yEnd),
    };
    writeCommand(CMD_COLUMN_ADDRESS_SET, columns, sizeof(columns));
    writeCommand(CMD_ROW_ADDRESS_SET, rows, sizeof(rows));
}

void JD9853DisplayDriver::beginDataWrite() {
    spi.beginTransaction(SPISettings(config.spiFrequencyHz, MSBFIRST, SPI_MODE0));
    digitalWrite(config.chipSelectPin, LOW);
    digitalWrite(config.dataCommandPin, LOW);
    spi.transfer(CMD_MEMORY_WRITE);
    digitalWrite(config.dataCommandPin, HIGH);
}

void JD9853DisplayDriver::endDataWrite() {
    digitalWrite(config.chipSelectPin, HIGH);
    spi.endTransaction();
}

AXS5106TouchDriver::AXS5106TouchDriver(TwoWire& wire)
    : wire(wire), connected(false), readCount(0), errorCount(0) {}

bool AXS5106TouchDriver::begin(const Config& newConfig) {
    config = newConfig;
    connected = false;

    if (config.interruptPin >= 0) {
        pinMode(config.interruptPin, config.interruptActiveLow ? INPUT_PULLUP : INPUT_PULLDOWN);
    }
    if (config.resetPin >= 0) {
        pinMode(config.resetPin, OUTPUT);
        digitalWrite(config.resetPin, LOW);
        delay(10);
        digitalWrite(config.resetPin, HIGH);
        delay(10);
    }

    wire.beginTransmission(config.address);
    connected = wire.endTransmission(true) == 0;
    if (!connected) {
        errorCount++;
    }
    return connected;
}

bool AXS5106TouchDriver::interruptAsserted() const {
    if (config.interruptPin < 0) {
        return true;
    }
    const bool level = digitalRead(config.interruptPin) == HIGH;
    return config.interruptActiveLow ? !level : level;
}

bool AXS5106TouchDriver::read(Frame& frame) {
    uint8_t data[14] = {};
    frame.count = 0;
    if (!connected || !readRegisters(TOUCH_POINTS_REGISTER, data, sizeof(data))) {
        return false;
    }

    frame.count = min(static_cast<uint8_t>(data[1] & 0x0F), MAX_POINTS);
    for (uint8_t i = 0; i < frame.count; i++) {
        const size_t offset = 2 + i * 6;
        const uint16_t rawX = (static_cast<uint16_t>(data[offset] & 0x0F) << 8) |
                              data[offset + 1];
        const uint16_t rawY = (static_cast<uint16_t>(data[offset + 2] & 0x0F) << 8) |
                              data[offset + 3];
        transform(rawX, rawY, frame.points[i]);
    }
    frame.timestampMs = millis();
    readCount++;
    return true;
}

bool AXS5106TouchDriver::readRegisters(uint8_t reg, uint8_t* values, size_t length) {
    if (values == nullptr || length == 0 || length > 255) {
        errorCount++;
        return false;
    }

    wire.beginTransmission(config.address);
    wire.write(reg);
    if (wire.endTransmission(true) != 0) {
        errorCount++;
        return false;
    }
    const size_t received = wire.requestFrom(static_cast<uint16_t>(config.address), length, true);
    if (received != length) {
        while (wire.available()) {
            wire.read();
        }
        errorCount++;
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        values[i] = static_cast<uint8_t>(wire.read());
    }
    return true;
}

void AXS5106TouchDriver::transform(uint16_t rawX, uint16_t rawY, Point& point) const {
    const uint16_t x = min(rawX, static_cast<uint16_t>(JD9853DisplayDriver::PORTRAIT_WIDTH - 1));
    const uint16_t y = min(rawY, static_cast<uint16_t>(JD9853DisplayDriver::PORTRAIT_HEIGHT - 1));

    // Matches Waveshare's orientation flags for its ESP32 demo.
    switch (config.rotation & 0x03) {
        case 1:
            point.x = y;
            point.y = x;
            break;
        case 2:
            point.x = x;
            point.y = JD9853DisplayDriver::PORTRAIT_HEIGHT - 1 - y;
            break;
        case 3:
            point.x = JD9853DisplayDriver::PORTRAIT_HEIGHT - 1 - y;
            point.y = JD9853DisplayDriver::PORTRAIT_WIDTH - 1 - x;
            break;
        default:
            point.x = JD9853DisplayDriver::PORTRAIT_WIDTH - 1 - x;
            point.y = y;
            break;
    }
}

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
