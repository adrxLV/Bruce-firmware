#if !defined(LITE_VERSION)

#include "ai_config.h"
#include "core/sd_functions.h"
#include <ArduinoJson.h>
#include <globals.h>

static constexpr const char *PLACEHOLDER_URL = "https://your-tunnel.example.com";
static constexpr const char *PLACEHOLDER_TOKEN = "REPLACE_WITH_DEVICE_SECRET";

bool AIConfig::isConfigured() const {
    if (gatewayUrl.isEmpty() || gatewayUrl == PLACEHOLDER_URL) return false;
    if (!(gatewayUrl.startsWith("https://") || gatewayUrl.startsWith("http://"))) return false;
    if (deviceToken.isEmpty() || deviceToken == PLACEHOLDER_TOKEN) return false;
    return true;
}

String AIConfig::maskedToken() const {
    if (deviceToken.isEmpty() || deviceToken == PLACEHOLDER_TOKEN) return "(not set)";
    if (deviceToken.length() <= 6) return "******";
    return deviceToken.substring(0, 3) + "..." + deviceToken.substring(deviceToken.length() - 3);
}

namespace ai {

static void ensureAiDir(FS *fs) {
    if (!fs) return;
    if (!fs->exists(AI_DIR_PATH)) { fs->mkdir(AI_DIR_PATH); }
}

static bool writeDefaultConfigTemplate(FS *fs) {
    if (!fs) return false;
    ensureAiDir(fs);
    File f = fs->open(AI_CONFIG_PATH, FILE_WRITE);
    if (!f) return false;

    JsonDocument doc;
    doc["gateway_url"] = PLACEHOLDER_URL;
    doc["device_token"] = PLACEHOLDER_TOKEN;
    doc["device_id"] = "tembed-cc1101";
    doc["timeout_ms"] = 30000;
    doc["voice_max_ms"] = 8000;
    doc["voice_gain"] = 2.0;
    doc["insecure_tls"] = true;
    doc["ca_cert_path"] = "/BruceAI/ca_cert.pem";

    serializeJsonPretty(doc, f);
    f.println();
    f.close();
    return true;
}

static void populateDefaultPresets(std::vector<AIPreset> &outPresets) {
    outPresets = {
        {"Portfolio Portal", "Inicia um Wi-Fi portal do meu portfolio"},
        {"List SD Portals",  "Procura ficheiros HTML no cartao SD e lista-os"},
        {"Wi-Fi Scan",       "Faz um scan as redes Wi-Fi por perto e resume os resultados"},
        {"Device Status",    "Mostra o estado atual do dispositivo, bateria, RAM e Wi-Fi"},
        {"LEDs Cyan",        "Muda os LEDs RGB para ciano"},
    };
}

static bool writeDefaultPresetsTemplate(FS *fs, const std::vector<AIPreset> &presets) {
    if (!fs) return false;
    ensureAiDir(fs);
    File f = fs->open(AI_PRESETS_PATH, FILE_WRITE);
    if (!f) return false;

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto &p : presets) {
        JsonObject item = arr.add<JsonObject>();
        item["label"] = p.label;
        item["prompt"] = p.prompt;
    }

    serializeJsonPretty(doc, f);
    f.println();
    f.close();
    return true;
}

bool loadConfig(AIConfig &outConfig, FS **outFs) {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        if (outFs) *outFs = nullptr;
        return false;
    }
    if (outFs) *outFs = fs;

    if (!fs->exists(AI_CONFIG_PATH)) {
        writeDefaultConfigTemplate(fs);
        outConfig = AIConfig();
        outConfig.gatewayUrl = PLACEHOLDER_URL;
        outConfig.deviceToken = PLACEHOLDER_TOKEN;
        return false;
    }

    File f = fs->open(AI_CONFIG_PATH, FILE_READ);
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[AI] Failed to parse %s: %s\n", AI_CONFIG_PATH, err.c_str());
        return false;
    }

    outConfig.gatewayUrl = doc["gateway_url"] | String("");
    outConfig.gatewayUrl.trim();
    while (outConfig.gatewayUrl.endsWith("/")) {
        outConfig.gatewayUrl.remove(outConfig.gatewayUrl.length() - 1);
    }

    outConfig.deviceToken = doc["device_token"] | String("");
    outConfig.deviceToken.trim();

    outConfig.deviceId = doc["device_id"] | String("tembed-cc1101");
    outConfig.timeoutMs = doc["timeout_ms"] | 30000U;
    if (outConfig.timeoutMs < 3000U) outConfig.timeoutMs = 3000U;
    if (outConfig.timeoutMs > 120000U) outConfig.timeoutMs = 120000U;

    outConfig.voiceMaxMs = doc["voice_max_ms"] | 8000U;
    if (outConfig.voiceMaxMs < 1000U) outConfig.voiceMaxMs = 1000U;
    if (outConfig.voiceMaxMs > 30000U) outConfig.voiceMaxMs = 30000U;

    outConfig.voiceGain = doc["voice_gain"] | 2.0f;
    if (outConfig.voiceGain < 0.5f) outConfig.voiceGain = 0.5f;
    if (outConfig.voiceGain > 4.0f) outConfig.voiceGain = 4.0f;

    outConfig.insecureTls = doc["insecure_tls"] | true;
    outConfig.caCertPath = doc["ca_cert_path"] | String("/BruceAI/ca_cert.pem");

    return true;
}

bool saveConfig(const AIConfig &config) {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) return false;
    ensureAiDir(fs);

    File f = fs->open(AI_CONFIG_PATH, FILE_WRITE);
    if (!f) return false;

    JsonDocument doc;
    doc["gateway_url"] = config.gatewayUrl;
    doc["device_token"] = config.deviceToken;
    doc["device_id"] = config.deviceId;
    doc["timeout_ms"] = config.timeoutMs;
    doc["voice_max_ms"] = config.voiceMaxMs;
    doc["voice_gain"] = config.voiceGain;
    doc["insecure_tls"] = config.insecureTls;
    doc["ca_cert_path"] = config.caCertPath;

    serializeJsonPretty(doc, f);
    f.println();
    f.close();
    return true;
}

bool loadPresets(std::vector<AIPreset> &outPresets) {
    outPresets.clear();
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        populateDefaultPresets(outPresets);
        return false;
    }

    if (!fs->exists(AI_PRESETS_PATH)) {
        populateDefaultPresets(outPresets);
        writeDefaultPresetsTemplate(fs, outPresets);
        return true;
    }

    File f = fs->open(AI_PRESETS_PATH, FILE_READ);
    if (!f) {
        populateDefaultPresets(outPresets);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc.is<JsonArray>()) {
        populateDefaultPresets(outPresets);
        return false;
    }

    for (JsonVariant item : doc.as<JsonArray>()) {
        String label = item["label"] | String("");
        String prompt = item["prompt"] | String("");
        if (!label.isEmpty() && !prompt.isEmpty()) { outPresets.push_back({label, prompt}); }
    }

    if (outPresets.empty()) { populateDefaultPresets(outPresets); }
    return true;
}

String loadCaCert(const AIConfig &config) {
    if (config.caCertPath.isEmpty()) return "";
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs || !fs->exists(config.caCertPath)) return "";
    return readSmallFile(*fs, config.caCertPath);
}

} // namespace ai

#endif // !defined(LITE_VERSION)
