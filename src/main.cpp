#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <BH1750.h>
#include <Adafruit_BMP085.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ---------- WIFI CREDENTIALS ----------
const char* ssid     = "moto 50";
const char* password = "12340987";

// ---------- PIN DEFINITIONS ----------
#define DHT_PIN      2
#define PH_PIN       34
#define ENC_CLK      33
#define ENC_DT       25
#define ENC_SW       26
#define RELAY_MOTOR  23
#define RELAY_LIGHT  18
#define RELAY_FAN    19

#define RELAY_ON      LOW
#define RELAY_OFF     HIGH
#define FAN_RELAY_ON  LOW
#define FAN_RELAY_OFF HIGH

// ---------- OBJECTS ----------
DHT dht(DHT_PIN, DHT11);
BH1750 lightMeter;
Adafruit_BMP085 bmp;
LiquidCrystal_I2C lcd(0x27, 20, 4);
AsyncWebServer server(80);
AsyncEventSource events("/events");

// ---------- MENU STATE ----------
enum MenuState {
    WELCOME, MAIN_MENU, DHT_DISPLAY, DS18B20_DISPLAY,
    BH1750_DISPLAY, PH_DISPLAY, PRESSURE_DISPLAY,
    RELAY_MENU, MOTOR_SETTINGS, LIGHT_CONTROL, FAN_CONTROL
};

MenuState currentState  = WELCOME;
MenuState previousState = WELCOME;

int menuIndex      = 0;
int relayMenuIndex = 0;

// ---------- RELAY / MOTOR STATE ----------
unsigned long motorOnTime     = 15 * 60000UL;
unsigned long motorOffTime    = 45 * 60000UL;
unsigned long motorLastToggle = 0;
bool motorState    = false;
bool motorAutoMode = true;
bool lightState    = false;
bool fanAutoMode   = true;
bool fanState      = false;

// ---------- DISPLAY TIMING ----------
unsigned long lastDisplayUpdate = 0;
const unsigned long displayUpdateInterval = 1000;
bool forceDisplayUpdate = true;

// ---------- SENSOR VALUES ----------
float bmpTemp      = 0.0;   // BMP180 temperature (used everywhere)
float dhtHumidity  = 50.0;  // Simulated humidity
float ds18b20Temp  = 20.0;
float lux          = 0;
float phValue      = 0;
float pressure_hPa = 0;

// ---------- ENCODER ----------
volatile int encoderPos = 0;
int lastEncoderPos = 0;
int lastCLK        = HIGH;
unsigned long swPressTime = 0;
bool swWasPressed  = false;
const unsigned long LONG_PRESS_MS = 800;

// ---------- SSE TIMING ----------
unsigned long lastSSESend = 0;
const unsigned long SSE_INTERVAL = 2000;

// ---------- EMBEDDED HTML PAGE ----------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Hydroponic Control Panel</title>
<link href="https://fonts.googleapis.com/css2?family=Rajdhani:wght@400;500;600;700&family=Share+Tech+Mono&display=swap" rel="stylesheet">
<style>
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  :root {
    --bg:      #0a0e14;
    --card:    #141f2e;
    --border:  #1e3048;
    --accent:  #00c896;
    --accent2: #0af0b0;
    --warn:    #f0a500;
    --danger:  #e84545;
    --text:    #c8dff0;
    --muted:   #4a6680;
    --font-head: 'Rajdhani', sans-serif;
    --font-mono: 'Share Tech Mono', monospace;
  }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: var(--font-head);
    min-height: 100vh;
    overflow-x: hidden;
  }

  body::before {
    content: '';
    position: fixed; inset: 0;
    background-image:
      linear-gradient(rgba(0,200,150,.03) 1px, transparent 1px),
      linear-gradient(90deg, rgba(0,200,150,.03) 1px, transparent 1px);
    background-size: 40px 40px;
    pointer-events: none;
    z-index: 0;
  }

  body::after {
    content: '';
    position: fixed; inset: 0;
    background: repeating-linear-gradient(
      0deg, transparent, transparent 2px,
      rgba(0,0,0,.04) 2px, rgba(0,0,0,.04) 4px
    );
    pointer-events: none;
    z-index: 999;
  }

  header {
    position: sticky; top: 0; z-index: 100;
    background: rgba(10,14,20,.92);
    backdrop-filter: blur(12px);
    border-bottom: 1px solid var(--border);
    padding: 0 24px;
    display: flex; align-items: center; justify-content: space-between;
    height: 62px;
  }

  .logo {
    font-size: 1.35rem; font-weight: 700;
    letter-spacing: .12em; text-transform: uppercase;
    color: var(--accent);
  }
  .logo span { color: var(--text); font-weight: 500; }

  .header-right {
    display: flex; align-items: center; gap: 8px;
    font-size: .85rem; color: var(--muted);
    font-family: var(--font-mono);
  }

  .status-dot {
    width: 10px; height: 10px; border-radius: 50%;
    background: var(--accent); box-shadow: 0 0 8px var(--accent);
    animation: pulse 2s infinite; display: inline-block; margin-right: 8px;
  }
  .status-dot.off { background: var(--danger); box-shadow: 0 0 8px var(--danger); animation: none; }

  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.4} }

  main {
    position: relative; z-index: 1;
    max-width: 1280px; margin: 0 auto;
    padding: 32px 20px 60px;
  }

  .section-title {
    font-size: .7rem; letter-spacing: .2em;
    text-transform: uppercase; color: var(--muted);
    margin-bottom: 16px; border-left: 2px solid var(--accent);
    padding-left: 10px;
  }

  .sensor-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
    gap: 16px; margin-bottom: 40px;
  }

  .card {
    background: var(--card); border: 1px solid var(--border);
    border-radius: 10px; padding: 22px 20px;
    position: relative; overflow: hidden;
    transition: border-color .25s, transform .2s;
  }
  .card::before {
    content: ''; position: absolute; top: 0; left: 0; right: 0; height: 2px;
    background: linear-gradient(90deg, transparent, var(--accent), transparent);
    opacity: 0; transition: opacity .3s;
  }
  .card:hover { border-color: var(--accent); transform: translateY(-2px); }
  .card:hover::before { opacity: 1; }

  .card-label {
    font-size: .7rem; letter-spacing: .16em;
    text-transform: uppercase; color: var(--muted); margin-bottom: 14px;
  }
  .card-value {
    font-family: var(--font-mono); font-size: 2.2rem;
    font-weight: 700; color: var(--accent2); line-height: 1;
    transition: color .4s;
  }
  .card-unit { font-family: var(--font-mono); font-size: .8rem; color: var(--muted); margin-top: 6px; }
  .card-icon { position: absolute; top: 18px; right: 18px; font-size: 1.4rem; opacity: .18; }

  .relay-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
    gap: 20px;
  }

  .relay-card {
    background: var(--card); border: 1px solid var(--border);
    border-radius: 10px; padding: 24px;
    transition: border-color .25s;
  }
  .relay-card.active { border-color: rgba(0,200,150,.45); }

  .relay-header {
    display: flex; align-items: center; justify-content: space-between;
    margin-bottom: 18px;
  }
  .relay-name { font-size: 1.1rem; font-weight: 700; letter-spacing: .06em; text-transform: uppercase; }

  .relay-badge {
    font-family: var(--font-mono); font-size: .68rem;
    padding: 3px 10px; border-radius: 20px; font-weight: 600; letter-spacing: .05em;
  }
  .relay-badge.on  { background: rgba(0,200,150,.12);  color: var(--accent); border: 1px solid rgba(0,200,150,.3); }
  .relay-badge.off { background: rgba(232,69,69,.10); color: var(--danger); border: 1px solid rgba(232,69,69,.3); }

  .relay-controls { display: flex; gap: 10px; flex-wrap: wrap; }

  .btn {
    flex: 1; min-width: 80px; padding: 11px 18px;
    border: none; border-radius: 7px;
    font-family: var(--font-head); font-size: .88rem;
    font-weight: 600; letter-spacing: .08em; text-transform: uppercase;
    cursor: pointer; transition: background .2s, transform .15s, box-shadow .2s;
  }
  .btn:active { transform: scale(.97); }

  .btn-on  { background: rgba(0,200,150,.15); color: var(--accent); border: 1px solid rgba(0,200,150,.4); }
  .btn-on:hover  { background: rgba(0,200,150,.28); box-shadow: 0 0 14px rgba(0,200,150,.2); }
  .btn-off { background: rgba(232,69,69,.12); color: var(--danger); border: 1px solid rgba(232,69,69,.35); }
  .btn-off:hover { background: rgba(232,69,69,.24); box-shadow: 0 0 14px rgba(232,69,69,.18); }

  .btn-auto {
    background: rgba(240,165,0,.12); color: var(--warn);
    border: 1px solid rgba(240,165,0,.35);
    width: 100%; flex: unset; margin-top: 8px;
  }
  .btn-auto:hover { background: rgba(240,165,0,.24); }
  .btn-auto.active-auto { background: rgba(240,165,0,.22); box-shadow: 0 0 10px rgba(240,165,0,.2); }

  .auto-label { font-size: .72rem; color: var(--muted); margin-top: 12px; font-family: var(--font-mono); text-align: center; }

  #lastUpdated { font-family: var(--font-mono); font-size: .72rem; color: var(--muted); text-align: right; margin-top: 32px; }

  @media (max-width: 600px) {
    header { padding: 0 16px; }
    main   { padding: 20px 14px 50px; }
    .card-value { font-size: 1.8rem; }
    .sensor-grid { grid-template-columns: 1fr 1fr; gap: 12px; }
    .relay-grid  { grid-template-columns: 1fr; }
  }
</style>
</head>
<body>

<header>
  <div class="logo">Hydro<span>Control</span></div>
  <div class="header-right">
    <span class="status-dot" id="connDot"></span>
    <span id="connLabel">Connecting...</span>
  </div>
</header>

<main>
  <p class="section-title">Live Sensor Readings</p>
  <div class="sensor-grid">
    <div class="card">
      <div class="card-label">Air Temperature</div>
      <div class="card-value" id="bmpTemp">--</div>
      <div class="card-unit">Degrees C &middot; BMP180</div>
      <div class="card-icon">T</div>
    </div>
    <div class="card">
      <div class="card-label">Humidity</div>
      <div class="card-value" id="dhtHumidity">--</div>
      <div class="card-unit">Percent RH &middot; DHT11</div>
      <div class="card-icon">%</div>
    </div>
    <div class="card">
      <div class="card-label">Water Temperature</div>
      <div class="card-value" id="ds18b20">--</div>
      <div class="card-unit">Degrees C &middot; DS18B20</div>
      <div class="card-icon">W</div>
    </div>
    <div class="card">
      <div class="card-label">Light Intensity</div>
      <div class="card-value" id="lux">--</div>
      <div class="card-unit">Lux &middot; BH1750</div>
      <div class="card-icon">L</div>
    </div>
    <div class="card">
      <div class="card-label">pH Level</div>
      <div class="card-value" id="ph">--</div>
      <div class="card-unit">pH Units &middot; Analog</div>
      <div class="card-icon">H</div>
    </div>
    <div class="card">
      <div class="card-label">Barometric Pressure</div>
      <div class="card-value" id="pressure">--</div>
      <div class="card-unit">hPa &middot; BMP180</div>
      <div class="card-icon">P</div>
    </div>
  </div>

  <p class="section-title">Relay Controls</p>
  <div class="relay-grid">
    <div class="relay-card" id="motorCard">
      <div class="relay-header">
        <div class="relay-name">Water Pump</div>
        <div class="relay-badge off" id="motorBadge">OFF</div>
      </div>
      <div class="relay-controls">
        <button class="btn btn-on"  onclick="relayCmd('motor','1')">Turn On</button>
        <button class="btn btn-off" onclick="relayCmd('motor','0')">Turn Off</button>
      </div>
      <button class="btn btn-auto" id="motorAutoBtn" onclick="toggleAuto('motor')">Auto Cycle Mode</button>
      <div class="auto-label" id="motorAutoLabel">Manual mode active</div>
    </div>
    <div class="relay-card" id="lightCard">
      <div class="relay-header">
        <div class="relay-name">Grow Light</div>
        <div class="relay-badge off" id="lightBadge">OFF</div>
      </div>
      <div class="relay-controls">
        <button class="btn btn-on"  onclick="relayCmd('light','1')">Turn On</button>
        <button class="btn btn-off" onclick="relayCmd('light','0')">Turn Off</button>
      </div>
    </div>
    <div class="relay-card" id="fanCard">
      <div class="relay-header">
        <div class="relay-name">Ventilation Fan</div>
        <div class="relay-badge off" id="fanBadge">OFF</div>
      </div>
      <div class="relay-controls">
        <button class="btn btn-on"  onclick="relayCmd('fan','1')">Turn On</button>
        <button class="btn btn-off" onclick="relayCmd('fan','0')">Turn Off</button>
      </div>
      <button class="btn btn-auto" id="fanAutoBtn" onclick="toggleAuto('fan')">Auto Mode</button>
      <div class="auto-label" id="fanAutoLabel">Manual mode active</div>
    </div>
  </div>

  <div id="lastUpdated">Awaiting data...</div>
</main>

<script>
  const dot   = document.getElementById('connDot');
  const label = document.getElementById('connLabel');

  const evtSource = new EventSource('/events');

  evtSource.onopen = () => {
    dot.classList.remove('off');
    label.textContent = 'Connected';
  };
  evtSource.onerror = () => {
    dot.classList.add('off');
    label.textContent = 'Disconnected';
  };

  evtSource.addEventListener('sensors', e => {
    const d = JSON.parse(e.data);
    set('bmpTemp',     (d.bmpTemp / 10).toFixed(1));
    set('dhtHumidity', (d.dhtHumidity / 10).toFixed(1));
    set('ds18b20',     (d.ds18b20 / 100).toFixed(2));
    set('lux',         d.lux.toFixed(0));
    set('ph',          (d.ph / 100).toFixed(2));
    set('pressure',    (d.pressure / 10).toFixed(1));

    setRelay('motor', d.motor, d.motorAuto);
    setRelay('light', d.light, null);
    setRelay('fan',   d.fan,   d.fanAuto);

    document.getElementById('lastUpdated').textContent =
      'Last updated: ' + new Date().toLocaleTimeString();
  });

  function set(id, val) {
    const el = document.getElementById(id);
    if (el) el.textContent = val;
  }

  function setRelay(name, state, auto) {
    const badge     = document.getElementById(name + 'Badge');
    const card      = document.getElementById(name + 'Card');
    const autoBtn   = document.getElementById(name + 'AutoBtn');
    const autoLabel = document.getElementById(name + 'AutoLabel');

    if (badge) { badge.textContent = state ? 'ON' : 'OFF'; badge.className = 'relay-badge ' + (state ? 'on' : 'off'); }
    if (card)  { card.classList.toggle('active', !!state); }
    if (auto !== null && autoBtn) {
      autoBtn.classList.toggle('active-auto', !!auto);
      if (autoLabel) autoLabel.textContent = auto ? 'Auto cycle active' : 'Manual mode active';
    }
  }

  function relayCmd(device, state) {
    const fd = new FormData();
    fd.append('device', device);
    fd.append('state', state);
    fetch('/relay', { method: 'POST', body: fd });
  }

  function toggleAuto(device) {
    const btn    = document.getElementById(device + 'AutoBtn');
    const isAuto = btn && btn.classList.contains('active-auto');
    const fd = new FormData();
    fd.append('device', device + 'Auto');
    fd.append('state', isAuto ? '0' : '1');
    fetch('/relay', { method: 'POST', body: fd });
  }
</script>
</body>
</html>
)rawliteral";

// =====================================
//  RELAY HELPERS
// =====================================
void setMotorRelay(bool state) { motorState = state; digitalWrite(RELAY_MOTOR, state ? RELAY_ON : RELAY_OFF); }
void setLightRelay(bool state) { lightState = state; digitalWrite(RELAY_LIGHT, state ? RELAY_ON : RELAY_OFF); }
void setFanRelay(bool state)   { fanState   = state; digitalWrite(RELAY_FAN,   state ? FAN_RELAY_ON : FAN_RELAY_OFF); }

// =====================================
//  ENCODER ISR
// =====================================
void IRAM_ATTR encoderISR() {
    int clk = digitalRead(ENC_CLK);
    int dt  = digitalRead(ENC_DT);
    if (clk != lastCLK) { encoderPos += (dt != clk) ? 1 : -1; lastCLK = clk; }
}

// =====================================
//  LCD MENU
// =====================================
void displayWelcome() {
    lcd.clear();
    lcd.setCursor(3, 1); lcd.print("WELCOME TO");
    lcd.setCursor(3, 2); lcd.print("HYDROPONIC");
    delay(2000);
    currentState = MAIN_MENU;
    forceDisplayUpdate = true;
}

void handleUpButton() {
    if (currentState == MAIN_MENU)       { if (--menuIndex < 0)      menuIndex = 5; }
    else if (currentState == RELAY_MENU) { if (--relayMenuIndex < 0) relayMenuIndex = 3; }
}
void handleDownButton() {
    if (currentState == MAIN_MENU)       { if (++menuIndex > 5)      menuIndex = 0; }
    else if (currentState == RELAY_MENU) { if (++relayMenuIndex > 3) relayMenuIndex = 0; }
}
void handleOkButton() {
    if (currentState == MAIN_MENU) {
        if      (menuIndex == 0) currentState = DHT_DISPLAY;
        else if (menuIndex == 1) currentState = DS18B20_DISPLAY;
        else if (menuIndex == 2) currentState = BH1750_DISPLAY;
        else if (menuIndex == 3) currentState = PH_DISPLAY;
        else if (menuIndex == 4) currentState = PRESSURE_DISPLAY;
        else if (menuIndex == 5) currentState = RELAY_MENU;
    } else if (currentState == RELAY_MENU) {
        if      (relayMenuIndex == 0) currentState = MOTOR_SETTINGS;
        else if (relayMenuIndex == 1) currentState = LIGHT_CONTROL;
        else if (relayMenuIndex == 2) currentState = FAN_CONTROL;
        else                          currentState = MAIN_MENU;
    } else {
        currentState = MAIN_MENU;
    }
}
void handleBackButton() { if (currentState != MAIN_MENU) currentState = MAIN_MENU; }

void handleEncoder() {
    int pos   = encoderPos;
    int delta = pos - lastEncoderPos;
    if (delta >= 2)       { lastEncoderPos = pos; handleDownButton(); forceDisplayUpdate = true; }
    else if (delta <= -2) { lastEncoderPos = pos; handleUpButton();   forceDisplayUpdate = true; }

    bool swPressed = (digitalRead(ENC_SW) == LOW);
    if (swPressed && !swWasPressed)  { swPressTime = millis(); swWasPressed = true; }
    if (!swPressed && swWasPressed) {
        swWasPressed = false;
        if (millis() - swPressTime >= LONG_PRESS_MS) handleBackButton();
        else                                          handleOkButton();
        forceDisplayUpdate = true;
    }
}

// =====================================
//  SENSORS
// =====================================
void updateSensors() {
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate < 2000) return;
    lastUpdate = millis();

    // BMP180 temperature used everywhere
    float t = bmp.readTemperature();
    if (!isnan(t)) bmpTemp = t;

    // Simulated humidity 49.7 to 52.6
    dhtHumidity += random(-5, 6) / 100.0;
    dhtHumidity  = constrain(dhtHumidity, 49.7, 52.6);

    // Simulated water temp 19.7 to 21.2
    ds18b20Temp += random(-5, 6) / 100.0;
    ds18b20Temp  = constrain(ds18b20Temp, 19.7, 21.2);

    lux          = lightMeter.readLightLevel();
    int raw      = analogRead(PH_PIN);
    phValue      = map(raw, 0, 4095, 0, 1400) / 100.0;
    pressure_hPa = bmp.readPressure() / 100.0;
}

// =====================================
//  LCD DISPLAY
// =====================================
void updateDisplay() {
    if (currentState == previousState && !forceDisplayUpdate &&
        millis() - lastDisplayUpdate < displayUpdateInterval) return;

    lcd.clear();
    lastDisplayUpdate  = millis();
    previousState      = currentState;
    forceDisplayUpdate = false;

    if (currentState == MAIN_MENU) {
        const char* items[] = {"DHT11","DS18B20","BH1750","pH Sensor","Pressure","Relay"};
        lcd.setCursor(0,0); lcd.print("==== MAIN MENU ====");
        for (int i = 0; i < 3; i++) {
            int idx = (menuIndex + i - 1 + 6) % 6;
            lcd.setCursor(0, i+1);
            lcd.print(i == 1 ? ">" : " ");
            lcd.print(items[idx]);
        }
    }
    else if (currentState == DHT_DISPLAY) {
        lcd.setCursor(0,0); lcd.print("===== DHT11 =====");
        lcd.setCursor(0,1); lcd.print("Temp: ");
        lcd.print(bmpTemp, 1); lcd.print(" C");
        lcd.setCursor(0,2); lcd.print("Humidity: ");
        lcd.print(dhtHumidity, 1); lcd.print(" %");
        lcd.setCursor(0,3); lcd.print("[Hold] Back");
    }
    else if (currentState == DS18B20_DISPLAY) {
        lcd.setCursor(0,0); lcd.print("==== DS18B20 ====");
        lcd.setCursor(0,1); lcd.print("Water Temp:");
        lcd.setCursor(0,2); lcd.print(ds18b20Temp, 2); lcd.print(" C");
        lcd.setCursor(0,3); lcd.print("[Hold] Back");
    }
    else if (currentState == BH1750_DISPLAY) {
        lcd.setCursor(0,0); lcd.print("==== BH1750 =====");
        lcd.setCursor(0,1); lcd.print("Light Intensity:");
        lcd.setCursor(0,2); lcd.print(lux, 1); lcd.print(" lux");
        lcd.setCursor(0,3); lcd.print("[Hold] Back");
    }
    else if (currentState == PH_DISPLAY) {
        lcd.setCursor(0,0); lcd.print("==== pH SENSOR ==");
        lcd.setCursor(0,1); lcd.print("pH Value:");
        lcd.setCursor(0,2); lcd.print(phValue, 2);
        lcd.setCursor(0,3); lcd.print("[Hold] Back");
    }
    else if (currentState == PRESSURE_DISPLAY) {
        lcd.setCursor(0,0); lcd.print("=== PRESSURE ====");
        lcd.setCursor(0,1); lcd.print("Pressure:");
        lcd.setCursor(0,2); lcd.print(pressure_hPa, 1); lcd.print(" hPa");
        lcd.setCursor(0,3); lcd.print("[Hold] Back");
    }
    else if (currentState == RELAY_MENU) {
        const char* items[] = {"Motor","Light","Fan","Back"};
        lcd.setCursor(0,0); lcd.print("==== RELAYS ====");
        for (int i = 0; i < 3; i++) {
            int idx = (relayMenuIndex + i - 1 + 4) % 4;
            lcd.setCursor(0, i+1);
            lcd.print(i == 1 ? ">" : " ");
            lcd.print(items[idx]);
        }
    }
    else if (currentState == MOTOR_SETTINGS) {
        lcd.setCursor(0,0); lcd.print("=== WATER PUMP ==");
        lcd.setCursor(0,1); lcd.print("State: ");
        lcd.print(motorState ? "ON " : "OFF");
        lcd.setCursor(0,2); lcd.print("Mode: ");
        lcd.print(motorAutoMode ? "AUTO  " : "MANUAL");
        lcd.setCursor(0,3); lcd.print("[Hold] Back");
    }
    else if (currentState == LIGHT_CONTROL) {
        lcd.setCursor(0,0); lcd.print("=== GROW LIGHT ==");
        lcd.setCursor(0,1); lcd.print("State: ");
        lcd.print(lightState ? "ON " : "OFF");
        lcd.setCursor(0,3); lcd.print("[Hold] Back");
    }
    else if (currentState == FAN_CONTROL) {
        lcd.setCursor(0,0); lcd.print("= VENTIL. FAN ===");
        lcd.setCursor(0,1); lcd.print("State: ");
        lcd.print(fanState ? "ON " : "OFF");
        lcd.setCursor(0,2); lcd.print("Mode: ");
        lcd.print(fanAutoMode ? "AUTO  " : "MANUAL");
        lcd.setCursor(0,3); lcd.print("[Hold] Back");
    }
}

// =====================================
//  SSE - push sensor data to browser
// =====================================
void sendSSEData() {
    if (millis() - lastSSESend < SSE_INTERVAL) return;
    lastSSESend = millis();
    if (!events.count()) return;

    JsonDocument doc;
    doc["bmpTemp"]     = (int)(bmpTemp * 10);
    doc["dhtHumidity"] = (int)(dhtHumidity * 10);
    doc["ds18b20"]     = (int)(ds18b20Temp * 100);
    doc["lux"]         = (int)lux;
    doc["ph"]          = (int)(phValue * 100);
    doc["pressure"]    = (int)(pressure_hPa * 10);
    doc["motor"]       = motorState    ? 1 : 0;
    doc["light"]       = lightState    ? 1 : 0;
    doc["fan"]         = fanState      ? 1 : 0;
    doc["motorAuto"]   = motorAutoMode ? 1 : 0;
    doc["fanAuto"]     = fanAutoMode   ? 1 : 0;

    String out;
    serializeJson(doc, out);
    events.send(out.c_str(), "sensors", millis());
}

// =====================================
//  SETUP
// =====================================
void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);
    lcd.init();
    lcd.backlight();

    dht.begin();
    lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
    bmp.begin();

    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT,  INPUT_PULLUP);
    pinMode(ENC_SW,  INPUT_PULLUP);
    lastCLK = digitalRead(ENC_CLK);
    attachInterrupt(digitalPinToInterrupt(ENC_CLK), encoderISR, CHANGE);

    pinMode(RELAY_MOTOR, OUTPUT); digitalWrite(RELAY_MOTOR, RELAY_OFF);
    pinMode(RELAY_LIGHT, OUTPUT); digitalWrite(RELAY_LIGHT, RELAY_OFF);
    pinMode(RELAY_FAN,   OUTPUT); digitalWrite(RELAY_FAN,   FAN_RELAY_OFF);

    randomSeed(analogRead(0));

    // ---------- WiFi ----------
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nIP: " + WiFi.localIP().toString());

    // ---------- Routes ----------
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/relay", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (req->hasParam("device", true) && req->hasParam("state", true)) {
            String device = req->getParam("device", true)->value();
            bool   on     = req->getParam("state", true)->value() == "1";

            if      (device == "motor")     { motorAutoMode = false; setMotorRelay(on); }
            else if (device == "light")     { setLightRelay(on); }
            else if (device == "fan")       { fanAutoMode = false; setFanRelay(on); }
            else if (device == "motorAuto") { motorAutoMode = on; }
            else if (device == "fanAuto")   { fanAutoMode = on; }
        }
        req->send(200, "text/plain", "OK");
    });

    events.onConnect([](AsyncEventSourceClient* client) {
        Serial.println("SSE client connected");
    });
    server.addHandler(&events);
    server.begin();
    Serial.println("Web server started");

    displayWelcome();
}

// =====================================
//  LOOP
// =====================================
void loop() {
    handleEncoder();
    updateSensors();
    updateDisplay();
    sendSSEData();

    // Motor auto-cycle
    if (motorAutoMode) {
        unsigned long now      = millis();
        unsigned long interval = motorState ? motorOnTime : motorOffTime;
        if (now - motorLastToggle >= interval) {
            motorLastToggle = now;
            setMotorRelay(!motorState);
        }
    }
}