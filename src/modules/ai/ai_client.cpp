#if !defined(LITE_VERSION)

#include "ai_client.h"
#include "core/wifi/wifi_common.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <globals.h>

namespace ai {

bool ensureWifiReady() {
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        return true;
    }
    if (wifiConnecttoKnownNet() && WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        return true;
    }
    return false;
}

static bool beginHttpClient(
    HTTPClient &http, WiFiClient &plainClient, WiFiClientSecure &secureClient, const AIConfig &cfg,
    const String &url, String &caCertStorage
) {
    http.setTimeout(cfg.timeoutMs);
    http.setConnectTimeout(10000);
    http.setReuse(false);

    if (url.startsWith("https://")) {
        if (cfg.insecureTls) {
            secureClient.setInsecure();
        } else {
            caCertStorage = loadCaCert(cfg);
            if (!caCertStorage.isEmpty()) {
                secureClient.setCACert(caCertStorage.c_str());
            } else {
                secureClient.setInsecure();
            }
        }
        return http.begin(secureClient, url);
    }
    return http.begin(plainClient, url);
}

static void addAuthHeaders(HTTPClient &http, const AIConfig &cfg, const String &sessionId = "") {
    if (!cfg.deviceToken.isEmpty()) { http.addHeader("Authorization", "Bearer " + cfg.deviceToken); }
    http.addHeader("X-Device-Id", cfg.deviceId);
    http.addHeader("X-Bruce-Device", DEVICE_NAME);
    if (!sessionId.isEmpty()) { http.addHeader("X-Session-Id", sessionId); }
}

static bool parseGatewayResponse(const String &body, int httpCode, AIResponse &out) {
    out.httpCode = httpCode;
    if (body.isEmpty()) {
        out.ok = false;
        out.error = "Empty response (HTTP " + String(httpCode) + ")";
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        out.ok = false;
        out.error = "Invalid JSON (HTTP " + String(httpCode) + "): " + String(err.c_str());
        return false;
    }

    out.ok = doc["ok"] | (httpCode >= 200 && httpCode < 300);
    out.error = doc["error"] | String("");
    out.sessionId = doc["session_id"] | String("");
    out.transcript = doc["transcript"] | String("");
    out.reply = doc["reply"] | String("");

    out.steps.clear();
    if (doc["steps"].is<JsonArray>()) {
        int idx = 1;
        for (JsonVariant stepVar : doc["steps"].as<JsonArray>()) {
            if (!stepVar.is<JsonObject>()) continue;
            AIPctStep step;
            String defaultId = "s" + String(idx);
            step.id = stepVar["id"] | defaultId;
            step.tool = stepVar["tool"] | String("");
            step.onMissingVar = stepVar["on_missing_var"] | String("abort");

            if (stepVar["args"].is<JsonObject>()) {
                serializeJson(stepVar["args"], step.argsJson);
            } else {
                step.argsJson = "{}";
            }

            if (!step.tool.isEmpty()) { out.steps.push_back(step); }
            idx++;
        }
    }

    if (!out.ok && out.error.isEmpty()) { out.error = "Gateway error (HTTP " + String(httpCode) + ")"; }
    return out.ok;
}

bool checkGatewayHealth(const AIConfig &cfg, String &outStatus) {
    if (!cfg.isConfigured()) {
        outStatus = "Not configured (/BruceAI/config.json)";
        return false;
    }
    if (!ensureWifiReady()) {
        outStatus = "Wi-Fi not connected";
        return false;
    }

    HTTPClient http;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    String caCert;

    String url = cfg.gatewayUrl + "/api/v1/health";
    if (!beginHttpClient(http, plainClient, secureClient, cfg, url, caCert)) {
        outStatus = "HTTP begin failed";
        return false;
    }
    addAuthHeaders(http, cfg);

    int code = http.GET();
    String payload = http.getString();
    http.end();

    if (code >= 200 && code < 300) {
        JsonDocument doc;
        if (!deserializeJson(doc, payload)) {
            String model = doc["model"] | String("");
            String status = doc["status"] | String("ok");
            outStatus = model.isEmpty() ? ("OK (" + status + ")") : ("OK (" + model + ")");
        } else {
            outStatus = "OK (HTTP " + String(code) + ")";
        }
        return true;
    }

    outStatus = "Fail (HTTP " + String(code) + ")";
    return false;
}

bool sendTextPrompt(
    const AIConfig &cfg, const String &prompt, const String &sessionId, AIResponse &outResponse
) {
    outResponse = AIResponse();
    if (!cfg.isConfigured()) {
        outResponse.error = "Configure /BruceAI/config.json first";
        return false;
    }
    if (!ensureWifiReady()) {
        outResponse.error = "Wi-Fi not connected";
        return false;
    }

    JsonDocument reqDoc;
    reqDoc["prompt"] = prompt;
    if (!sessionId.isEmpty()) reqDoc["session_id"] = sessionId;

    JsonObject dev = reqDoc["device"].to<JsonObject>();
    dev["id"] = cfg.deviceId;
    dev["name"] = DEVICE_NAME;
    dev["free_heap"] = ESP.getFreeHeap();
    dev["free_psram"] = ESP.getFreePsram();
    dev["sd_mounted"] = sdcardMounted;
#ifdef HAS_MICROPHONE
    dev["has_mic"] = true;
#else
    dev["has_mic"] = false;
#endif
#ifdef HAS_RGB_LED
    dev["has_rgb_led"] = true;
#else
    dev["has_rgb_led"] = false;
#endif

    JsonArray tools = reqDoc["tools"].to<JsonArray>();
    static const char *const TOOL_LIST[] = {
        "sd_list", "sd_find", "sd_read", "sd_write", "evil_portal_start", "wifi_scan", "wifi_status",
        "device_info", "led_set", "display_message", "ir_tx", "rf_tx", "cli_exec", "bjs_eval"
    };
    for (const char *t : TOOL_LIST) { tools.add(t); }

    String reqBody;
    serializeJson(reqDoc, reqBody);

    HTTPClient http;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    String caCert;

    String url = cfg.gatewayUrl + "/api/v1/chat";
    if (!beginHttpClient(http, plainClient, secureClient, cfg, url, caCert)) {
        outResponse.error = "Failed to connect to gateway";
        return false;
    }

    addAuthHeaders(http, cfg, sessionId);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(reqBody);
    String respBody = http.getString();
    http.end();

    if (code <= 0) {
        outResponse.httpCode = code;
        outResponse.error = "HTTP error: " + http.errorToString(code);
        return false;
    }

    return parseGatewayResponse(respBody, code, outResponse);
}

bool sendVoiceWav(
    const AIConfig &cfg, FS *fs, const String &wavPath, const String &sessionId, AIResponse &outResponse
) {
    outResponse = AIResponse();
    if (!cfg.isConfigured()) {
        outResponse.error = "Configure /BruceAI/config.json first";
        return false;
    }
    if (!fs || !fs->exists(wavPath)) {
        outResponse.error = "WAV file missing: " + wavPath;
        return false;
    }
    if (!ensureWifiReady()) {
        outResponse.error = "Wi-Fi not connected";
        return false;
    }

    File wavFile = fs->open(wavPath, FILE_READ);
    if (!wavFile || wavFile.size() == 0) {
        if (wavFile) wavFile.close();
        outResponse.error = "Empty WAV recording";
        return false;
    }

    HTTPClient http;
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    String caCert;

    String url = cfg.gatewayUrl + "/api/v1/voice";
    if (!beginHttpClient(http, plainClient, secureClient, cfg, url, caCert)) {
        wavFile.close();
        outResponse.error = "Failed to connect to gateway";
        return false;
    }

    addAuthHeaders(http, cfg, sessionId);
    http.addHeader("Content-Type", "audio/wav");

    int code = http.sendRequest("POST", &wavFile, wavFile.size());
    wavFile.close();

    String respBody = http.getString();
    http.end();

    if (code <= 0) {
        outResponse.httpCode = code;
        outResponse.error = "HTTP error: " + http.errorToString(code);
        return false;
    }

    return parseGatewayResponse(respBody, code, outResponse);
}

} // namespace ai

#endif // !defined(LITE_VERSION)
