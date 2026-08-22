#include "PourOverSession.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
PourOverRecipe recipe() {
    PourOverRecipe value;
    strcpy(value.name, "Test brew");
    value.stageCount = 3;
    value.stages[0].type = PourOverStageType::Pour;
    strcpy(value.stages[0].name, "Bloom");
    value.stages[0].targetGrams = 50.0f;
    value.stages[0].durationMs = 10000;
    value.stages[0].flowMin = 2.0f;
    value.stages[0].flowMax = 6.0f;
    value.stages[1].type = PourOverStageType::Pause;
    strcpy(value.stages[1].name, "Bloom pause");
    value.stages[1].durationMs = 30000;
    value.stages[2].type = PourOverStageType::Drawdown;
    strcpy(value.stages[2].name, "Drawdown");
    value.stages[2].durationMs = 60000;
    return value;
}

bool closeTo(float actual, float expected) {
    return fabsf(actual - expected) < 0.01f;
}
}

int main() {
    PourOverSession session;
    assert(session.setRecipe(recipe()));
    assert(session.status() == PourOverStatus::Ready);
    assert(session.start(1000, 10.0f));

    session.update(1500, 35.0f, 3.0f, true);
    assert(session.currentStageIndex() == 0);
    session.update(1700, 61.0f, 4.0f, true);
    assert(session.currentStageIndex() == 1);
    assert(session.result(0).complete);
    assert(closeTo(session.result(0).actualAddedGrams, 51.0f));
    assert(session.result(0).durationMs == 700);

    session.update(11700, 61.0f, 0.0f, true);
    assert(session.pause(11700));
    const uint32_t pausedStageMs = session.stageElapsedMs();
    session.update(21700, 61.0f, 0.0f, false);
    assert(session.stageElapsedMs() == pausedStageMs);
    assert(session.resume(21700));
    session.update(41700, 61.0f, 0.0f, true);
    assert(session.currentStageIndex() == 2);
    assert(session.result(1).complete);

    assert(session.previous(42000, 61.0f));
    assert(session.currentStageIndex() == 1);
    assert(!session.result(1).complete);
    assert(session.next(43000, 61.0f));
    assert(session.currentStageIndex() == 2);
    assert(session.finish(44000, 61.0f));
    assert(session.status() == PourOverStatus::Finished);

    session.reset();
    assert(session.status() == PourOverStatus::Ready);
    assert(session.start(UINT32_MAX - 100, 0.0f));
    session.update(50, 1.0f, 1.0f, true);
    assert(session.stageElapsedMs() == 151);

    PourOverRecipe invalid = recipe();
    invalid.stages[0].targetGrams = 0.0f;
    assert(!session.setRecipe(invalid));
    printf("pour-over session tests passed\n");
    return 0;
}
