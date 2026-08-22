#ifndef HOST_TEST_WIRE_H
#define HOST_TEST_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <deque>
#include <vector>

class TwoWire {
public:
    TwoWire() { resetDevice(); }

    void beginTransmission(uint8_t address) {
        transmittedAddress = address;
        transmitBuffer.clear();
    }

    size_t write(uint8_t value) {
        transmitBuffer.push_back(value);
        return 1;
    }

    uint8_t endTransmission(bool) {
        transmissionCount++;
        if (failNextTransmission) {
            failNextTransmission = false;
            return 4;
        }
        if (transmittedAddress != deviceAddress || transmitBuffer.empty()) {
            return 2;
        }

        selectedRegister = transmitBuffer[0] & 0x7f;
        if (transmitBuffer.size() > 1) {
            uint8_t reg = selectedRegister;
            for (size_t index = 1; index < transmitBuffer.size(); ++index) {
                writeRegister(reg, transmitBuffer[index]);
                if ((registers[0x21] & 0x04) != 0) {
                    reg++;
                }
            }
        }
        return 0;
    }

    size_t requestFrom(uint16_t address, size_t length, bool) {
        requestCount++;
        receiveBuffer.clear();
        if (address != deviceAddress || failNextRequest) {
            failNextRequest = false;
            return 0;
        }
        uint8_t reg = selectedRegister;
        for (size_t index = 0; index < length; ++index) {
            receiveBuffer.push_back(registers[reg]);
            if ((registers[0x21] & 0x04) != 0) {
                reg++;
            }
        }
        return length;
    }

    int available() const { return static_cast<int>(receiveBuffer.size()); }

    int read() {
        if (receiveBuffer.empty()) {
            return -1;
        }
        const uint8_t value = receiveBuffer.front();
        receiveBuffer.pop_front();
        return value;
    }

    void resetDevice() {
        registers.fill(0);
        registers[0x0f] = 0x44;
        registers[0x21] = 0x04;
    }

    void setRegister(uint8_t reg, uint8_t value) { registers[reg] = value; }
    uint8_t registerValue(uint8_t reg) const { return registers[reg]; }
    void failTransmissionOnce() { failNextTransmission = true; }
    void failRequestOnce() { failNextRequest = true; }

    uint8_t deviceAddress = 0x19;
    uint32_t transmissionCount = 0;
    uint32_t requestCount = 0;
    uint32_t resetCount = 0;

private:
    void writeRegister(uint8_t reg, uint8_t value) {
        if (reg == 0x21 && (value & 0x40) != 0) {
            resetCount++;
            resetDevice();
            return;
        }
        registers[reg] = value;
    }

    std::array<uint8_t, 256> registers{};
    std::vector<uint8_t> transmitBuffer;
    std::deque<uint8_t> receiveBuffer;
    uint8_t transmittedAddress = 0;
    uint8_t selectedRegister = 0;
    bool failNextTransmission = false;
    bool failNextRequest = false;
};

extern TwoWire Wire;

#endif
