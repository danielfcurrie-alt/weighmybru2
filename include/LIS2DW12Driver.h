#ifndef LIS2DW12_DRIVER_H
#define LIS2DW12_DRIVER_H

#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include <Arduino.h>
#include <Wire.h>

class LIS2DW12Driver {
public:
    static constexpr uint8_t DEFAULT_ADDRESS = 0x19;
    static constexpr uint8_t EXPECTED_DEVICE_ID = 0x44;

    enum class OutputDataRate : uint8_t {
        PowerDown = 0x00,
        Hz12_5 = 0x02,
        Hz25 = 0x03,
        Hz50 = 0x04,
        Hz100 = 0x05,
        Hz200 = 0x06,
        Hz400 = 0x07,
        Hz800 = 0x08,
        Hz1600 = 0x09,
    };

    enum class FullScale : uint8_t {
        G2 = 0,
        G4 = 1,
        G8 = 2,
        G16 = 3,
    };

    struct Config {
        OutputDataRate outputDataRate = OutputDataRate::Hz100;
        FullScale fullScale = FullScale::G4;
        int8_t interruptPin = -1;
        bool routeDataReadyToInterrupt = false;
        bool lowNoise = true;
    };

    struct Sample {
        float xG = 0.0f;
        float yG = 0.0f;
        float zG = 0.0f;
        uint32_t timestampMs = 0;
    };

    explicit LIS2DW12Driver(TwoWire& wire = Wire, uint8_t address = DEFAULT_ADDRESS);

    bool begin();
    bool begin(const Config& config);
    bool configure(const Config& config);
    bool isConnected() const { return connected; }
    bool dataReady();
    bool readAcceleration(Sample& sample);
    bool readTemperatureC(float& temperatureC);
    uint8_t deviceId() const { return detectedDeviceId; }
    uint32_t successfulReads() const { return readCount; }
    uint32_t communicationErrors() const { return errorCount; }
    uint32_t lastSampleMillis() const { return lastSampleMs; }

private:
    TwoWire& wire;
    uint8_t address;
    Config config;
    bool connected;
    uint8_t detectedDeviceId;
    uint32_t readCount;
    uint32_t errorCount;
    uint32_t lastSampleMs;

    bool softReset();
    bool readRegister(uint8_t reg, uint8_t& value);
    bool readRegisters(uint8_t reg, uint8_t* values, size_t length);
    bool writeRegister(uint8_t reg, uint8_t value);
    float rawToG(int16_t raw) const;
};

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
#endif
