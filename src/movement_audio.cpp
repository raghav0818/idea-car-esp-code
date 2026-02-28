#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include "audiodata.h"

// ==========================================
// --- NETWORK CONFIGURATION ---
// ==========================================
const char* host_ap_ssid = "WanderBin-Mover";
const char* host_ap_pass = "moverpass";
const char* target_ssid = "WanderBin-Robot";
const char* target_pass = "wanderbinpass";
const char* statusUrl  = "http://192.168.4.1/status";

WebServer server(80);
HTTPClient httpClient;

// ==========================================
// --- WEB UI HTML ---
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
  <div class="status">WiFi: <span>WanderBin-Mover</span> &middot; 192.168.5.1</div>

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
// --- HARDWARE CONFIGURATION ---
// ==========================================
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

// Globals
unsigned long lastStatusCheckTime = 0;
const unsigned long checkInterval = 500;
bool staConnected = false;
bool lastRemoteLidIsOpen = false;
String lastRemoteScannedItem = "";

// ==========================================
// --- MOTOR MOVEMENT LOGIC (Restored from mecanum_car.cpp) ---
// ==========================================

// Helper to set individual motors (1 = forward, -1 = backward, 0 = stop)
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

// Set all four motors: FL (MOT_A 1/2), FR (MOT_A 3/4), BL (MOT_B 1/2), BR (MOT_B 3/4)
void setMotors(int fl, int fr, int bl, int br) {
  setMotor(MOT_A_IN1, MOT_A_IN2, fl);
  setMotor(MOT_A_IN3, MOT_A_IN4, fr);
  setMotor(MOT_B_IN1, MOT_B_IN2, bl);
  setMotor(MOT_B_IN3, MOT_B_IN4, br);
}

// Standard mecanum kinematics (O-configuration):
//   FL  FR        Forward/Backward: all 4 wheels same direction
//   BL  BR        Strafe: FL=BR opposite to FR=BL
//                 Rotate: left side (FL,BL) opposite to right side (FR,BR)
//                 Diagonal: only 2 diagonal wheels spin (e.g. FWD-R = FR+BL)
void stopMotors()  { setMotors( 0,  0,  0,  0); }
void forward()     { setMotors( 1,  1,  1,  1); }
void backward()    { setMotors(-1, -1, -1, -1); }
void strafeLeft()  { setMotors( 1, -1, -1,  1); }
void strafeRight() { setMotors(-1,  1,  1, -1); }
void rotateLeft()  { setMotors(-1,  1, -1,  1); }
void rotateRight() { setMotors( 1, -1,  1, -1); }
void forwardLeft() { setMotors( 1,  0,  0,  1); }
void forwardRight(){ setMotors( 0,  1,  1,  0); }
void backLeft()    { setMotors( 0, -1, -1,  0); }
void backRight()   { setMotors(-1,  0,  0, -1); }


// ==========================================
// --- WEB UI COMMAND HANDLER ---
// ==========================================
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
  else                   stopMotors();

  server.send(200, "text/plain", "OK");
}

// ==========================================
// --- NATIVE I2S AUDIO PLAYER (FreeRTOS) ---
// ==========================================
enum SoundType { HAPPY, SAD };
TaskHandle_t audioTaskHandle = NULL;
const int16_t* currentAudioData = nullptr;
unsigned int currentAudioSize = 0;

void audioTask(void *pvParameters) {
  size_t bytes_written;
  // Write raw PCM data directly to the amplifier
  i2s_write(I2S_NUM_0, currentAudioData, currentAudioSize, &bytes_written, portMAX_DELAY);
  i2s_zero_dma_buffer(I2S_NUM_0); // Prevent static after sound finishes
  
  audioTaskHandle = NULL;
  vTaskDelete(NULL); // Kill task when done
}

void playSound(SoundType type) {
  Serial.print("🔊 Audio Triggered: ");
  Serial.println(type == HAPPY ? "HAPPY" : "SAD");

  // If a sound is already playing, stop it instantly
  if (audioTaskHandle != NULL) {
    vTaskDelete(audioTaskHandle);
    audioTaskHandle = NULL;
    i2s_zero_dma_buffer(I2S_NUM_0);
  }

  // Load the correct sound from audiodata.h
  if (type == HAPPY) {
    currentAudioData = rawData_Happy;
    currentAudioSize = rawData_Happy_len; // <-- Changed here
  } else {
    currentAudioData = rawData_Sad;
    currentAudioSize = rawData_Sad_len;   // <-- Changed here
  }

  // Start the sound playing in the background
  xTaskCreate(audioTask, "AudioTask", 4096, NULL, 1, &audioTaskHandle);
}

// ==========================================
// --- POLLING LOGIC ---
// ==========================================
void checkRemoteStatus() {
  if (millis() - lastStatusCheckTime < checkInterval) return;
  lastStatusCheckTime = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (staConnected) { Serial.println("⚠️ Lost connection to Brain Wi-Fi."); staConnected = false; }
    return;
  }
  if (!staConnected) { Serial.println("✅ Connected to Brain Wi-Fi."); staConnected = true; }

  httpClient.begin(statusUrl);
  int httpCode = httpClient.GET();

  if (httpCode == 200) {
    String payload = httpClient.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      bool remoteAllowOpen      = doc["lidAllowOpen"];
      bool remoteLidIsOpened    = doc["lidIsOpen"];
      String remoteLastItem     = doc["lastItem"] | "";

      // 1. HAPPY TRIGGER
      if (remoteLidIsOpened && !lastRemoteLidIsOpen) {
        playSound(HAPPY);
      }
      // 2. SAD TRIGGER
      if (remoteLastItem != lastRemoteScannedItem && remoteLastItem != "") {
        if (!remoteAllowOpen) {
          playSound(SAD); 
        }
      }
      lastRemoteLidIsOpen = remoteLidIsOpened;
      lastRemoteScannedItem = remoteLastItem;
    }
  }
  httpClient.end();
}

// ==========================================
// --- SETUP ---
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(3000); 
  Serial.println("\n--- ESP32 STARTING BOOT SEQUENCE ---");

  // 1. Init Motors
  Serial.println("-> Starting Motor Init...");
  pinMode(MOT_A_IN1, OUTPUT); pinMode(MOT_A_IN2, OUTPUT);
  pinMode(MOT_A_IN3, OUTPUT); pinMode(MOT_A_IN4, OUTPUT);
  pinMode(MOT_B_IN1, OUTPUT); pinMode(MOT_B_IN2, OUTPUT);
  pinMode(MOT_B_IN3, OUTPUT); pinMode(MOT_B_IN4, OUTPUT);
  
  stopMotors(); // <--- Make sure it says stopMotors() here!

  // 2. Init Native I2S Audio
  Serial.println("-> Starting I2S Audio Init...");
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = 44100, 
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 64,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_BCLK,
      .ws_io_num = I2S_LRCK,
      .data_out_num = I2S_DIN,
      .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("-> I2S Audio Init SUCCESS!");

  // 3. Start AP Mode (192.168.5.1)
  Serial.println("-> Starting Wi-Fi AP...");
  WiFi.mode(WIFI_AP_STA);
  IPAddress local_ip(192, 168, 5, 1);
  IPAddress gateway(192, 168, 5, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(host_ap_ssid, host_ap_pass);
  Serial.println("-> Wi-Fi AP SUCCESS!");

  // 4. Start Web Server
  Serial.println("-> Starting Web Server...");
  server.on("/", []() { server.send(200, "text/html", rc_html); });
  server.on("/cmd", handleCommand);
  server.begin();
  Serial.println("-> Web Server SUCCESS!");

  // 5. Start STA Mode
  Serial.println("-> Starting Wi-Fi STA (Connecting to Robot)...");
  WiFi.begin(target_ssid, target_pass);
  Serial.println("-> Setup Complete! Entering Main Loop.");
}

void loop() {
  server.handleClient();
  checkRemoteStatus();
}
