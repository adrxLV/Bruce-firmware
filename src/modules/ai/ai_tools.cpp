#if !defined(LITE_VERSION)

#include "ai_tools.h"
#include "core/display.h"
#include "core/led_control.h"
#include "core/sd_functions.h"
#include "core/wifi/wifi_common.h"
#include "modules/ir/custom_ir.h"
#include "modules/wifi/evil_portal.h"
#if !defined(DISABLE_INTERPRETER)
#include "modules/bjs_interpreter/interpreter.h"
#endif
#include <ArduinoJson.h>
#include <WiFi.h>
#include <globals.h>
#include <interface.h>

namespace ai {

static String interpolatePlaceholders(
    const String &input, const std::map<String, String> &vars, bool &outMissing, String &outMissingName
) {
    String result;
    result.reserve(input.length());
    int pos = 0;
    while (pos < (int)input.length()) {
        int start = input.indexOf("${", pos);
        if (start < 0) {
            result += input.substring(pos);
            break;
        }
        result += input.substring(pos, start);
        int end = input.indexOf('}', start + 2);
        if (end < 0) {
            result += input.substring(start);
            break;
        }
        String key = input.substring(start + 2, end);
        auto it = vars.find(key);
        if (it == vars.end() || it->second.isEmpty()) {
            outMissing = true;
            outMissingName = key;
        } else {
            result += it->second;
        }
        pos = end + 1;
    }
    return result;
}

static void interpolateJsonVariant(
    JsonVariant v, const std::map<String, String> &vars, bool &outMissing, String &outMissingName
) {
    if (v.is<const char *>()) {
        String s = v.as<const char *>();
        if (s.indexOf("${") >= 0) {
            String replaced = interpolatePlaceholders(s, vars, outMissing, outMissingName);
            v.set(replaced);
        }
    } else if (v.is<JsonObject>()) {
        for (JsonPair kv : v.as<JsonObject>()) {
            interpolateJsonVariant(kv.value(), vars, outMissing, outMissingName);
        }
    } else if (v.is<JsonArray>()) {
        for (JsonVariant elem : v.as<JsonArray>()) {
            interpolateJsonVariant(elem, vars, outMissing, outMissingName);
        }
    }
}

static void sdFindRecursive(
    FS *fs, const String &dirPath, const String &queryLower, const String &extLower, int maxResults,
    int depth, std::vector<String> &matches
) {
    if (!fs || depth > 6 || (int)matches.size() >= maxResults) return;
    File dir = fs->open(dirPath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }

    File entry = dir.openNextFile();
    while (entry && (int)matches.size() < maxResults) {
        if (check(EscPress)) {
            entry.close();
            break;
        }

        String name = String(entry.name());
        String fullPath;
        if (name.startsWith("/")) {
            fullPath = name;
        } else if (dirPath == "/") {
            fullPath = "/" + name;
        } else {
            fullPath = dirPath + "/" + name;
        }

        if (entry.isDirectory()) {
            entry.close();
            sdFindRecursive(fs, fullPath, queryLower, extLower, maxResults, depth + 1, matches);
        } else {
            entry.close();
            String lowerPath = fullPath;
            lowerPath.toLowerCase();

            bool okQuery = queryLower.isEmpty() || lowerPath.indexOf(queryLower) >= 0;
            bool okExt = extLower.isEmpty() || lowerPath.endsWith(extLower);
            if (okQuery && okExt) { matches.push_back(fullPath); }
        }
        entry = dir.openNextFile();
    }
    dir.close();
}

static AIPctStepResult execSdList(JsonObject args) {
    AIPctStepResult res;
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        res.summary = "No storage available";
        return res;
    }

    String path = args["path"] | String("/");
    int maxEntries = args["max_entries"] | 40;
    if (maxEntries < 1) maxEntries = 1;
    if (maxEntries > 100) maxEntries = 100;

    File dir = fs->open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        res.summary = "Directory not found: " + path;
        return res;
    }

    String listStr;
    int count = 0;
    File entry = dir.openNextFile();
    while (entry && count < maxEntries) {
        if (count > 0) listStr += "\n";
        listStr += String(entry.name());
        if (entry.isDirectory()) listStr += "/";
        count++;
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    res.ok = true;
    res.outputs["count"] = String(count);
    res.outputs["entries"] = listStr;
    res.summary = "Listed " + String(count) + " item(s) in " + path;
    return res;
}

static AIPctStepResult execSdFind(JsonObject args) {
    AIPctStepResult res;
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        res.summary = "No storage available";
        return res;
    }

    String query = args["query"] | String("");
    String path = args["path"] | String("/");
    String ext = args["ext"] | String("");
    int maxResults = args["max_results"] | 10;
    if (maxResults < 1) maxResults = 1;
    if (maxResults > 50) maxResults = 50;

    query.toLowerCase();
    ext.toLowerCase();
    if (!ext.isEmpty() && !ext.startsWith(".")) ext = "." + ext;

    std::vector<String> matches;
    sdFindRecursive(fs, path, query, ext, maxResults, 0, matches);

    String joined;
    for (size_t i = 0; i < matches.size(); i++) {
        if (i > 0) joined += "\n";
        joined += matches[i];
    }

    res.ok = true;
    res.outputs["count"] = String(matches.size());
    res.outputs["matches"] = joined;
    res.outputs["first_match"] = matches.empty() ? String("") : matches[0];
    if (matches.empty()) {
        res.summary = "No files matching '" + query + "'";
    } else {
        res.summary = "Found " + String(matches.size()) + " match(es): " + matches[0];
    }
    return res;
}

static AIPctStepResult execSdRead(JsonObject args) {
    AIPctStepResult res;
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        res.summary = "No storage available";
        return res;
    }

    String path = args["path"] | String("");
    int maxBytes = args["max_bytes"] | 2048;
    if (maxBytes < 64) maxBytes = 64;
    if (maxBytes > 8192) maxBytes = 8192;

    if (path.isEmpty() || !fs->exists(path)) {
        res.summary = "File not found: " + path;
        return res;
    }

    File f = fs->open(path, FILE_READ);
    if (!f) {
        res.summary = "Failed to open: " + path;
        return res;
    }

    String content;
    content.reserve(min((size_t)maxBytes, f.size()));
    while (f.available() && (int)content.length() < maxBytes) { content += (char)f.read(); }
    f.close();

    res.ok = true;
    res.outputs["content"] = content;
    res.outputs["bytes"] = String(content.length());
    res.summary = "Read " + String(content.length()) + "B from " + path;
    return res;
}

static AIPctStepResult execSdWrite(JsonObject args) {
    AIPctStepResult res;
    FS *fs = nullptr;
    if (!getFsStorage(fs) || !fs) {
        res.summary = "No storage available";
        return res;
    }

    String path = args["path"] | String("");
    String content = args["content"] | String("");
    bool append = args["append"] | false;

    if (path.isEmpty()) {
        res.summary = "Missing path";
        return res;
    }

    int lastSlash = path.lastIndexOf('/');
    if (lastSlash > 0) {
        String parentDir = path.substring(0, lastSlash);
        if (!fs->exists(parentDir)) fs->mkdir(parentDir);
    }

    File f = fs->open(path, append ? FILE_APPEND : FILE_WRITE);
    if (!f) {
        res.summary = "Cannot write: " + path;
        return res;
    }
    size_t written = f.print(content);
    f.close();

    res.ok = true;
    res.outputs["bytes"] = String(written);
    res.outputs["path"] = path;
    res.summary = "Wrote " + String(written) + "B to " + path;
    return res;
}

static AIPctStepResult execEvilPortalStart(JsonObject args) {
    AIPctStepResult res;
    String ssid = args["ssid"] | String("Free Wifi");
    String htmlPath = args["html_path"] | String("");
    uint8_t channel = args["channel"] | 6;
    bool deauth = args["deauth"] | false;
    bool verifyPwd = args["verify_pwd"] | false;

    if (!htmlPath.isEmpty()) {
        FS *fs = nullptr;
        if (!getFsStorage(fs) || !fs || !fs->exists(htmlPath)) {
            res.summary = "HTML file not found: " + htmlPath;
            return res;
        }
    }

    // Blocking call until user presses ESC inside EvilPortal; reset returnToMenu afterwards
    {
        EvilPortal portal(ssid, channel, deauth, verifyPwd, true, false, htmlPath);
        res.outputs["ap_name"] = portal.getApName();
    }
    returnToMenu = false;

    res.ok = true;
    res.outputs["html_path"] = htmlPath;
    res.summary = "EvilPortal finished (" + ssid + ")";
    return res;
}

static AIPctStepResult execWifiScan(JsonObject args) {
    AIPctStepResult res;
    int maxResults = args["max_results"] | 12;
    if (maxResults < 1) maxResults = 1;
    if (maxResults > 30) maxResults = 30;

    int n = WiFi.scanNetworks(false, true);
    if (n < 0) {
        res.summary = "Wi-Fi scan failed";
        return res;
    }

    String summary;
    String topSsid;
    int limit = min(n, maxResults);
    for (int i = 0; i < limit; i++) {
        String s = WiFi.SSID(i);
        if (i == 0) topSsid = s;
        if (i > 0) summary += "\n";
        summary += s + " (" + String(WiFi.RSSI(i)) + "dBm, ch" + String(WiFi.channel(i)) + ")";
    }
    WiFi.scanDelete();

    res.ok = true;
    res.outputs["count"] = String(n);
    res.outputs["top_ssid"] = topSsid;
    res.outputs["networks"] = summary;
    res.summary = "Found " + String(n) + " Wi-Fi net(s)";
    return res;
}

static AIPctStepResult execWifiStatus() {
    AIPctStepResult res;
    bool connected = (WiFi.status() == WL_CONNECTED);
    String ip = connected ? WiFi.localIP().toString() : "0.0.0.0";
    String ssid = connected ? WiFi.SSID() : "";

    res.ok = true;
    res.outputs["connected"] = connected ? "true" : "false";
    res.outputs["ssid"] = ssid;
    res.outputs["ip"] = ip;
    res.outputs["rssi"] = String(WiFi.RSSI());
    res.outputs["mac"] = WiFi.macAddress();
    res.summary = connected ? ("Wi-Fi: " + ssid + " (" + ip + ")") : "Wi-Fi: Disconnected";
    return res;
}

static AIPctStepResult execDeviceInfo() {
    AIPctStepResult res;
    int bat = getBattery();
    uint32_t heapKb = ESP.getFreeHeap() / 1024;
    uint32_t psramKb = ESP.getFreePsram() / 1024;

    res.ok = true;
    res.outputs["device"] = DEVICE_NAME;
    res.outputs["battery"] = String(bat);
    res.outputs["free_heap_kb"] = String(heapKb);
    res.outputs["free_psram_kb"] = String(psramKb);
    res.outputs["sd_mounted"] = sdcardMounted ? "true" : "false";
    res.outputs["wifi_connected"] = (WiFi.status() == WL_CONNECTED) ? "true" : "false";
    res.summary = String(DEVICE_NAME) + " | Bat:" + String(bat) + "% | Heap:" + String(heapKb) +
                  "KB | PSRAM:" + String(psramKb) + "KB | SD:" + (sdcardMounted ? "OK" : "NO");
    return res;
}

static AIPctStepResult execLedSet(JsonObject args) {
    AIPctStepResult res;
#ifdef HAS_RGB_LED
    int r = args["r"] | 0;
    int g = args["g"] | 255;
    int b = args["b"] | 255;
    int effect = args["effect"] | -1;
    int brightness = args["brightness"] | -1;

    setLedColor(CRGB(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255)));
    if (effect >= 0) setLedEffect(effect);
    if (brightness >= 0) setLedBrightness(constrain(brightness, 0, 255));

    res.ok = true;
    res.summary = "LED RGB(" + String(r) + "," + String(g) + "," + String(b) + ")";
#else
    res.summary = "No RGB LED on this board";
#endif
    return res;
}

static AIPctStepResult execDisplayMessage(JsonObject args) {
    AIPctStepResult res;
    String title = args["title"] | String("AI MESSAGE");
    String text = args["text"] | String("");
    uint32_t durationMs = args["duration_ms"] | 2500U;

    drawMainBorderWithTitle(title);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.setCursor(10, 36);
    padprintln(text);

    if (durationMs == 0) {
        while (!check(SelPress) && !check(EscPress)) { delay(20); }
    } else {
        unsigned long start = millis();
        while (millis() - start < durationMs) {
            if (check(SelPress) || check(EscPress)) break;
            delay(20);
        }
    }

    res.ok = true;
    res.summary = "Displayed message";
    return res;
}

static AIPctStepResult execIrTx(JsonObject args) {
    AIPctStepResult res;
    String path = args["path"] | String("");
    String protocol = args["protocol"] | String("");
    String data = args["data"] | String("");
    uint8_t bits = args["bits"] | 32;

    if (!path.isEmpty()) {
        FS *fs = nullptr;
        if (!getFsStorage(fs) || !fs || !fs->exists(path)) {
            res.summary = "IR file not found: " + path;
            return res;
        }
        res.ok = txIrFile(fs, path, true);
        res.summary = res.ok ? ("IR TX file: " + path) : ("IR TX failed: " + path);
        return res;
    }

    if (!protocol.isEmpty() && !data.isEmpty()) {
        res.ok = sendDecodedCommand(protocol, data, bits, true);
        res.summary = res.ok ? ("IR TX " + protocol + " " + data) : "IR TX failed";
        return res;
    }

    res.summary = "ir_tx requires path or (protocol + data)";
    return res;
}

static AIPctStepResult execRfTx(JsonObject args) {
    AIPctStepResult res;
    String path = args["path"] | String("");
    String data = args["data"] | String("");
    uint32_t freq = args["freq"] | 433920000U;
    uint32_t te = args["te"] | 174U;
    uint32_t count = args["count"] | 10U;

    if (!path.isEmpty()) {
        res.ok = serialCli.parse("subghz tx_from_file " + path + " 1");
        res.summary = res.ok ? ("RF TX file: " + path) : ("RF TX failed: " + path);
        return res;
    }

    if (!data.isEmpty()) {
        res.ok = serialCli.parse(
            "subghz tx " + data + " " + String(freq) + " " + String(te) + " " + String(count)
        );
        res.summary = res.ok ? ("RF TX " + String(freq) + "Hz") : "RF TX failed";
        return res;
    }

    res.summary = "rf_tx requires path or data";
    return res;
}

static AIPctStepResult execCliExec(JsonObject args) {
    AIPctStepResult res;
    String command = args["command"] | String("");
    command.trim();
    if (command.isEmpty()) {
        res.summary = "cli_exec requires command";
        return res;
    }

    res.ok = serialCli.parse(command);
    res.summary = (res.ok ? "CLI OK: " : "CLI ERR: ") + command;
    return res;
}

static AIPctStepResult execBjsEval(JsonObject args) {
    AIPctStepResult res;
#if !defined(DISABLE_INTERPRETER)
    String code = args["code"] | String("");
    if (code.isEmpty()) {
        res.summary = "bjs_eval requires code";
        return res;
    }

    std::vector<char> buf(code.length() + 1);
    memcpy(buf.data(), code.c_str(), code.length() + 1);
    res.ok = run_bjs_script_headless(buf.data());
    returnToMenu = false;
    res.summary = res.ok ? "BruceJS executed OK" : "BruceJS execution failed";
#else
    res.summary = "BruceJS interpreter disabled";
#endif
    return res;
}

AIPctBatchResult executePctBatch(
    const std::vector<AIPctStep> &steps, std::function<void(const String &)> onLog
) {
    AIPctBatchResult batch;
    std::map<String, String> vars;

    for (size_t i = 0; i < steps.size(); i++) {
        if (check(EscPress)) {
            batch.aborted = true;
            batch.allOk = false;
            if (onLog) onLog("! Aborted by ESC");
            break;
        }

        const AIPctStep &step = steps[i];
        if (onLog) onLog("> [" + step.id + "] " + step.tool + "...");

        JsonDocument argsDoc;
        DeserializationError err = deserializeJson(argsDoc, step.argsJson);
        if (err || !argsDoc.is<JsonObject>()) { argsDoc.to<JsonObject>(); }

        bool missingVar = false;
        String missingName;
        interpolateJsonVariant(argsDoc.as<JsonVariant>(), vars, missingVar, missingName);

        AIPctStepResult r;
        r.id = step.id;
        r.tool = step.tool;

        if (missingVar && step.onMissingVar != "continue") {
            r.ok = false;
            r.summary = "Missing var: " + missingName;
            batch.allOk = false;
            batch.stepResults.push_back(r);
            if (onLog) onLog("  ERR: " + r.summary);
            break;
        }

        JsonObject args = argsDoc.as<JsonObject>();
        if (step.tool == "sd_list") r = execSdList(args);
        else if (step.tool == "sd_find") r = execSdFind(args);
        else if (step.tool == "sd_read") r = execSdRead(args);
        else if (step.tool == "sd_write") r = execSdWrite(args);
        else if (step.tool == "evil_portal_start") r = execEvilPortalStart(args);
        else if (step.tool == "wifi_scan") r = execWifiScan(args);
        else if (step.tool == "wifi_status") r = execWifiStatus();
        else if (step.tool == "device_info") r = execDeviceInfo();
        else if (step.tool == "led_set") r = execLedSet(args);
        else if (step.tool == "display_message") r = execDisplayMessage(args);
        else if (step.tool == "ir_tx") r = execIrTx(args);
        else if (step.tool == "rf_tx") r = execRfTx(args);
        else if (step.tool == "cli_exec") r = execCliExec(args);
        else if (step.tool == "bjs_eval") r = execBjsEval(args);
        else {
            r.ok = false;
            r.summary = "Unknown tool: " + step.tool;
        }

        r.id = step.id;
        r.tool = step.tool;

        for (const auto &kv : r.outputs) { vars[step.id + "." + kv.first] = kv.second; }
        vars[step.id + ".ok"] = r.ok ? "true" : "false";
        vars[step.id + ".summary"] = r.summary;

        if (onLog) onLog((r.ok ? "  OK: " : "  ERR: ") + r.summary);
        if (!r.ok) batch.allOk = false;
        batch.stepResults.push_back(r);
    }

    return batch;
}

} // namespace ai

#endif // !defined(LITE_VERSION)
