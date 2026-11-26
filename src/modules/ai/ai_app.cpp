#if !defined(LITE_VERSION)

#include "ai_app.h"
#include "ai_client.h"
#include "ai_config.h"
#include "ai_tools.h"
#include "core/display.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"
#include "core/sd_functions.h"
#ifdef HAS_MICROPHONE
#include "modules/others/mic.h"
#endif
#include <WiFi.h>
#include <globals.h>

namespace ai {

namespace {

enum class AIAction : uint8_t {
#ifdef HAS_MICROPHONE
    VOICE = 0,
    TYPE,
#else
    TYPE = 0,
#endif
    PRESETS,
    STATUS,
    COUNT
};

static const char *actionLabel(AIAction act) {
    switch (act) {
#ifdef HAS_MICROPHONE
        case AIAction::VOICE: return "Voice";
#endif
        case AIAction::TYPE: return "Type";
        case AIAction::PRESETS: return "Presets";
        case AIAction::STATUS: return "Status";
        default: return "";
    }
}

static void drawActionBar(int selectedIdx, int barY, int barH) {
    const int count = static_cast<int>(AIAction::COUNT);
    const int marginX = BORDER_PAD_X + 2;
    const int totalW = tftWidth - 2 * marginX;
    const int gap = 4;
    const int btnW = (totalW - gap * (count - 1)) / count;

    tft.fillRect(marginX, barY, totalW, barH, bruceConfig.bgColor);
    tft.setTextSize(FP);

    for (int i = 0; i < count; i++) {
        int bx = marginX + i * (btnW + gap);
        bool sel = (i == selectedIdx);
        uint16_t fill = sel ? bruceConfig.priColor : bruceConfig.bgColor;
        uint16_t text = sel ? bruceConfig.bgColor : bruceConfig.priColor;

        tft.fillRoundRect(bx, barY, btnW, barH, 4, fill);
        tft.drawRoundRect(bx, barY, btnW, barH, 4, bruceConfig.priColor);
        tft.setTextColor(text, fill);
        tft.drawCentreString(actionLabel(static_cast<AIAction>(i)), bx + btnW / 2, barY + (barH - LH * FP) / 2, 1);
    }
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
}

static void appendLog(ScrollableTextArea &area, const String &line) {
    area.addLine(line);
    size_t maxL = area.getMaxLines();
    size_t vis = area.getMaxVisibleLines();
    if (maxL > vis) { area.scrollToLine(maxL - vis); }
    area.draw(true);
}

static void redrawChrome(
    const AIConfig &cfg, ScrollableTextArea &area, int selectedAction, int barY, int barH
) {
    drawMainBorderWithTitle("AI CONTROL");

    // Compact status indicator on top-right inside header area
    tft.setTextSize(FP);
    String badge = String(WiFi.status() == WL_CONNECTED ? "WF:OK " : "WF:-- ") +
                   String(cfg.isConfigured() ? "GW:OK" : "GW:NO");
    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    tft.drawRightString(badge, tftWidth - BORDER_PAD_X - 4, STATUS_BAR_HEIGHT - 1, 1);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    area.draw(true);
    drawActionBar(selectedAction, barY, barH);
}

static void handleGatewayResponse(
    const AIResponse &resp, bool ok, String &sessionId, ScrollableTextArea &area, const AIConfig &cfg,
    int selectedAction, int barY, int barH
) {
    if (!resp.sessionId.isEmpty()) { sessionId = resp.sessionId; }

    if (!ok) {
        appendLog(area, "ERR: " + resp.error);
        return;
    }

    if (!resp.transcript.isEmpty()) { appendLog(area, "You: " + resp.transcript); }
    if (!resp.reply.isEmpty()) { appendLog(area, "AI: " + resp.reply); }

    if (!resp.steps.empty()) {
        appendLog(area, "PCT: " + String(resp.steps.size()) + " step(s)");
        AIPctBatchResult batch = executePctBatch(resp.steps, [&](const String &msg) {
            appendLog(area, msg);
        });
        // Redraw full AI Control screen in case a tool (e.g. EvilPortal / display_message) took over TFT
        redrawChrome(cfg, area, selectedAction, barY, barH);
        if (batch.aborted) {
            appendLog(area, "PCT aborted.");
        } else if (batch.allOk) {
            appendLog(area, "PCT done (OK).");
        } else {
            appendLog(area, "PCT finished with errors.");
        }
    }
}

#ifdef HAS_MICROPHONE
static void runVoiceAction(
    const AIConfig &cfg, String &sessionId, ScrollableTextArea &area, int selectedAction, int barY, int barH
) {
    if (!cfg.isConfigured()) {
        appendLog(area, "Edit /BruceAI/config.json first.");
        return;
    }

    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        appendLog(area, "ERR: No SD/LittleFS for WAV.");
        return;
    }

    appendLog(area, "REC... [SEL=send ESC=cancel]");
#ifdef HAS_RGB_LED
    setLedColor(CRGB(255, 40, 40));
#endif

    bool cancelled = false;
    uint32_t recordedBytes = 0;
    bool recOk = mic_record_wav_to_path(
        fs,
        AI_VOICE_TMP_PATH,
        cfg.voiceMaxMs,
        &recordedBytes,
        cfg.voiceGain,
        [&]() -> bool {
            if (check(EscPress)) {
                cancelled = true;
                return false;
            }
            if (check(SelPress)) { return false; }
            return true;
        }
    );

#ifdef HAS_RGB_LED
    setLedColor(CRGB(0, 180, 255));
#endif

    if (cancelled) {
        if (fs->exists(AI_VOICE_TMP_PATH)) fs->remove(AI_VOICE_TMP_PATH);
        appendLog(area, "Voice cancelled.");
        return;
    }

    if (!recOk || recordedBytes == 0) {
        appendLog(area, "ERR: Mic recording failed.");
        return;
    }

    appendLog(area, "Uploading WAV (" + String(recordedBytes / 1024) + "KB)...");
    AIResponse resp;
    bool ok = sendVoiceWav(cfg, fs, AI_VOICE_TMP_PATH, sessionId, resp);
    if (fs->exists(AI_VOICE_TMP_PATH)) fs->remove(AI_VOICE_TMP_PATH);

    handleGatewayResponse(resp, ok, sessionId, area, cfg, selectedAction, barY, barH);
}
#endif

static void runTypeAction(
    const AIConfig &cfg, String &sessionId, ScrollableTextArea &area, int selectedAction, int barY, int barH
) {
    String prompt = keyboard("", 180, "AI Prompt:");
    prompt.trim();
    redrawChrome(cfg, area, selectedAction, barY, barH);

    if (prompt.isEmpty()) return;
    if (!cfg.isConfigured()) {
        appendLog(area, "Edit /BruceAI/config.json first.");
        return;
    }

    appendLog(area, "You: " + prompt);
    appendLog(area, "Thinking...");
    AIResponse resp;
    bool ok = sendTextPrompt(cfg, prompt, sessionId, resp);
    handleGatewayResponse(resp, ok, sessionId, area, cfg, selectedAction, barY, barH);
}

static void runPresetsAction(
    const AIConfig &cfg, const std::vector<AIPreset> &presets, String &sessionId, ScrollableTextArea &area,
    int selectedAction, int barY, int barH
) {
    String chosenPrompt;
    options.clear();
    for (const auto &p : presets) {
        options.push_back({p.label.c_str(), [&chosenPrompt, p]() { chosenPrompt = p.prompt; }});
    }
    options.push_back({"Back", []() {}});

    loopOptions(options, MENU_TYPE_REGULAR, "AI Presets");
    returnToMenu = false;
    redrawChrome(cfg, area, selectedAction, barY, barH);

    if (chosenPrompt.isEmpty()) return;
    if (!cfg.isConfigured()) {
        appendLog(area, "Edit /BruceAI/config.json first.");
        return;
    }

    appendLog(area, "You: " + chosenPrompt);
    appendLog(area, "Thinking...");
    AIResponse resp;
    bool ok = sendTextPrompt(cfg, chosenPrompt, sessionId, resp);
    handleGatewayResponse(resp, ok, sessionId, area, cfg, selectedAction, barY, barH);
}

static void runStatusAction(
    AIConfig &cfg, std::vector<AIPreset> &presets, String &sessionId, ScrollableTextArea &area,
    int selectedAction, int barY, int barH
) {
    options = {
        {"Test Gateway Health",
         [&]() {
             redrawChrome(cfg, area, selectedAction, barY, barH);
             appendLog(area, "Checking gateway...");
             String st;
             bool ok = checkGatewayHealth(cfg, st);
             appendLog(area, String(ok ? "Health OK: " : "Health ERR: ") + st);
         }},
        {"Show Config",
         [&]() {
             redrawChrome(cfg, area, selectedAction, barY, barH);
             appendLog(area, "URL: " + cfg.gatewayUrl);
             appendLog(area, "Tok: " + cfg.maskedToken());
             appendLog(area, "ID:  " + cfg.deviceId);
             appendLog(area, "TLS: " + String(cfg.insecureTls ? "insecure" : "CA cert"));
         }},
        {"Reload SD Config",
         [&]() {
             loadConfig(cfg);
             loadPresets(presets);
             redrawChrome(cfg, area, selectedAction, barY, barH);
             appendLog(area, cfg.isConfigured() ? "Config reloaded (OK)." : "Config template not set.");
         }},
        {"Reset Session",
         [&]() {
             sessionId = "";
             area.clear();
             redrawChrome(cfg, area, selectedAction, barY, barH);
             appendLog(area, "Session cleared.");
         }},
        {"Back", []() {}}
    };

    loopOptions(options, MENU_TYPE_REGULAR, "AI Status");
    returnToMenu = false;
    redrawChrome(cfg, area, selectedAction, barY, barH);
}

} // namespace

void runAIControlApp() {
    AIConfig cfg;
    std::vector<AIPreset> presets;
    bool loaded = loadConfig(cfg);
    loadPresets(presets);

    const int barH = 22;
    const int barY = tftHeight - BORDER_PAD_Y - barH - 2;
    const int areaX = BORDER_PAD_X + 2;
    const int areaY = STATUS_BAR_HEIGHT + 12;
    const int areaW = tftWidth - 2 * areaX;
    const int areaH = barY - areaY - 4;

    ScrollableTextArea area(FP, areaX, areaY, areaW, areaH, false, true);
    int selectedAction = 0;
    const int actionCount = static_cast<int>(AIAction::COUNT);
    String sessionId = "";

    redrawChrome(cfg, area, selectedAction, barY, barH);

    if (!cfg.isConfigured()) {
        appendLog(area, loaded ? "AI config incomplete." : "Created /BruceAI/config.json");
        appendLog(area, "Set gateway_url & device_token");
        appendLog(area, "on SD card, then Status->Reload.");
    } else {
        appendLog(area, "Ready (" + cfg.deviceId + ").");
        appendLog(area, "Turn knob: select | SEL: run");
    }

    while (!returnToMenu) {
        if (check(EscPress)) {
            returnToMenu = true;
            break;
        }

        if (check(NextPress)) {
            selectedAction = (selectedAction + 1) % actionCount;
            drawActionBar(selectedAction, barY, barH);
        }

        if (check(PrevPress)) {
            selectedAction = (selectedAction - 1 + actionCount) % actionCount;
            drawActionBar(selectedAction, barY, barH);
        }

        if (check(SelPress)) {
            AIAction act = static_cast<AIAction>(selectedAction);
            switch (act) {
#ifdef HAS_MICROPHONE
                case AIAction::VOICE:
                    runVoiceAction(cfg, sessionId, area, selectedAction, barY, barH);
                    break;
#endif
                case AIAction::TYPE:
                    runTypeAction(cfg, sessionId, area, selectedAction, barY, barH);
                    break;
                case AIAction::PRESETS:
                    runPresetsAction(cfg, presets, sessionId, area, selectedAction, barY, barH);
                    break;
                case AIAction::STATUS:
                    runStatusAction(cfg, presets, sessionId, area, selectedAction, barY, barH);
                    break;
                default: break;
            }
        }

        delay(15);
    }

    returnToMenu = false;
}

} // namespace ai

#endif // !defined(LITE_VERSION)
