#ifndef SAMPLE_CADENCE_TRACKER_H
#define SAMPLE_CADENCE_TRACKER_H

#include <stdint.h>
#include <math.h>

class SampleCadenceTracker {
public:
    bool recordSampleMicros(uint32_t nowMicros) {
        if (lastSampleMicros == 0) {
            lastSampleMicros = nowMicros;
            lastIntervalMicros = 0;
            return false;
        }

        const uint32_t intervalMicros = nowMicros - lastSampleMicros;
        lastSampleMicros = nowMicros;
        lastIntervalMicros = intervalMicros;

        if (intervalMicros == 0 || intervalMicros > 2000000UL) {
            return false;
        }

        const uint32_t referenceMicros = getExpectedMicros() > 0 ? getExpectedMicros() : getAverageMicros();
        uint32_t lostSlotsThisSample = 0;
        const bool isLongGap =
            statsCount >= 3 &&
            referenceMicros > 0 &&
            intervalMicros > referenceMicros * 2UL;

        if (statsCount >= 3 && referenceMicros > 0) {
            const uint32_t roundedSlots = static_cast<uint32_t>(roundf(static_cast<float>(intervalMicros) / static_cast<float>(referenceMicros)));
            if (roundedSlots > 1) {
                lostSlotsThisSample = roundedSlots - 1;
                estimatedLostCadenceSlots += lostSlotsThisSample;
            }
        }
        lastEstimatedLostCadenceSlots = lostSlotsThisSample;

        totalMicros += intervalMicros;
        statsCount++;
        if (minMicros == 0 || intervalMicros < minMicros) {
            minMicros = intervalMicros;
        }
        if (intervalMicros > maxMicros) {
            maxMicros = intervalMicros;
        }
        if (expectedMicros == 0) {
            expectedMicros = intervalMicros;
        } else if (!isLongGap) {
            expectedMicros = static_cast<uint32_t>((static_cast<uint64_t>(expectedMicros) * 7ULL + intervalMicros) / 8ULL);
        }
        if (isLongGap) {
            longGapCount++;
        }

        return isLongGap;
    }

    void reset() {
        lastSampleMicros = 0;
        lastIntervalMicros = 0;
        totalMicros = 0;
        statsCount = 0;
        minMicros = 0;
        maxMicros = 0;
        longGapCount = 0;
        expectedMicros = 0;
        estimatedLostCadenceSlots = 0;
        lastEstimatedLostCadenceSlots = 0;
    }

    uint32_t getAverageMicros() const {
        if (statsCount == 0) {
            return 0;
        }
        return static_cast<uint32_t>(totalMicros / statsCount);
    }

    float getRateHz() const {
        const uint32_t averageMicros = getAverageMicros();
        if (averageMicros == 0) {
            return 0.0f;
        }
        return 1000000.0f / static_cast<float>(averageMicros);
    }

    const char* getRateMode() const {
        const float rateHz = getRateHz();
        if (rateHz >= 50.0f) {
            return "80SPS";
        }
        if (rateHz >= 6.0f) {
            return "10SPS";
        }
        return "UNKNOWN";
    }

    uint8_t getRoundedRateHz() const {
        const float rateHz = getRateHz();
        if (rateHz <= 0.0f) {
            return 0;
        }
        if (rateHz >= 255.0f) {
            return 255;
        }
        return static_cast<uint8_t>(roundf(rateHz));
    }

    uint32_t getLastIntervalMicros() const { return lastIntervalMicros; }
    uint64_t getTotalMicros() const { return totalMicros; }
    uint32_t getStatsCount() const { return statsCount; }
    uint32_t getMinMicros() const { return minMicros; }
    uint32_t getMaxMicros() const { return maxMicros; }
    uint32_t getLongGapCount() const { return longGapCount; }
    uint32_t getExpectedMicros() const { return expectedMicros; }
    uint32_t getEstimatedLostCadenceSlots() const { return estimatedLostCadenceSlots; }
    uint32_t getLastEstimatedLostCadenceSlots() const { return lastEstimatedLostCadenceSlots; }

private:
    uint32_t lastSampleMicros = 0;
    uint32_t lastIntervalMicros = 0;
    uint64_t totalMicros = 0;
    uint32_t statsCount = 0;
    uint32_t minMicros = 0;
    uint32_t maxMicros = 0;
    uint32_t longGapCount = 0;
    uint32_t expectedMicros = 0;
    uint32_t estimatedLostCadenceSlots = 0;
    uint32_t lastEstimatedLostCadenceSlots = 0;
};

#endif
