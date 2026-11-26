# Bruce Firmware — AI Gateway HTTP/JSON & PCT Contract

This document defines the exact HTTP/JSON contract spoken by the **Bruce AI Control** module (`src/modules/ai/`) when communicating with your self-hosted Gateway server (e.g., FastAPI/Node behind a **Cloudflare Tunnel**).

No Gemini / LLM API keys are ever stored in the firmware. The T-Embed authenticates exclusively with a per-device Bearer token configured in `/BruceAI/config.json` on the SD card (or LittleFS fallback).

---

## 1. SD Card Configuration (`/BruceAI/config.json`)

Created automatically on first entry into **AI** from the main carousel:

```json
{
  "gateway_url": "https://your-tunnel.example.com",
  "device_token": "REPLACE_WITH_DEVICE_SECRET",
  "device_id": "tembed-cc1101",
  "timeout_ms": 30000,
  "voice_max_ms": 8000,
  "voice_gain": 2.0,
  "insecure_tls": true,
  "ca_cert_path": "/BruceAI/ca_cert.pem"
}
```

Quick prompts are loaded from `/BruceAI/presets.json`:

```json
[
  { "label": "Portfolio Portal", "prompt": "Inicia um Wi-Fi portal do meu portfolio" },
  { "label": "List SD Portals", "prompt": "Procura ficheiros HTML no cartao SD e lista-os" }
]
```

---

## 2. Authentication & Common Headers

Every request sent by Bruce includes:

| Header | Description |
| --- | --- |
| `Authorization` | `Bearer <device_token>` |
| `X-Device-Id` | Value of `device_id` from `/BruceAI/config.json` |
| `X-Bruce-Device` | Compile-time `DEVICE_NAME` (e.g. `Lilygo T-Embed CC1101`) |
| `X-Session-Id` | Optional conversation session ID returned by the Gateway |

---

## 3. Endpoints

### `GET /api/v1/health`
Used by **AI -> Status -> Test Gateway Health**.

**Response (`200 OK`)**:
```json
{
  "status": "ok",
  "model": "gemini-3.8-flash"
}
```

### `POST /api/v1/chat`
Used by **Type** and **Presets**.

**Request (`Content-Type: application/json`)**:
```json
{
  "prompt": "Inicia um Wi-Fi portal do meu portfolio",
  "session_id": "sess_123",
  "device": {
    "id": "tembed-cc1101",
    "name": "Lilygo T-Embed CC1101",
    "free_heap": 182400,
    "free_psram": 7864320,
    "sd_mounted": true,
    "has_mic": true,
    "has_rgb_led": true
  },
  "tools": [
    "sd_list", "sd_find", "sd_read", "sd_write",
    "evil_portal_start", "wifi_scan", "wifi_status",
    "device_info", "led_set", "display_message",
    "ir_tx", "rf_tx", "cli_exec", "bjs_eval"
  ]
}
```

### `POST /api/v1/voice`
Used by **Voice (Push-to-Talk)**.

- **Request**: Raw 16-bit mono PCM WAV streamed directly from SD/LittleFS with `Content-Type: audio/wav` (48 kHz mono recorded by Bruce `mic_record_wav_to_path`).
- The Gateway can pass the audio directly to Gemini multimodal audio input (or run Whisper/STT first) and return the exact same JSON schema as `/api/v1/chat`, including `"transcript"`.

---

## 4. PCT (Programmatic Tool Calling) Response Schema

Both `/api/v1/chat` and `/api/v1/voice` return:

```json
{
  "ok": true,
  "session_id": "sess_123",
  "transcript": "Inicia um Wi-Fi portal do meu portfolio",
  "reply": "A procurar o HTML do teu portfolio no SD e a iniciar o Evil Portal...",
  "steps": [
    {
      "id": "s1",
      "tool": "sd_find",
      "args": {
        "query": "portfolio",
        "ext": ".html",
        "max_results": 5
      }
    },
    {
      "id": "s2",
      "tool": "evil_portal_start",
      "args": {
        "ssid": "Meu Portfolio",
        "html_path": "${s1.first_match}",
        "channel": 6,
        "deauth": false,
        "verify_pwd": false
      },
      "on_missing_var": "abort"
    }
  ]
}
```

### Variable Interpolation (`${step_id.key}`)
- Every executed step exports its outputs into the batch context under `<step_id>.<key>`, plus `<step_id>.ok` (`"true"` / `"false"`) and `<step_id>.summary`.
- Any string argument in subsequent steps can reference `${step_id.key}`.
- If a referenced variable is missing or empty and `"on_missing_var"` is `"abort"` (default), the batch aborts safely with an error on screen. Set `"on_missing_var": "continue"` to allow empty substitution.

---

## 5. Native Bruce Tool Reference (v1 Core)

| Tool | Arguments | Exported Outputs |
| --- | --- | --- |
| `sd_list` | `path` (`"/"`), `max_entries` (`40`) | `count`, `entries` |
| `sd_find` | `query`, `path` (`"/"`), `ext` (`""`), `max_results` (`10`) | `count`, `first_match`, `matches` |
| `sd_read` | `path`, `max_bytes` (`2048`) | `content`, `bytes` |
| `sd_write` | `path`, `content`, `append` (`false`) | `bytes`, `path` |
| `evil_portal_start` | `ssid` (`"Free Wifi"`), `html_path` (`""`), `channel` (`6`), `deauth` (`false`), `verify_pwd` (`false`) | `ap_name`, `html_path` |
| `wifi_scan` | `max_results` (`12`) | `count`, `top_ssid`, `networks` |
| `wifi_status` | *(none)* | `connected`, `ssid`, `ip`, `rssi`, `mac` |
| `device_info` | *(none)* | `device`, `battery`, `free_heap_kb`, `free_psram_kb`, `sd_mounted`, `wifi_connected` |
| `led_set` | `r`, `g`, `b`, `effect` (`-1`), `brightness` (`-1`) | `summary` |
| `display_message` | `title`, `text`, `duration_ms` (`2500`, `0` = wait key) | `summary` |
| `ir_tx` | `path` **or** (`protocol`, `data`, `bits`) | `summary` |
| `rf_tx` | `path` **or** (`data`, `freq`, `te`, `count`) | `summary` |
| `cli_exec` | `command` | `summary` |
| `bjs_eval` | `code` (MicroQuickJS script) | `summary` |
