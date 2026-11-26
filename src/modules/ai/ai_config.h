#ifndef __AI_CONFIG_H__
#define __AI_CONFIG_H__

#if !defined(LITE_VERSION)

#include <Arduino.h>
#include <FS.h>
#include <vector>

struct AIPreset {
    String label;
    String prompt;
};

struct AIConfig {
    String gatewayUrl = "";
    String deviceToken = "";
    String deviceId = "tembed-cc1101";
    uint32_t timeoutMs = 30000;
    uint32_t voiceMaxMs = 8000;
    float voiceGain = 2.0f;
    bool insecureTls = true;
    String caCertPath = "/BruceAI/ca_cert.pem";

    bool isConfigured() const;
    String maskedToken() const;
};

namespace ai {

constexpr const char *AI_DIR_PATH = "/BruceAI";
constexpr const char *AI_CONFIG_PATH = "/BruceAI/config.json";
constexpr const char *AI_PRESETS_PATH = "/BruceAI/presets.json";
constexpr const char *AI_VOICE_TMP_PATH = "/BruceAI/tmp_voice.wav";

/**
 * Loads /BruceAI/config.json from SD (falling back to LittleFS).
 * If missing, creates /BruceAI and writes a template config.json.
 * @return true if config was loaded from an existing file, false if template was newly created or storage unavailable.
 */
bool loadConfig(AIConfig &outConfig, FS **outFs = nullptr);

/**
 * Saves AIConfig to /BruceAI/config.json on SD (falling back to LittleFS).
 */
bool saveConfig(const AIConfig &config);

/**
 * Loads /BruceAI/presets.json from SD (falling back to LittleFS).
 * If missing, creates a default presets.json and populates outPresets.
 */
bool loadPresets(std::vector<AIPreset> &outPresets);

/**
 * Reads optional PEM CA certificate from config.caCertPath if insecureTls == false.
 */
String loadCaCert(const AIConfig &config);

} // namespace ai

#endif // !defined(LITE_VERSION)
#endif // __AI_CONFIG_H__
