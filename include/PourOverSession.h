#ifndef POUR_OVER_SESSION_H
#define POUR_OVER_SESSION_H

#include <stddef.h>
#include <stdint.h>

enum class PourOverStageType : uint8_t {
    Pour,
    Pause,
    Agitate,
    Drawdown,
};

enum class PourOverStatus : uint8_t {
    Idle,
    Ready,
    Running,
    Paused,
    Finished,
};

struct PourOverStage {
    PourOverStageType type = PourOverStageType::Pause;
    char name[32] = {};
    float targetGrams = 0.0f;
    uint32_t durationMs = 0;
    float flowMin = 0.0f;
    float flowMax = 0.0f;
    bool autoAdvance = true;
};

struct PourOverRecipe {
    static constexpr size_t MAX_STAGES = 12;

    char name[32] = {};
    PourOverStage stages[MAX_STAGES] = {};
    uint8_t stageCount = 0;
};

struct PourOverStageResult {
    bool complete = false;
    float actualAddedGrams = 0.0f;
    uint32_t durationMs = 0;
    float averageFlow = 0.0f;
    uint32_t completedAtMs = 0;
};

class PourOverSession {
public:
    bool setRecipe(const PourOverRecipe& recipe);
    static bool validateRecipe(const PourOverRecipe& recipe);

    bool start(uint32_t nowMs, float weightGrams);
    bool pause(uint32_t nowMs);
    bool resume(uint32_t nowMs);
    bool next(uint32_t nowMs, float weightGrams);
    bool previous(uint32_t nowMs, float weightGrams);
    bool finish(uint32_t nowMs, float weightGrams);
    void reset();
    void update(uint32_t nowMs, float weightGrams, float flowGramsPerSecond,
                bool freshSample);

    const PourOverRecipe& recipe() const { return activeRecipe; }
    const PourOverStage* currentStage() const;
    const PourOverStageResult& result(size_t index) const;
    PourOverStatus status() const { return sessionStatus; }
    uint8_t currentStageIndex() const { return stageIndex; }
    uint32_t stageElapsedMs() const { return currentStageElapsedMs; }
    uint32_t totalElapsedMs() const { return totalSessionElapsedMs; }
    float stageBaselineWeightGrams() const { return stageBaselineWeight; }
    float currentAddedGrams() const;
    float currentWeightGrams() const { return liveWeight; }
    float currentFlowGramsPerSecond() const { return liveFlow; }
    float peakFlowGramsPerSecond() const { return peakFlow; }
    float averageStageFlowGramsPerSecond() const;
    uint32_t transitionCount() const { return transitions; }
    bool hasRecipe() const { return activeRecipe.stageCount > 0; }

    static const char* stageTypeName(PourOverStageType type);
    static const char* statusName(PourOverStatus status);

private:
    PourOverRecipe activeRecipe = {};
    PourOverStageResult results[PourOverRecipe::MAX_STAGES] = {};
    PourOverStatus sessionStatus = PourOverStatus::Idle;
    uint8_t stageIndex = 0;
    uint32_t currentStageElapsedMs = 0;
    uint32_t totalSessionElapsedMs = 0;
    uint32_t lastUpdateMs = 0;
    float stageBaselineWeight = 0.0f;
    float liveWeight = 0.0f;
    float liveFlow = 0.0f;
    float stageFlowSum = 0.0f;
    uint32_t stageFlowSamples = 0;
    float peakFlow = 0.0f;
    uint32_t transitions = 0;

    void advanceClock(uint32_t nowMs);
    void beginStage(uint32_t nowMs, float weightGrams);
    void captureCurrentResult();
    bool shouldAutoAdvance() const;
};

#endif
