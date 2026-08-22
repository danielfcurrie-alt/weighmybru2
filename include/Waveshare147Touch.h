#ifndef WAVESHARE_147_TOUCH_H
#define WAVESHARE_147_TOUCH_H

#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

class JD9853DisplayDriver {
public:
    static constexpr uint16_t PORTRAIT_WIDTH = 172;
    static constexpr uint16_t PORTRAIT_HEIGHT = 320;

    struct Config {
        int8_t sclkPin = -1;
        int8_t mosiPin = -1;
        int8_t misoPin = -1;
        int8_t chipSelectPin = -1;
        int8_t dataCommandPin = -1;
        int8_t resetPin = -1;
        int8_t backlightPin = -1;
        uint32_t spiFrequencyHz = 40000000;
        uint8_t rotation = 0;
        bool backlightActiveHigh = true;
    };

    explicit JD9853DisplayDriver(SPIClass& spi = SPI);

    bool begin(const Config& config);
    bool isInitialized() const { return initialized; }
    void setRotation(uint8_t rotation);
    uint8_t rotation() const { return currentRotation; }
    uint16_t width() const;
    uint16_t height() const;
    void setBacklight(bool enabled);
    void setDisplayEnabled(bool enabled);
    void invertDisplay(bool inverted);
    bool fill(uint16_t color);
    bool fillRect(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color);
    bool drawRGB565(int16_t x, int16_t y, int16_t width, int16_t height,
                    const uint16_t* pixels, size_t pixelCount);

private:
    SPIClass& spi;
    Config config;
    bool initialized;
    uint8_t currentRotation;
    uint16_t xOffset;
    uint16_t yOffset;

    void hardwareReset();
    void runInitializationSequence();
    void writeCommand(uint8_t command, const uint8_t* data = nullptr, size_t length = 0);
    void setAddressWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    void beginDataWrite();
    void endDataWrite();
};

class AXS5106TouchDriver {
public:
    static constexpr uint8_t DEFAULT_ADDRESS = 0x63;
    static constexpr uint8_t MAX_POINTS = 2;

    struct Config {
        uint8_t address = DEFAULT_ADDRESS;
        int8_t resetPin = -1;
        int8_t interruptPin = -1;
        uint8_t rotation = 0;
        bool interruptActiveLow = true;
    };

    struct Point {
        uint16_t x = 0;
        uint16_t y = 0;
    };

    struct Frame {
        Point points[MAX_POINTS];
        uint8_t count = 0;
        uint32_t timestampMs = 0;
    };

    explicit AXS5106TouchDriver(TwoWire& wire = Wire);

    bool begin(const Config& config);
    bool isConnected() const { return connected; }
    bool interruptAsserted() const;
    bool read(Frame& frame);
    uint32_t successfulReads() const { return readCount; }
    uint32_t communicationErrors() const { return errorCount; }

private:
    TwoWire& wire;
    Config config;
    bool connected;
    uint32_t readCount;
    uint32_t errorCount;

    bool readRegisters(uint8_t reg, uint8_t* values, size_t length);
    void transform(uint16_t rawX, uint16_t rawY, Point& point) const;
};

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
#endif
