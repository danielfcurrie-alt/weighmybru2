#ifndef HX711_ACQUISITION_H
#define HX711_ACQUISITION_H

#include "Hx711Io.h"
#include <Arduino.h>
#include <atomic>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class Hx711Acquisition {
public:
    struct RawSample {
        int32_t rawCounts = 0;
        uint32_t readStartMicros = 0;
        uint32_t readEndMicros = 0;
        uint32_t readDurationMicros = 0;
        uint32_t sequence = 0;
        bool levelRecovered = false;
    };

    void begin(uint8_t dataPin, uint8_t clockPin, Hx711Io::Gain gain = Hx711Io::Gain::ChannelA128);
    bool start();
    bool isStarted() const { return taskHandle != nullptr; }
    bool isReady() const { return io.isReady(); }
    Hx711Io::Status readRawSync(int32_t& rawCounts, uint32_t* readDurationMicros = nullptr);
    Hx711Io::Status readRawWithTimeoutSync(uint32_t timeoutMicros,
                                           int32_t& rawCounts,
                                           uint32_t* waitDurationMicros = nullptr,
                                           uint32_t* readDurationMicros = nullptr);
    bool popSample(RawSample& sample);
    void clearSamples();
    void powerDown();
    void powerUp();

    uint32_t getReadyCount() const { return readyCount.load(std::memory_order_relaxed); }
    uint32_t getNotReadyCount() const { return notReadyCount.load(std::memory_order_relaxed); }
    uint32_t getRawReadCount() const { return rawReadCount.load(std::memory_order_relaxed); }
    uint32_t getReadErrorCount() const { return readErrorCount.load(std::memory_order_relaxed); }
    uint32_t getTimeoutCount() const { return timeoutCount.load(std::memory_order_relaxed); }
    uint32_t getDisconnectedCount() const { return disconnectedCount.load(std::memory_order_relaxed); }
    uint32_t getQueueDropCount() const { return queueDropCount.load(std::memory_order_relaxed); }
    uint32_t getDataReadyIrqCount() const { return dataReadyIrqCount.load(std::memory_order_relaxed); }
    uint32_t getTaskWakeCount() const { return taskWakeCount.load(std::memory_order_relaxed); }
    uint32_t getReadyRecoveredByLevelPollCount() const { return readyRecoveredByLevelPollCount.load(std::memory_order_relaxed); }
    uint32_t getDoutHighTimeoutCount() const { return doutHighTimeoutCount.load(std::memory_order_relaxed); }
    uint32_t getReadyWhileBusyCount() const { return readyWhileBusyCount.load(std::memory_order_relaxed); }
    uint32_t getSpuriousReadyCount() const { return spuriousReadyCount.load(std::memory_order_relaxed); }
    uint32_t getLastReadDurationMicros() const { return lastReadDurationMicros.load(std::memory_order_relaxed); }
    uint32_t getMaxReadDurationMicros() const { return maxReadDurationMicros.load(std::memory_order_relaxed); }

private:
    static constexpr uint8_t QUEUE_LENGTH = 64;
    static constexpr uint32_t DISCONNECTED_TIMEOUT_MICROS = 1000000UL;

    Hx711Io io;
    QueueHandle_t sampleQueue = nullptr;
    StaticQueue_t sampleQueueControl;
    uint8_t sampleQueueStorage[QUEUE_LENGTH * sizeof(RawSample)];
    TaskHandle_t taskHandle = nullptr;
    uint8_t dataPin = 0;
    uint8_t clockPin = 0;
    bool begun = false;
    volatile bool readInProgress = false;
    bool interruptAttached = false;

    std::atomic<uint32_t> sequence{0};
    std::atomic<uint32_t> readyCount{0};
    std::atomic<uint32_t> notReadyCount{0};
    std::atomic<uint32_t> rawReadCount{0};
    std::atomic<uint32_t> readErrorCount{0};
    std::atomic<uint32_t> timeoutCount{0};
    std::atomic<uint32_t> disconnectedCount{0};
    std::atomic<uint32_t> queueDropCount{0};
    std::atomic<uint32_t> dataReadyIrqCount{0};
    std::atomic<uint32_t> taskWakeCount{0};
    std::atomic<uint32_t> readyRecoveredByLevelPollCount{0};
    std::atomic<uint32_t> doutHighTimeoutCount{0};
    std::atomic<uint32_t> readyWhileBusyCount{0};
    std::atomic<uint32_t> spuriousReadyCount{0};
    std::atomic<uint32_t> lastReadDurationMicros{0};
    std::atomic<uint32_t> maxReadDurationMicros{0};

    static void taskEntry(void* context);
    static void IRAM_ATTR dataReadyIsr(void* context);
    void taskLoop();
    void publishSample(const RawSample& sample);
    void recordReadDuration(uint32_t readDurationMicros);
    void attachDataReadyInterrupt();
    void detachDataReadyInterrupt();
    void disableDataReadyInterrupt();
    void enableDataReadyInterrupt();
    void clearPendingDataReadyInterrupt();
    void notifyTaskFromTask();
};

#endif
