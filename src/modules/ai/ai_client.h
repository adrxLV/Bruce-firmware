#ifndef __AI_CLIENT_H__
#define __AI_CLIENT_H__

#if !defined(LITE_VERSION)

#include "ai_config.h"
#include <Arduino.h>
#include <FS.h>
#include <vector>

struct AIPctStep {
    String id;
    String tool;
    String argsJson;
    String onMissingVar = "abort";
};

struct AIResponse {
    bool ok = false;
    int httpCode = 0;
    String error;
    String sessionId;
    String transcript;
    String reply;
    std::vector<AIPctStep> steps;
};

namespace ai {

/**
 * Ensures Wi-Fi STA is connected (attempts wifiConnecttoKnownNet() if disconnected).
 */
bool ensureWifiReady();

/**
 * Calls GET {gatewayUrl}/api/v1/health with Bearer auth.
 */
bool checkGatewayHealth(const AIConfig &cfg, String &outStatus);

/**
 * Calls POST {gatewayUrl}/api/v1/chat with JSON prompt + device context.
 */
bool sendTextPrompt(
    const AIConfig &cfg, const String &prompt, const String &sessionId, AIResponse &outResponse
);

/**
 * Calls POST {gatewayUrl}/api/v1/voice streaming the WAV file directly from SD/LittleFS.
 */
bool sendVoiceWav(
    const AIConfig &cfg, FS *fs, const String &wavPath, const String &sessionId, AIResponse &outResponse
);

} // namespace ai

#endif // !defined(LITE_VERSION)
#endif // __AI_CLIENT_H__
