#ifndef __AI_TOOLS_H__
#define __AI_TOOLS_H__

#if !defined(LITE_VERSION)

#include "ai_client.h"
#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>

struct AIPctStepResult {
    String id;
    String tool;
    bool ok = false;
    String summary;
    std::map<String, String> outputs;
};

struct AIPctBatchResult {
    bool allOk = true;
    bool aborted = false;
    std::vector<AIPctStepResult> stepResults;
};

namespace ai {

/**
 * Executes a PCT batch of steps autonomously with ${step_id.key} variable interpolation.
 * Checks ESC between steps to allow immediate user abort.
 */
AIPctBatchResult executePctBatch(
    const std::vector<AIPctStep> &steps, std::function<void(const String &)> onLog = nullptr
);

} // namespace ai

#endif // !defined(LITE_VERSION)
#endif // __AI_TOOLS_H__
