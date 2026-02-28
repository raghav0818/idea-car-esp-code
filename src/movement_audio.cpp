#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// I2S Audio Libraries
#include "AudioFileSourcePROGMEM.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"
#include "audiodata.h" // User must update this file as described above!

// ==========================================
// --- NETWORK CONFIGURATION ---
// ==========================================

// 1. AP Mode (Phone connects here to drive the car)
const char* host_ap_ssid = "WanderBin-Mover";
const char* host_ap_pass = "moverpass";

// 2. STA Mode (ESP32 connects here to read audio triggers)
const char* target_ssid = "WanderBin-Robot";
const char* target_pass = "wanderbinpass";
const char* statusUrl  = "http://192.168.4.1/status"; // Brain ESP32 IP

WebServer server(80);
HTTPClient httpClient;

// ==========================================
// --- HARDWARE CONFIGURATION (ESP32-S3) ---
// ==========================================

// Motor Driver Pins (as specified by user image reference)
//Front motor driver
const int MOT_A_IN1 = 4;
const int MOT_A_IN2 = 5;
const int MOT_A_IN3 = 6;
const int MOT_A_IN4 = 7;

//Back motor driver
const int MOT_B_IN1 = 11;
const int MOT_B_IN2 = 12;
const int MOT_B_IN3 = 13;
const int MOT_B_IN4 = 14;

// Audio Pins (MAX98357A I2S connection)
const int I2S_BCLK = 36;
const int I2S_LRCK = 35;
const int I2S_DIN  = 37;

// ==========================================
// --- GLOBAL STATE ---
// ==========================================

// Global pointers for Audio Control
AudioFileSourcePROGMEM *audioSource = nullptr;
AudioGeneratorWAV *audioGenerator    = nullptr;
AudioOutputI2S *audioOutput         = nullptr;

// Networking & Audio Logic Timers
unsigned long lastStatusCheckTime = 0;
const unsigned long checkInterval = 500; // Poll brain every 500ms
bool staConnected = false;

// Variables to track remote data state changes
bool lastRemoteLidIsOpen = false;
String lastRemoteScannedItem = "";

// ==========================================
// --- MOTOR MOVEMENT LOGIC (Simplifed) ---
// ==========================================

// Based on standard L298N/Differential drive. Adjust mapping if needed.
void stopMotors() {
  digitalWrite(MOT_A_IN1, LOW); digitalWrite(MOT_A_IN2, LOW);
  digitalWrite(MOT_A_IN3, LOW); digitalWrite(MOT_A_IN4, LOW);
  digitalWrite(MOT_B_IN1, LOW); digitalWrite(MOT_B_IN2, LOW);
  digitalWrite(MOT_B_IN3, LOW); digitalWrite(MOT_B_IN4, LOW);
}

void moveForward() {
  stopMotors(); // Clean slate
  digitalWrite(MOT_A_IN1, HIGH); digitalWrite(MOT_A_IN2, LOW);
  digitalWrite(MOT_A_IN3, HIGH); digitalWrite(MOT_A_IN4, LOW);
  digitalWrite(MOT_B_IN1, HIGH); digitalWrite(MOT_B_IN2, LOW);
  digitalWrite(MOT_B_IN3, HIGH); digitalWrite(MOT_B_IN4, LOW);
}

void moveBackward() {
  stopMotors();
  digitalWrite(MOT_A_IN1, LOW); digitalWrite(MOT_A_IN2, HIGH);
  digitalWrite(MOT_A_IN3, LOW); digitalWrite(MOT_A_IN4, HIGH);
  digitalWrite(MOT_B_IN1, LOW); digitalWrite(MOT_B_IN2, HIGH);
  digitalWrite(MOT_B_IN3, LOW); digitalWrite(MOT_B_IN4, HIGH);
}

void turnLeft() {
  stopMotors();
  digitalWrite(MOT_A_IN1, LOW); digitalWrite(MOT_A_IN2, HIGH);
  digitalWrite(MOT_A_IN3, LOW); digitalWrite(MOT_A_IN4, HIGH);
  digitalWrite(MOT_B_IN1, HIGH); digitalWrite(MOT_B_IN2, LOW);
  digitalWrite(MOT_B_IN3, HIGH); digitalWrite(MOT_B_IN4, LOW);
}

void turnRight() {
  stopMotors();
  digitalWrite(MOT_A_IN1, HIGH); digitalWrite(MOT_A_IN2, LOW);
  digitalWrite(MOT_A_IN3, HIGH); digitalWrite(MOT_A_IN4, LOW);
  digitalWrite(MOT_B_IN1, LOW); digitalWrite(MOT_B_IN2, HIGH);
  digitalWrite(MOT_B_IN3, LOW); digitalWrite(MOT_B_IN4, HIGH);
}

// ==========================================
// --- I2S AUDIO LOGIC ---
// ==========================================

// Helper to stop any current sound safely
void stopCurrentlyPlaying() {
  if (audioGenerator) {
    if (audioGenerator->isRunning()) {
      audioGenerator->stop();
    }
    delete audioGenerator;
    audioGenerator = nullptr;
  }
  if (audioSource) {
    delete audioSource;
    audioSource = nullptr;
  }
}

enum SoundType { HAPPY, SAD };

// Plays raw audio from audiodata.h via I2S
void playSound(SoundType type) {
  stopCurrentlyPlaying(); // Stop old sound before starting new

  Serial.print("🔊 Audio Triggered: ");
  Serial.println(type == HAPPY ? "HAPPY (Thank you!)" : "SAD (Wrong Bin)");

  // Assign the correct memory pointer and length from audiodata.h
  const unsigned char* targetData = (type == HAPPY) ? rawData_Happy : rawData_Sad;
  unsigned int targetLen          = (type == HAPPY) ? rawData_Happy_len : rawData_Sad_len;

  audioSource = new AudioFileSourcePROGMEM(targetData, targetLen);
  
  audioGenerator = new AudioGeneratorWAV();
  
  // Set volume (0.0 to 1.0)
  audioOutput->SetGain(0.8);
  
  audioGenerator->begin(audioSource, audioOutput);
}

// ==========================================
// --- WEB UI FOR DRIVING CAR (AP Mode) ---
// ==========================================

const char* rc_html = R"rawliteral(
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

// ==========================================
// --- COMMAND HANDLER (From Old UI) ---
// ==========================================
void handleCommand() {
  String move = server.arg("move");

  if      (move == "F")  moveForward();
  else if (move == "B")  moveBackward();
  else if (move == "SL" || move == "RL") turnLeft();  // Maps strafe/rotate left to turnLeft
  else if (move == "SR" || move == "RR") turnRight(); // Maps strafe/rotate right to turnRight
  else                   stopMotors();

  server.send(200, "text/plain", "OK");
}

// ==========================================
// --- SETUP ---
// ==========================================

void setup() {
  Serial.begin(115200);

  // Initialize Motor Pins
  pinMode(MOT_A_IN1, OUTPUT); pinMode(MOT_A_IN2, OUTPUT);
  pinMode(MOT_A_IN3, OUTPUT); pinMode(MOT_A_IN4, OUTPUT);
  pinMode(MOT_B_IN1, OUTPUT); pinMode(MOT_B_IN2, OUTPUT);
  pinMode(MOT_B_IN3, OUTPUT); pinMode(MOT_B_IN4, OUTPUT);
  stopMotors();
  Serial.println("🔧 Motor Pins initialized and Stopped.");

  // Initialize Audio Output (I2S)
  // Use specific I2S port 0 on S3
  audioOutput = new AudioOutputI2S(0, 0); 
  audioOutput->SetPinout(I2S_BCLK, I2S_LRCK, I2S_DIN);
  Serial.println("🔧 I2S Audio Pins initialized.");

  // 1. --- Start Access Point (for RC Driving) ---
  // THE FIX: Set this AP to 192.168.5.1 so it doesn't conflict with the Brain ESP
  IPAddress local_ip(192, 168, 5, 1);
  IPAddress gateway(192, 168, 5, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);

  WiFi.softAP(host_ap_ssid, host_ap_pass);
  Serial.print("📡 Mover AP Started: ");
  Serial.println(host_ap_ssid);
  Serial.println("🔗 Connect to http://192.168.5.1 to Drive.");

  // --- THE NEW ROUTES ---
  // (Make sure "html" perfectly matches the name of the string variable you pasted!)
  server.on("/", []() { server.send(200, "text/html", rc_html); });
  server.on("/cmd", handleCommand);
  
  server.begin();
  Serial.println("✅ Web Server for driving started.");

  // 2. --- Start Station Mode (to read Audio Triggers) ---
  Serial.println("\n------------------------------------");
  Serial.print("Connecting to WanderBin-Robot Wi-Fi: ");
  Serial.println(target_ssid);
  WiFi.begin(target_ssid, target_pass);
}

// ==========================================
// --- POLLING LOGIC (STA Mode) ---
// ==========================================

void checkRemoteStatus() {
  // Non-blocking timer
  if (millis() - lastStatusCheckTime < checkInterval) return;
  lastStatusCheckTime = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (staConnected) {
      Serial.println("⚠️ Lost connection to Brain Wi-Fi.");
      staConnected = false;
    }
    return; // Wait for reconnect
  }

  if (!staConnected) {
    Serial.println("✅ Connected to Brain Wi-Fi.");
    staConnected = true;
  }

  // Fetch JSON status from Brain ESP32
  httpClient.begin(statusUrl);
  int httpCode = httpClient.GET();

  if (httpCode == 200) {
    String payload = httpClient.getString();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // Parse remote data
      bool remoteAllowOpen      = doc["lidAllowOpen"];
      bool remoteLidIsOpened     = doc["lidIsOpen"];
      String remoteLastItem     = doc["lastItem"] | "";

      // --- LOGIC FOR PLAYING AUDIO ---

      // 1. HAPPY TRIGGER: Played when user waves and the lid pops open for a valid item.
      if (remoteLidIsOpened && !lastRemoteLidIsOpen) {
        // Lid just physically opened!
        playSound(HAPPY); // Play "Thank you"
      }

      // 2. SAD TRIGGER: Played when a NEW scan occurs, but permission is denied (Wrong Bin).
      if (remoteLastItem != lastRemoteScannedItem && remoteLastItem != "") {
        if (!remoteAllowOpen) {
          // New scan, but bad item.
          playSound(SAD); // Play "Wrong Bin"
        }
      }

      // Update tracking variables
      lastRemoteLidIsOpen = remoteLidIsOpened;
      lastRemoteScannedItem = remoteLastItem;
    }
  }
  httpClient.end();
}

// ==========================================
// --- MAIN LOOP ---
// ==========================================

void loop() {
  // Handle RC Driving WebServer (AP Mode)
  server.handleClient();

  // Poll Remote Brain Status (STA Mode)
  checkRemoteStatus();

  // Handle Audio processing (CRITICAL: MUST RUN FAST)
  if (audioGenerator && audioGenerator->isRunning()) {
    if (!audioGenerator->loop()) {
      // Sound finished playing
      stopCurrentlyPlaying();
      Serial.println("🔊 Sound finished.");
    }
  }
}
