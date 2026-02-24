#include <Arduino.h>

// Omnidirectional 4WD Mecanum RC Car
// ESP32-S3-WROOM + 2x L298N Motor Drivers
// WiFi AP mode with web-based controller
// + Lid control via ultrasonic sensor + servo (for Wander-Bin app)

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ----- WiFi Credentials -----
const char* ssid     = "Daniel";
const char* password = "hellobagia";

WebServer server(80);

// ----- Motor Pin Definitions -----
// Front L298N driver
const int FL_IN1 = 17;  // Front Left motor
const int FL_IN2 = 16;
const int FR_IN1 = 15;  // Front Right motor
const int FR_IN2 = 7;

// Back L298N driver
const int BL_IN1 = 14;  // Back Left motor
const int BL_IN2 = 13;
const int BR_IN1 = 12;  // Back Right motor
const int BR_IN2 = 11;

// All motor pins in an array for easy setup
const int motorPins[] = {
  FL_IN1, FL_IN2, FR_IN1, FR_IN2,
  BL_IN1, BL_IN2, BR_IN1, BR_IN2
};
const int NUM_MOTOR_PINS = sizeof(motorPins) / sizeof(motorPins[0]);

// ----- Ultrasonic Sensor Pins -----
// >>> CHANGE THESE to match your wiring <<<
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;

// ----- Servo Pin (MG996R) -----
// >>> CHANGE THIS to match your wiring <
const int SERVO_PIN = 4;

Servo lidServo;

// ----- Lid Control State -----
bool lidAllowOpen = false;       // Set by the web app JSON
bool lidIsOpen = false;           // Current lid position
String lastItemName = "";
String lastReason = "";

const float HAND_DISTANCE_CM = 15.0;  // Trigger distance
const int LID_OPEN_ANGLE = 90;        // Servo angle when open
const int LID_CLOSED_ANGLE = 0;       // Servo angle when closed
const unsigned long LID_OPEN_DURATION = 8000;  // Keep lid open for 8 seconds

unsigned long lidOpenedAt = 0;  // Timestamp when lid was opened

// ----- Ultrasonic Helper -----
float getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);  // Timeout 30ms (~5m max)
  if (duration == 0) return -1;  // No echo received
  return (duration * 0.0343) / 2.0;
}

// ----- Motor Helper Functions -----
// Set an individual motor: 1 = forward, -1 = backward, 0 = stop
void setMotor(int in1, int in2, int dir) {
  if (dir == 1) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  } else if (dir == -1) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }
}

// Set all four motors at once: FL, FR, BL, BR
void setMotors(int fl, int fr, int bl, int br) {
  setMotor(FL_IN1, FL_IN2, fl);
  setMotor(FR_IN1, FR_IN2, fr);
  setMotor(BL_IN1, BL_IN2, bl);
  setMotor(BR_IN1, BR_IN2, br);
}

// ----- Movement Functions -----
void stopCar()     { setMotors( 0,  0,  0,  0); }
void forward()     { setMotors( 1, -1, -1,  1); }
void backward()    { setMotors(-1,  1,  1, -1); }
void strafeLeft()  { setMotors( 1,  1,  1,  1); }
void strafeRight() { setMotors(-1, -1, -1, -1); }
void rotateLeft()  { setMotors( 1, -1,  1, -1); }
void rotateRight() { setMotors(-1,  1, -1,  1); }
void forwardLeft() { setMotors( 1,  0,  0,  1); }
void forwardRight(){ setMotors( 0, -1, -1,  0); }
void backLeft()    { setMotors( 0,  1,  1,  0); }
void backRight()   { setMotors(-1,  0,  0, -1); }

// ----- CORS Helper -----
void sendCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ----- Lid Control Endpoint -----
// Receives JSON: { "allowOpen": true/false, "itemName": "...", "reason": "..." }
void handleLidControl() {
  sendCORSHeaders();

  // Handle preflight OPTIONS request (CORS)
  if (server.method() == HTTP_OPTIONS) {
    server.send(204);
    return;
  }

  // Parse incoming JSON
  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }

  // Extract fields
  lidAllowOpen = doc["allowOpen"] | false;
  lastItemName = doc["itemName"] | "Unknown";
  lastReason   = doc["reason"]   | "No reason";

  // Print to Serial Monitor
  Serial.println("──────────────────────────────");
  Serial.println("📦 Received from Wander-Bin App:");
  Serial.print("   Item:       "); Serial.println(lastItemName);
  Serial.print("   Reason:     "); Serial.println(lastReason);
  Serial.print("   Allow Open: "); Serial.println(lidAllowOpen ? "YES ✅" : "NO ❌");
  Serial.println("   ✅ Web App → ESP32 connection successful!");
  Serial.println("──────────────────────────────");

  if (lidAllowOpen) {
    Serial.println("🔊 Ultrasonic sensor ACTIVATED — waiting for hand within 15cm...");
  } else {
    Serial.println("🔒 Lid stays LOCKED.");
    // Make sure lid is closed if it was open
    if (lidIsOpen) {
      lidServo.write(LID_CLOSED_ANGLE);
      lidIsOpen = false;
      Serial.println("🔒 Lid closed.");
    }
  }

  // Send JSON response back to the web app
  String response;
  JsonDocument resDoc;
  resDoc["status"] = "ok";
  resDoc["allowOpen"] = lidAllowOpen;
  resDoc["itemName"] = lastItemName;
  resDoc["message"] = lidAllowOpen
    ? "Ultrasonic sensor activated. Wave hand to open lid."
    : "Lid locked. Item is not recyclable.";
  serializeJson(resDoc, response);

  server.send(200, "application/json", response);
}

// ----- Status Endpoint -----
void handleStatus() {
  sendCORSHeaders();

  String response;
  JsonDocument doc;
  doc["status"] = "online";
  doc["lidAllowOpen"] = lidAllowOpen;
  doc["lidIsOpen"] = lidIsOpen;
  doc["lastItem"] = lastItemName;
  serializeJson(doc, response);

  server.send(200, "application/json", response);
}

// ----- Web UI -----
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>RC Car Controller</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    background: #0f0f1a;
    color: #e0e0e0;
    display: flex;
    flex-direction: column;
    align-items: center;
    min-height: 100vh;
    overflow: hidden;
    touch-action: manipulation;
    -webkit-user-select: none;
    user-select: none;
  }
  h1 {
    font-size: 1.3rem;
    margin: 12px 0 4px;
    color: #7eb8ff;
    letter-spacing: 1px;
  }
  .status {
    font-size: 0.75rem;
    color: #6a6a8a;
    margin-bottom: 8px;
  }
  .status span { color: #4cff9f; }
  .pad {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    grid-template-rows: repeat(3, 1fr);
    gap: 6px;
    width: min(85vw, 340px);
    height: min(85vw, 340px);
    margin-bottom: 10px;
  }
  .btn {
    display: flex;
    align-items: center;
    justify-content: center;
    flex-direction: column;
    border: none;
    border-radius: 14px;
    font-size: 0.85rem;
    font-weight: 600;
    color: #c8d6e5;
    cursor: pointer;
    transition: background 0.1s, transform 0.08s;
    -webkit-tap-highlight-color: transparent;
  }
  .btn svg { width: 28px; height: 28px; margin-bottom: 2px; fill: currentColor; }
  .btn.dir  { background: #1e2a3a; }
  .btn.diag { background: #192030; color: #8a9ab5; }
  .btn.stop { background: #3a1525; color: #ff6b8a; }
  .btn.rot  { background: #1a2535; color: #7eb8ff; }
  .btn:active, .btn.active { transform: scale(0.93); }
  .btn.dir:active,  .btn.dir.active  { background: #2a4060; color: #fff; }
  .btn.diag:active, .btn.diag.active { background: #253050; color: #c0d0f0; }
  .btn.stop:active, .btn.stop.active { background: #6a1030; color: #fff; }
  .btn.rot:active,  .btn.rot.active  { background: #254565; color: #fff; }
  .rotate-row {
    display: flex;
    gap: 6px;
    width: min(85vw, 340px);
    justify-content: center;
  }
  .rotate-row .btn {
    width: 48%;
    height: 54px;
    border-radius: 14px;
  }
  .label { font-size: 0.65rem; opacity: 0.7; margin-top: 1px; }
  .cmd-display {
    margin-top: 10px;
    font-size: 0.8rem;
    color: #4a5568;
    height: 1.2em;
  }
</style>
</head>
<body>
  <h1>RC CAR CONTROL</h1>
  <div class="status">WiFi: <span>MecanumCar</span> &middot; 192.168.4.1</div>

  <div class="pad">
    <button class="btn diag" id="FL">
      <svg viewBox="0 0 24 24"><path d="M14 3h-4l1.5 1.5L5 11l1.5 1.5L13 6l1.5 1.5z" transform="rotate(-45 12 12)"/></svg>
      <span class="label">FWD-L</span>
    </button>
    <button class="btn dir" id="F">
      <svg viewBox="0 0 24 24"><path d="M12 4l-6 6h4v6h4v-6h4z"/></svg>
      <span class="label">FWD</span>
    </button>
    <button class="btn diag" id="FR">
      <svg viewBox="0 0 24 24"><path d="M14 3h-4l1.5 1.5L5 11l1.5 1.5L13 6l1.5 1.5z" transform="rotate(45 12 12)"/></svg>
      <span class="label">FWD-R</span>
    </button>
    <button class="btn dir" id="SL">
      <svg viewBox="0 0 24 24"><path d="M4 12l6-6v4h6v4h-6v4z"/></svg>
      <span class="label">LEFT</span>
    </button>
    <button class="btn stop" id="S">
      <svg viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" rx="2"/></svg>
      <span class="label">STOP</span>
    </button>
    <button class="btn dir" id="SR">
      <svg viewBox="0 0 24 24"><path d="M20 12l-6-6v4H8v4h6v4z"/></svg>
      <span class="label">RIGHT</span>
    </button>
    <button class="btn diag" id="BL">
      <svg viewBox="0 0 24 24"><path d="M14 3h-4l1.5 1.5L5 11l1.5 1.5L13 6l1.5 1.5z" transform="rotate(-135 12 12)"/></svg>
      <span class="label">BWD-L</span>
    </button>
    <button class="btn dir" id="B">
      <svg viewBox="0 0 24 24"><path d="M12 20l6-6h-4V8H10v6H6z"/></svg>
      <span class="label">BWD</span>
    </button>
    <button class="btn diag" id="BR">
      <svg viewBox="0 0 24 24"><path d="M14 3h-4l1.5 1.5L5 11l1.5 1.5L13 6l1.5 1.5z" transform="rotate(135 12 12)"/></svg>
      <span class="label">BWD-R</span>
    </button>
  </div>

  <div class="rotate-row">
    <button class="btn rot" id="RL">
      <svg viewBox="0 0 24 24"><path d="M12.5 3a9 9 0 0 0-8.5 6h2.2a7 7 0 1 1-.7 5H3.3A9 9 0 1 0 12.5 3z"/><path d="M4 3v6h6L4 3z"/></svg>
      <span class="label">ROTATE L</span>
    </button>
    <button class="btn rot" id="RR">
      <svg viewBox="0 0 24 24"><path d="M11.5 3a9 9 0 0 1 8.5 6h-2.2a7 7 0 1 0 .7 5h2.2A9 9 0 1 1 11.5 3z"/><path d="M20 3v6h-6l6-6z"/></svg>
      <span class="label">ROTATE R</span>
    </button>
  </div>

  <div class="cmd-display" id="cmdDisplay"></div>

<script>
  const display = document.getElementById('cmdDisplay');
  let activeCmd = 'S';

  function sendCmd(cmd) {
    if (cmd === activeCmd && cmd !== 'S') return;
    activeCmd = cmd;
    display.textContent = cmd === 'S' ? '' : cmd;
    fetch('/cmd?move=' + cmd).catch(() => {});
  }

  // Bind all buttons
  const allIds = ['F','B','SL','SR','RL','RR','FL','FR','BL','BR','S'];
  allIds.forEach(id => {
    const btn = document.getElementById(id);
    if (!btn) return;

    // Mouse events
    btn.addEventListener('mousedown', e => { e.preventDefault(); sendCmd(id); btn.classList.add('active'); });
    btn.addEventListener('mouseup',   e => { e.preventDefault(); sendCmd('S'); btn.classList.remove('active'); });
    btn.addEventListener('mouseleave',e => { if (btn.classList.contains('active')) { sendCmd('S'); btn.classList.remove('active'); } });

    // Touch events
    btn.addEventListener('touchstart', e => { e.preventDefault(); sendCmd(id); btn.classList.add('active'); });
    btn.addEventListener('touchend',   e => { e.preventDefault(); sendCmd('S'); btn.classList.remove('active'); });
    btn.addEventListener('touchcancel',e => { sendCmd('S'); btn.classList.remove('active'); });
  });

  // Safety: stop if window loses focus
  window.addEventListener('blur', () => sendCmd('S'));
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

// ----- Command Handler (car movement) -----
void handleCommand() {
  String move = server.arg("move");

  if      (move == "F")  forward();
  else if (move == "B")  backward();
  else if (move == "SL") strafeLeft();
  else if (move == "SR") strafeRight();
  else if (move == "RL") rotateLeft();
  else if (move == "RR") rotateRight();
  else if (move == "FL") forwardLeft();
  else if (move == "FR") forwardRight();
  else if (move == "BL") backLeft();
  else if (move == "BR") backRight();
  else                   stopCar();

  server.send(200, "text/plain", "OK");
}

// ----- Setup -----
void setup() {
  Serial.begin(9600);

  // Motor pins disabled — not used on ZY-ESP32 board
  // for (int i = 0; i < NUM_MOTOR_PINS; i++) {
  //   pinMode(motorPins[i], OUTPUT);
  //   digitalWrite(motorPins[i], LOW);
  // }

  // Initialize ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize servo
  lidServo.attach(SERVO_PIN, 500, 2500);  // MG996R pulse range: 500–2500µs
  lidServo.write(LID_CLOSED_ANGLE);  // Start closed
  Serial.println("🔧 Servo initialized at 0° (closed)");

// Connect to WiFi network (Station mode)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\nConnecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {  // 20 second timeout
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n========================================");
    Serial.println("   🦀 Wander-Bin Car Started!");
    Serial.println("========================================");
    Serial.print("   Connected to:  ");
    Serial.println(ssid);
    Serial.println("----------------------------------------");
    Serial.print("   📡 ESP32 IP:   http://");
    Serial.println(WiFi.localIP());
    Serial.println("========================================\n");
  } else {
    Serial.println("\n❌ WiFi connection failed!");
    Serial.println("   Check SSID/password and make sure hotspot is 2.4GHz");
  }

  // Register web routes
  server.on("/", handleRoot);
  server.on("/cmd", handleCommand);
  server.on("/lid-control", HTTP_POST, handleLidControl);
  server.on("/lid-control", HTTP_OPTIONS, []() {
    sendCORSHeaders();
    server.send(204);
  });
  server.on("/status", handleStatus);
  server.begin();
  Serial.println("✅ Web server started\n");
}

// ----- Main Loop -----
void loop() {
  server.handleClient();

  // ── Lid control logic (runs every loop cycle) ──
  // Only check ultrasonic if the web app said allowOpen = true
  if (lidAllowOpen && !lidIsOpen) {
    float distance = getDistanceCM();

    // Valid reading and within range
    if (distance > 0 && distance <= HAND_DISTANCE_CM) {
      Serial.print("👋 Hand detected at ");
      Serial.print(distance, 1);
      Serial.println(" cm — Opening lid!");

      lidServo.write(LID_OPEN_ANGLE);
      lidIsOpen = true;
      lidOpenedAt = millis();
    }
  }

  // Auto-close lid after LID_OPEN_DURATION
  if (lidIsOpen && (millis() - lidOpenedAt >= LID_OPEN_DURATION)) {
    Serial.println("⏰ Lid open timeout — Closing lid.");
    lidServo.write(LID_CLOSED_ANGLE);
    lidIsOpen = false;
    lidAllowOpen = false;  // Reset — require new scan
  }

  delay(50);
}
