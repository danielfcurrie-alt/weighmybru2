#include "PourOverSession.h"

#include <math.h>
#include <string.h>

namespace {
const PourOverStageResult EMPTY_RESULT = {};
}

bool PourOverSession::validateRecipe(const PourOverRecipe& recipe) {
    if (recipe.stageCount == 0 || recipe.stageCount > PourOverRecipe::MAX_STAGES ||
        recipe.name[0] == '\0') {
        return false;
    }
    for (uint8_t i = 0; i < recipe.stageCount; i++) {
        const PourOverStage& stage = recipe.stages[i];
        if (stage.name[0] == '\0' || !isfinite(stage.targetGrams) ||
            !isfinite(stage.flowMin) || !isfinite(stage.flowMax) ||
            stage.targetGrams < 0.0f || stage.targetGrams > 2000.0f ||
            stage.durationMs > 3600000UL || stage.flowMin < 0.0f ||
            stage.flowMax < stage.flowMin || stage.flowMax > 30.0f) {
            return false;
        }
        if (stage.type == PourOverStageType::Pour && stage.targetGrams <= 0.0f) {
            return false;
        }
    }
    return true;
}

bool PourOverSession::setRecipe(const PourOverRecipe& recipe) {
    if (!validateRecipe(recipe)) {
        return false;
    }
    activeRecipe = recipe;
    memset(results, 0, sizeof(results));
    sessionStatus = PourOverStatus::Ready;
    stageIndex = 0;
    currentStageElapsedMs = 0;
    totalSessionElapsedMs = 0;
    lastUpdateMs = 0;
    stageBaselineWeight = 0.0f;
    liveWeight = 0.0f;
    liveFlow = 0.0f;
    stageFlowSum = 0.0f;
    stageFlowSamples = 0;
    peakFlow = 0.0f;
    transitions++;
    return true;
}

bool PourOverSession::start(uint32_t nowMs, float weightGrams) {
    if (!hasRecipe()) {
        return false;
    }
    memset(results, 0, sizeof(results));
    sessionStatus = PourOverStatus::Running;
    stageIndex = 0;
    totalSessionElapsedMs = 0;
    peakFlow = 0.0f;
    beginStage(nowMs, weightGrams);
    transitions++;
    return true;
}

bool PourOverSession::pause(uint32_t nowMs) {
    if (sessionStatus != PourOverStatus::Running) {
        return false;
    }
    advanceClock(nowMs);
    sessionStatus = PourOverStatus::Paused;
    transitions++;
    return true;
}

bool PourOverSession::resume(uint32_t nowMs) {
    if (sessionStatus != PourOverStatus::Paused) {
        return false;
    }
    lastUpdateMs = nowMs;
    sessionStatus = PourOverStatus::Running;
    transitions++;
    return true;
}

bool PourOverSession::next(uint32_t nowMs, float weightGrams) {
    if (sessionStatus != PourOverStatus::Running &&
        sessionStatus != PourOverStatus::Paused) {
        return false;
    }
    if (sessionStatus == PourOverStatus::Running) {
        advanceClock(nowMs);
    }
    liveWeight = weightGrams;
    captureCurrentResult();
    if (stageIndex + 1 >= activeRecipe.stageCount) {
        sessionStatus = PourOverStatus::Finished;
        lastUpdateMs = nowMs;
        transitions++;
        return true;
    }
    stageIndex++;
    sessionStatus = PourOverStatus::Running;
    beginStage(nowMs, weightGrams);
    transitions++;
    return true;
}

bool PourOverSession::previous(uint32_t nowMs, float weightGrams) {
    if ((sessionStatus != PourOverStatus::Running &&
         sessionStatus != PourOverStatus::Paused) || stageIndex == 0) {
        return false;
    }
    if (sessionStatus == PourOverStatus::Running) {
        advanceClock(nowMs);
    }
    results[stageIndex] = {};
    stageIndex--;
    results[stageIndex] = {};
    sessionStatus = PourOverStatus::Running;
    beginStage(nowMs, weightGrams);
    transitions++;
    return true;
}

bool PourOverSession::finish(uint32_t nowMs, float weightGrams) {
    if (sessionStatus != PourOverStatus::Running &&
        sessionStatus != PourOverStatus::Paused) {
        return false;
    }
    if (sessionStatus == PourOverStatus::Running) {
        advanceClock(nowMs);
    }
    liveWeight = weightGrams;
    captureCurrentResult();
    sessionStatus = PourOverStatus::Finished;
    lastUpdateMs = nowMs;
    transitions++;
    return true;
}

void PourOverSession::reset() {
    const bool recipeAvailable = hasRecipe();
    memset(results, 0, sizeof(results));
    sessionStatus = recipeAvailable ? PourOverStatus::Ready : PourOverStatus::Idle;
    stageIndex = 0;
    currentStageElapsedMs = 0;
    totalSessionElapsedMs = 0;
    lastUpdateMs = 0;
    stageBaselineWeight = 0.0f;
    liveFlow = 0.0f;
    stageFlowSum = 0.0f;
    stageFlowSamples = 0;
    peakFlow = 0.0f;
    transitions++;
}

void PourOverSession::update(uint32_t nowMs, float weightGrams,
                             float flowGramsPerSecond, bool freshSample) {
    liveWeight = isfinite(weightGrams) ? weightGrams : liveWeight;
    liveFlow = isfinite(flowGramsPerSecond) ? flowGramsPerSecond : 0.0f;
    if (sessionStatus != PourOverStatus::Running) {
        lastUpdateMs = nowMs;
        return;
    }

    advanceClock(nowMs);
    if (freshSample && isfinite(flowGramsPerSecond)) {
        const float positiveFlow = flowGramsPerSecond > 0.0f ? flowGramsPerSecond : 0.0f;
        stageFlowSum += positiveFlow;
        stageFlowSamples++;
        if (positiveFlow > peakFlow) {
            peakFlow = positiveFlow;
        }
    }
    if (shouldAutoAdvance()) {
        next(nowMs, liveWeight);
    }
}

const PourOverStage* PourOverSession::currentStage() const {
    return hasRecipe() && stageIndex < activeRecipe.stageCount
        ? &activeRecipe.stages[stageIndex]
        : nullptr;
}

const PourOverStageResult& PourOverSession::result(size_t index) const {
    return index < PourOverRecipe::MAX_STAGES ? results[index] : EMPTY_RESULT;
}

float PourOverSession::currentAddedGrams() const {
    const float added = liveWeight - stageBaselineWeight;
    return added > 0.0f ? added : 0.0f;
}

float PourOverSession::averageStageFlowGramsPerSecond() const {
    return stageFlowSamples > 0 ? stageFlowSum / static_cast<float>(stageFlowSamples) : 0.0f;
}

const char* PourOverSession::stageTypeName(PourOverStageType type) {
    switch (type) {
        case PourOverStageType::Pour: return "pour";
        case PourOverStageType::Pause: return "pause";
        case PourOverStageType::Agitate: return "agitate";
        case PourOverStageType::Drawdown: return "drawdown";
    }
    return "pause";
}

const char* PourOverSession::statusName(PourOverStatus status) {
    switch (status) {
        case PourOverStatus::Idle: return "idle";
        case PourOverStatus::Ready: return "ready";
        case PourOverStatus::Running: return "running";
        case PourOverStatus::Paused: return "paused";
        case PourOverStatus::Finished: return "finished";
    }
    return "idle";
}

void PourOverSession::advanceClock(uint32_t nowMs) {
    const uint32_t deltaMs = nowMs - lastUpdateMs;
    currentStageElapsedMs += deltaMs;
    totalSessionElapsedMs += deltaMs;
    lastUpdateMs = nowMs;
}

void PourOverSession::beginStage(uint32_t nowMs, float weightGrams) {
    currentStageElapsedMs = 0;
    lastUpdateMs = nowMs;
    stageBaselineWeight = isfinite(weightGrams) ? weightGrams : liveWeight;
    liveWeight = stageBaselineWeight;
    liveFlow = 0.0f;
    stageFlowSum = 0.0f;
    stageFlowSamples = 0;
}

void PourOverSession::captureCurrentResult() {
    if (stageIndex >= activeRecipe.stageCount) {
        return;
    }
    PourOverStageResult& captured = results[stageIndex];
    captured.complete = true;
    captured.actualAddedGrams = currentAddedGrams();
    captured.durationMs = currentStageElapsedMs;
    captured.averageFlow = averageStageFlowGramsPerSecond();
    captured.completedAtMs = totalSessionElapsedMs;
}

bool PourOverSession::shouldAutoAdvance() const {
    const PourOverStage* stage = currentStage();
    if (stage == nullptr || !stage->autoAdvance || currentStageElapsedMs < 600UL) {
        return false;
    }
    if (stage->type == PourOverStageType::Pour) {
        return currentAddedGrams() >= stage->targetGrams;
    }
    return stage->durationMs > 0 && currentStageElapsedMs >= stage->durationMs;
}
