#pragma once

// Config UI baked into firmware (no uploadfs required).
static const char kWebUiPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Display Control</title>
  <style>
    :root {
      --bg: #0b1220;
      --panel: #152238;
      --text: #e8eef8;
      --muted: #93a4bd;
      --accent: #2ec5ff;
      --ok: #3ddc97;
      --line: #2a3f5f;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Segoe UI", system-ui, sans-serif;
      background: radial-gradient(circle at top left, #173452, var(--bg));
      color: var(--text);
      min-height: 100vh;
      padding: 20px;
    }
    header { margin-bottom: 18px; }
    .status {
      display: inline-block;
      padding: 4px 10px;
      border-radius: 999px;
      background: #1d3b2c;
      color: var(--ok);
      font-size: 12px;
    }
    .grid { display: grid; gap: 14px; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }
    section {
      background: color-mix(in srgb, var(--panel) 90%, black);
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 14px 16px 16px;
    }
    h2 { margin: 0 0 8px; font-size: 16px; }
    details.connections summary { cursor: pointer; color: var(--muted); font-size: 14px; margin-bottom: 8px; }
    label { display: block; font-size: 12px; color: var(--muted); margin: 10px 0 4px; }
    input, button, select {
      width: 100%;
      padding: 10px 12px;
      border-radius: 8px;
      border: 1px solid #314868;
      background: #0f1a2b;
      color: var(--text);
    }
    button {
      margin-top: 12px;
      background: var(--accent);
      color: #041018;
      border: none;
      font-weight: 700;
      cursor: pointer;
    }
    button.secondary { background: #2a3f5f; color: var(--text); }
    button.warn { background: #bb5a2a; color: #fff; }
    .row { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
    .slot {
      border: 1px solid var(--line);
      border-radius: 10px;
      padding: 10px;
      margin-top: 10px;
      background: #0f1a2b55;
    }
    .slot h3 { margin: 0 0 6px; font-size: 13px; color: var(--muted); }
    .hint { font-size: 12px; color: var(--muted); margin-top: 6px; }
    .hidden { display: none !important; }
    .actions { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 14px; }
  </style>
</head>
<body>
  <header><span class="status" id="status">…</span></header>

  <div class="grid">
    <section>
      <h2>Display</h2>
      <label>Brightness (0–100)</label>
      <input id="brightness" type="number" min="0" max="100" />
      <label>Orientation</label>
      <select id="orientation">
        <option value="0">Portrait (480×1920)</option>
        <option value="1">Portrait flipped</option>
        <option value="2">Landscape (1920×480)</option>
        <option value="3">Landscape flipped</option>
      </select>
      <label>Layout</label>
      <select id="layoutMode">
        <option value="grid">Grid (all widgets)</option>
        <option value="carousel">Carousel (one at a time)</option>
      </select>
      <label>Carousel interval (ms)</label>
      <input id="carouselIntervalMs" type="number" min="2000" step="500" />
      <div class="row">
        <button class="secondary" id="applyBrightness" type="button">Apply brightness</button>
        <button class="warn" id="toggleTest" type="button">Test pattern</button>
      </div>
      <p class="hint" id="testHint">Test pattern off</p>
    </section>

    <section>
      <h2>Weather (Open-Meteo)</h2>
      <div class="row">
        <div>
          <label>Latitude</label>
          <input id="weatherLat" type="number" step="0.0001" />
        </div>
        <div>
          <label>Longitude</label>
          <input id="weatherLon" type="number" step="0.0001" />
        </div>
      </div>
      <label>Timezone</label>
      <input id="weatherTz" placeholder="Australia/Melbourne" />
      <label>Units</label>
      <select id="weatherFahrenheit">
        <option value="false">Celsius</option>
        <option value="true">Fahrenheit</option>
      </select>
    </section>

    <section style="grid-column: 1 / -1;">
      <h2>Widgets</h2>
      <p class="hint">Spotify credentials stay in include/secrets.h (never sent to the browser).</p>
      <div id="widgets"></div>
    </section>

    <section style="grid-column: 1 / -1;">
      <details class="connections">
        <summary>Connections (Wi-Fi / MQTT)</summary>
        <div class="grid" style="margin-top:10px;">
          <div>
            <label>Wi-Fi SSID</label>
            <input id="wifiSsid" />
            <label>Wi-Fi password</label>
            <input id="wifiPassword" type="password" placeholder="unchanged if blank" />
          </div>
          <div>
            <label>MQTT host</label>
            <input id="mqttHost" />
            <div class="row">
              <div>
                <label>Port</label>
                <input id="mqttPort" type="number" />
              </div>
              <div>
                <label>Client ID</label>
                <input id="mqttClientId" />
              </div>
            </div>
            <label>Username</label>
            <input id="mqttUser" />
            <label>Password</label>
            <input id="mqttPassword" type="password" placeholder="unchanged if blank" />
          </div>
        </div>
      </details>
    </section>
  </div>

  <div class="actions">
    <button id="save" type="button">Save</button>
    <button class="secondary" id="ota" type="button" onclick="location.href='/update'">OTA update</button>
  </div>

  <script>
    let testPattern = false;
    const TYPE_OPTS = [
      ["off", "Off"],
      ["mqtt_text", "MQTT text"],
      ["mqtt_json", "MQTT JSON"],
      ["world_clock", "World clock"],
      ["weather", "Weather"],
      ["spotify", "Spotify now playing"]
    ];

    function typeSelect(value) {
      return TYPE_OPTS.map(([v, l]) =>
        '<option value="' + v + '"' + (v === (value || "off") ? " selected" : "") + ">" + l + "</option>"
      ).join("");
    }

    function esc(s) {
      return String(s || "").replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
    }

    function slotHtml(i, cfg) {
      const type = (cfg.widgetTypes || [])[i] || "off";
      return (
        '<div class="slot" data-slot="' + i + '">' +
          "<h3>Slot " + (i + 1) + "</h3>" +
          "<label>Type</label><select data-type=\"" + i + "\">" + typeSelect(type) + "</select>" +
          "<label>Label</label><input data-label=\"" + i + "\" value=\"" + esc((cfg.widgetLabels || [])[i]) + "\" />" +
          '<div class="mqtt-fields">' +
            "<label>MQTT topic</label><input data-topic=\"" + i + "\" value=\"" + esc((cfg.widgetTopics || [])[i]) + "\" />" +
          "</div>" +
          '<div class="json-fields">' +
            "<label>JSON path (e.g. main.temp)</label><input data-path=\"" + i + "\" value=\"" + esc((cfg.widgetPaths || [])[i]) + "\" />" +
          "</div>" +
          '<div class="tz-fields">' +
            "<label>UTC offset (e.g. UTC+10)</label><input data-tz=\"" + i + "\" value=\"" + esc((cfg.widgetTz || [])[i]) + "\" />" +
          "</div>" +
          '<p class="hint spotify-hint">Uses SPOTIFY_* from secrets.h</p>' +
          '<p class="hint weather-hint">Uses Weather section above</p>' +
        "</div>"
      );
    }

    function refreshSlotVisibility(slot) {
      const type = slot.querySelector("[data-type]").value;
      slot.querySelector(".mqtt-fields").classList.toggle("hidden", !(type === "mqtt_text" || type === "mqtt_json"));
      slot.querySelector(".json-fields").classList.toggle("hidden", type !== "mqtt_json");
      slot.querySelector(".tz-fields").classList.toggle("hidden", type !== "world_clock");
      slot.querySelector(".spotify-hint").classList.toggle("hidden", type !== "spotify");
      slot.querySelector(".weather-hint").classList.toggle("hidden", type !== "weather");
    }

    function wireSlots() {
      document.querySelectorAll("[data-slot]").forEach((slot) => {
        slot.querySelector("[data-type]").onchange = () => refreshSlotVisibility(slot);
        refreshSlotVisibility(slot);
      });
    }

    function setTestHint() {
      document.getElementById("testHint").textContent = testPattern
        ? "Test pattern ON — colour bars + corner marks + resolution"
        : "Test pattern off";
      document.getElementById("toggleTest").textContent =
        testPattern ? "Disable test pattern" : "Test pattern";
    }

    let haDiscovery = true;

    async function load() {
      const status = await fetch("/api/status").then((r) => r.json());
      document.getElementById("status").textContent = status.status + " @ " + status.ip;
      const cfg = await fetch("/api/config").then((r) => r.json());
      document.getElementById("brightness").value = cfg.brightness ?? 40;
      document.getElementById("orientation").value = String(cfg.orientation ?? 2);
      document.getElementById("layoutMode").value = cfg.layoutMode || "grid";
      document.getElementById("carouselIntervalMs").value = cfg.carouselIntervalMs ?? 8000;
      testPattern = !!cfg.testPattern;
      haDiscovery = cfg.haDiscovery !== false;
      setTestHint();
      document.getElementById("weatherLat").value = cfg.weatherLat ?? "";
      document.getElementById("weatherLon").value = cfg.weatherLon ?? "";
      document.getElementById("weatherTz").value = cfg.weatherTz || "";
      document.getElementById("weatherFahrenheit").value = cfg.weatherFahrenheit ? "true" : "false";
      document.getElementById("wifiSsid").value = cfg.wifiSsid || "";
      document.getElementById("mqttHost").value = cfg.mqttHost || "";
      document.getElementById("mqttPort").value = cfg.mqttPort ?? 1883;
      document.getElementById("mqttClientId").value = cfg.mqttClientId || "";
      document.getElementById("mqttUser").value = cfg.mqttUser || "";
      const box = document.getElementById("widgets");
      box.innerHTML = "";
      for (let i = 0; i < 6; i++) box.innerHTML += slotHtml(i, cfg);
      wireSlots();
    }

    function collectBody() {
      const body = {
        brightness: Number(document.getElementById("brightness").value),
        orientation: Number(document.getElementById("orientation").value),
        layoutMode: document.getElementById("layoutMode").value,
        carouselIntervalMs: Number(document.getElementById("carouselIntervalMs").value),
        testPattern: testPattern,
        weatherLat: Number(document.getElementById("weatherLat").value),
        weatherLon: Number(document.getElementById("weatherLon").value),
        weatherTz: document.getElementById("weatherTz").value,
        weatherFahrenheit: document.getElementById("weatherFahrenheit").value === "true",
        wifiSsid: document.getElementById("wifiSsid").value,
        mqttHost: document.getElementById("mqttHost").value,
        mqttPort: Number(document.getElementById("mqttPort").value),
        mqttClientId: document.getElementById("mqttClientId").value,
        mqttUser: document.getElementById("mqttUser").value,
        haDiscovery: haDiscovery,
        widgetTypes: [...document.querySelectorAll("[data-type]")].map((el) => el.value),
        widgetLabels: [...document.querySelectorAll("[data-label]")].map((el) => el.value),
        widgetTopics: [...document.querySelectorAll("[data-topic]")].map((el) => el.value),
        widgetPaths: [...document.querySelectorAll("[data-path]")].map((el) => el.value),
        widgetTz: [...document.querySelectorAll("[data-tz]")].map((el) => el.value)
      };
      const pass = document.getElementById("wifiPassword").value;
      const mqttPass = document.getElementById("mqttPassword").value;
      if (pass) body.wifiPassword = pass;
      if (mqttPass) body.mqttPassword = mqttPass;
      return body;
    }

    async function saveConfig() {
      const res = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(collectBody())
      });
      document.getElementById("status").textContent = res.ok ? "saved" : "save failed";
      return res.ok;
    }

    document.getElementById("save").onclick = () => saveConfig();
    document.getElementById("applyBrightness").onclick = async () => {
      const value = document.getElementById("brightness").value;
      await fetch("/api/brightness", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: new URLSearchParams({ value })
      });
    };
    document.getElementById("toggleTest").onclick = async () => {
      const res = await fetch("/api/testpattern", { method: "POST" });
      if (!res.ok) {
        document.getElementById("status").textContent = "test pattern failed";
        return;
      }
      const data = await res.json();
      testPattern = !!data.testPattern;
      setTestHint();
      document.getElementById("status").textContent =
        testPattern ? "test pattern on" : "test pattern off";
    };

    async function refreshStatus() {
      try {
        const status = await fetch("/api/status").then((r) => r.json());
        document.getElementById("status").textContent = status.status + " @ " + status.ip;
      } catch (e) {}
    }

    load();
    setInterval(refreshStatus, 5000);
  </script>
</body>
</html>)HTML";
