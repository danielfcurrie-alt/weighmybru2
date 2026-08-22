#include "PourOverCommandQueue.h"

bool PourOverCommandQueue::requestRecipe(const PourOverRecipe& recipe) {
    if (!PourOverSession::validateRecipe(recipe)) {
        return false;
    }
    portENTER_CRITICAL(&mux);
    pendingRecipe = recipe;
    recipePending = true;
    portEXIT_CRITICAL(&mux);
    return true;
}

bool PourOverCommandQueue::requestCommand(Command command) {
    portENTER_CRITICAL(&mux);
    if (commandCount >= COMMAND_CAPACITY) {
        portEXIT_CRITICAL(&mux);
        return false;
    }
    commands[commandWrite] = command;
    commandWrite = static_cast<uint8_t>((commandWrite + 1) % COMMAND_CAPACITY);
    commandCount++;
    portEXIT_CRITICAL(&mux);
    return true;
}

void PourOverCommandQueue::process(PourOverSession& session, uint32_t nowMs,
                                   float weightGrams) {
    bool applyRecipe = false;
    PourOverRecipe recipe = {};
    Command pendingCommands[COMMAND_CAPACITY] = {};
    uint8_t pendingCount = 0;

    portENTER_CRITICAL(&mux);
    if (recipePending) {
        applyRecipe = true;
        recipe = pendingRecipe;
        recipePending = false;
    }
    while (commandCount > 0 && pendingCount < COMMAND_CAPACITY) {
        pendingCommands[pendingCount++] = commands[commandRead];
        commandRead = static_cast<uint8_t>((commandRead + 1) % COMMAND_CAPACITY);
        commandCount--;
    }
    portEXIT_CRITICAL(&mux);

    if (applyRecipe) {
        session.setRecipe(recipe);
    }
    for (uint8_t i = 0; i < pendingCount; i++) {
        switch (pendingCommands[i]) {
            case Command::Start: session.start(nowMs, weightGrams); break;
            case Command::Pause: session.pause(nowMs); break;
            case Command::Resume: session.resume(nowMs); break;
            case Command::Next: session.next(nowMs, weightGrams); break;
            case Command::Previous: session.previous(nowMs, weightGrams); break;
            case Command::Finish: session.finish(nowMs, weightGrams); break;
            case Command::Reset: session.reset(); break;
        }
    }
}
