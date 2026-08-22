#ifndef POUR_OVER_COMMAND_QUEUE_H
#define POUR_OVER_COMMAND_QUEUE_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "PourOverSession.h"

class PourOverCommandQueue {
public:
    enum class Command : uint8_t {
        Start,
        Pause,
        Resume,
        Next,
        Previous,
        Finish,
        Reset,
    };

    bool requestRecipe(const PourOverRecipe& recipe);
    bool requestCommand(Command command);
    void process(PourOverSession& session, uint32_t nowMs, float weightGrams);

private:
    static constexpr uint8_t COMMAND_CAPACITY = 8;
    mutable portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    bool recipePending = false;
    PourOverRecipe pendingRecipe = {};
    Command commands[COMMAND_CAPACITY] = {};
    uint8_t commandRead = 0;
    uint8_t commandWrite = 0;
    uint8_t commandCount = 0;
};

#endif
