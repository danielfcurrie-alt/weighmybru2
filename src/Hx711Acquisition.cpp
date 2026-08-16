#include "Hx711Acquisition.h"
#include "BoardConfig.h"

#if WMBP_ACQUISITION_DOUT_INTERRUPT
#include "driver/gpio.h"
#include "soc/gpio_struct.h"
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

void Hx711Acquisition::begin(uint8_t dataPinArg, uint8_t clockPinArg, Hx711Io::Gain gain) {
    dataPin = dataPinArg;
    clockPin = clockPinArg;
    io.begin(dataPin, clockPin, gain);
    begun = true;

    if (sampleQueue == nullptr) {
        sampleQueue = xQueueCreateStatic(QUEUE_LENGTH,
                                         sizeof(RawSample),
                                         sampleQueueStorage,
                                         &sampleQueueControl);
    }
}

bool Hx711Acquisition::start() {
    if (!begun || sampleQueue == nullptr) {
        return false;
    }
    if (taskHandle != nullptr) {
        return true;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(taskEntry,
                                                       "hx711_acq",
                                                       WMBP_ACQUISITION_TASK_STACK,
                                                       this,
                                                       WMBP_ACQUISITION_TASK_PRIORITY,
                                                       &taskHandle,
                                                       WMBP_ACQUISITION_TASK_CORE);
    if (created != pdPASS) {
        taskHandle = nullptr;
        return false;
    }
#if WMBP_ACQUISITION_DOUT_INTERRUPT
    attachDataReadyInterrupt();
#endif
    return true;
}

Hx711Io::Status Hx711Acquisition::readRawSync(int32_t& rawCounts, uint32_t* readDurationMicros) {
    return io.readRaw(rawCounts, readDurationMicros);
}

Hx711Io::Status Hx711Acquisition::readRawWithTimeoutSync(uint32_t timeoutMicros,
                                                         int32_t& rawCounts,
                                                         uint32_t* waitDurationMicros,
                                                         uint32_t* readDurationMicros) {
    return io.readRawWithTimeout(timeoutMicros, rawCounts, waitDurationMicros, readDurationMicros);
}

bool Hx711Acquisition::popSample(RawSample& sample) {
    if (sampleQueue == nullptr) {
        return false;
    }
    return xQueueReceive(sampleQueue, &sample, 0) == pdTRUE;
}

void Hx711Acquisition::clearSamples() {
    if (sampleQueue != nullptr) {
        xQueueReset(sampleQueue);
    }
}

void Hx711Acquisition::powerDown() {
#if WMBP_ACQUISITION_DOUT_INTERRUPT
    detachDataReadyInterrupt();
#endif
    io.powerDown();
}

void Hx711Acquisition::powerUp() {
    io.powerUp();
#if WMBP_ACQUISITION_DOUT_INTERRUPT
    if (taskHandle != nullptr) {
        attachDataReadyInterrupt();
    }
#endif
}

void Hx711Acquisition::taskEntry(void* context) {
    static_cast<Hx711Acquisition*>(context)->taskLoop();
}

void IRAM_ATTR Hx711Acquisition::dataReadyIsr(void* context) {
#if WMBP_ACQUISITION_DOUT_INTERRUPT
    auto* self = static_cast<Hx711Acquisition*>(context);
    if (self == nullptr) {
        return;
    }

    self->dataReadyIrqCount.fetch_add(1, std::memory_order_relaxed);

    if (self->readInProgress) {
        self->readyWhileBusyCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    TaskHandle_t handle = self->taskHandle;
    if (handle == nullptr) {
        return;
    }

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(handle, &higherPriorityTaskWoken);
    if (higherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
#else
    (void)context;
#endif
}

void Hx711Acquisition::taskLoop() {
    uint32_t lastNotReadyStartMicros = micros();
    uint32_t lastTimeoutReportMicros = 0;

    for (;;) {
#if WMBP_ACQUISITION_DOUT_INTERRUPT
        uint32_t notifications = 0;
        if (interruptAttached) {
            notifications = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2));
            if (notifications > 0) {
                taskWakeCount.fetch_add(notifications, std::memory_order_relaxed);
            }
        }
#endif

        if (!io.isReady()) {
            notReadyCount.fetch_add(1, std::memory_order_relaxed);
#if WMBP_ACQUISITION_DOUT_INTERRUPT
            if (interruptAttached && notifications > 0) {
                spuriousReadyCount.fetch_add(1, std::memory_order_relaxed);
            }
#endif

            const uint32_t nowMicros = micros();
            const uint32_t notReadyDurationMicros = nowMicros - lastNotReadyStartMicros;
            if (notReadyDurationMicros >= DISCONNECTED_TIMEOUT_MICROS &&
                nowMicros - lastTimeoutReportMicros >= DISCONNECTED_TIMEOUT_MICROS) {
                timeoutCount.fetch_add(1, std::memory_order_relaxed);
                doutHighTimeoutCount.fetch_add(1, std::memory_order_relaxed);
                lastTimeoutReportMicros = nowMicros;
            }

#if WMBP_ACQUISITION_DOUT_INTERRUPT
            if (interruptAttached) {
                continue;
            }
#endif
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

#if WMBP_ACQUISITION_DOUT_INTERRUPT
        bool levelRecovered = false;
        if (interruptAttached && notifications == 0) {
            levelRecovered = true;
            readyRecoveredByLevelPollCount.fetch_add(1, std::memory_order_relaxed);
        }
        if (interruptAttached) {
            disableDataReadyInterrupt();
            (void)ulTaskNotifyTake(pdTRUE, 0);
            clearPendingDataReadyInterrupt();
        }
#else
        constexpr bool levelRecovered = false;
#endif

        readyCount.fetch_add(1, std::memory_order_relaxed);
        const uint32_t readStartMicros = micros();
        int32_t rawCounts = 0;
        uint32_t readDurationMicros = 0;
        readInProgress = true;
        const Hx711Io::Status status = io.readRaw(rawCounts, &readDurationMicros);
        readInProgress = false;
        const uint32_t readEndMicros = micros();
        lastNotReadyStartMicros = readEndMicros;

#if WMBP_ACQUISITION_DOUT_INTERRUPT
        if (interruptAttached) {
            clearPendingDataReadyInterrupt();
            const bool readyAfterRead = io.isReady();
            enableDataReadyInterrupt();
            if (readyAfterRead || io.isReady()) {
                notifyTaskFromTask();
            }
        }
#endif

        if (status == Hx711Io::Status::Ok) {
            recordReadDuration(readDurationMicros);
            const uint32_t sampleSequence = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            rawReadCount.fetch_add(1, std::memory_order_relaxed);

            RawSample sample;
            sample.rawCounts = rawCounts;
            sample.readStartMicros = readStartMicros;
            sample.readEndMicros = readEndMicros;
            sample.readDurationMicros = readDurationMicros;
            sample.sequence = sampleSequence;
            sample.levelRecovered = levelRecovered;
            publishSample(sample);
        } else if (status == Hx711Io::Status::Disconnected) {
            disconnectedCount.fetch_add(1, std::memory_order_relaxed);
            readErrorCount.fetch_add(1, std::memory_order_relaxed);
        } else if (status == Hx711Io::Status::Timeout) {
            timeoutCount.fetch_add(1, std::memory_order_relaxed);
            readErrorCount.fetch_add(1, std::memory_order_relaxed);
        } else if (status != Hx711Io::Status::NotReady) {
            readErrorCount.fetch_add(1, std::memory_order_relaxed);
        }

        taskYIELD();
    }
}

void Hx711Acquisition::publishSample(const RawSample& sample) {
    if (sampleQueue == nullptr) {
        return;
    }

    if (xQueueSend(sampleQueue, &sample, 0) == pdTRUE) {
        return;
    }

    RawSample discarded;
    (void)xQueueReceive(sampleQueue, &discarded, 0);
    if (xQueueSend(sampleQueue, &sample, 0) == pdTRUE) {
        queueDropCount.fetch_add(1, std::memory_order_relaxed);
    }
}

void Hx711Acquisition::recordReadDuration(uint32_t readDurationMicros) {
    lastReadDurationMicros.store(readDurationMicros, std::memory_order_relaxed);
    uint32_t currentMax = maxReadDurationMicros.load(std::memory_order_relaxed);
    while (readDurationMicros > currentMax &&
           !maxReadDurationMicros.compare_exchange_weak(currentMax,
                                                        readDurationMicros,
                                                        std::memory_order_relaxed)) {
    }
}

void Hx711Acquisition::attachDataReadyInterrupt() {
#if WMBP_ACQUISITION_DOUT_INTERRUPT
    if (interruptAttached) {
        return;
    }
    clearPendingDataReadyInterrupt();
    attachInterruptArg(dataPin, dataReadyIsr, this, FALLING);
    interruptAttached = true;
    if (io.isReady()) {
        notifyTaskFromTask();
    }
#endif
}

void Hx711Acquisition::detachDataReadyInterrupt() {
#if WMBP_ACQUISITION_DOUT_INTERRUPT
    if (!interruptAttached) {
        return;
    }
    detachInterrupt(dataPin);
    clearPendingDataReadyInterrupt();
    interruptAttached = false;
#endif
}

void Hx711Acquisition::disableDataReadyInterrupt() {
#if WMBP_ACQUISITION_DOUT_INTERRUPT
    if (!interruptAttached) {
        return;
    }
    gpio_intr_disable(static_cast<gpio_num_t>(dataPin));
#endif
}

void Hx711Acquisition::enableDataReadyInterrupt() {
#if WMBP_ACQUISITION_DOUT_INTERRUPT
    if (!interruptAttached) {
        return;
    }
    clearPendingDataReadyInterrupt();
    gpio_intr_enable(static_cast<gpio_num_t>(dataPin));
#endif
}

void Hx711Acquisition::clearPendingDataReadyInterrupt() {
#if WMBP_ACQUISITION_DOUT_INTERRUPT
    if (dataPin < 32) {
        GPIO.status_w1tc = (1UL << dataPin);
    } else {
        GPIO.status1_w1tc.val = (1UL << (dataPin - 32));
    }
#endif
}

void Hx711Acquisition::notifyTaskFromTask() {
    if (taskHandle == nullptr) {
        return;
    }
    xTaskNotifyGive(taskHandle);
}
