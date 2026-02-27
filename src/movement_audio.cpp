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
const int MOT_A_IN1 = 7;
const int MOT_A_IN2 = 15;
const int MOT_A_IN3 = 16;
const int MOT_A_IN4 = 17;
const int MOT_B_IN1 = 11;
const int MOT_B_IN2 = 12;
const int MOT_B_IN3 = 13;
const int MOT_B_IN4 = 14;

// Audio Pins (MAX98357A I2S connection)
const int I2S_BCLK = 41;
const int I2S_LRCK = 42;
const int I2S_DIN  = 2;

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
<!DOCTYPE html><html><head><title>WanderBin RC</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{text-align:center;background:#222;color:white;font-family:sans-serif;}
.btn{display:block;width:120px;height:120px;margin:15px auto;background:#4CAF50;color:white;font-size:40px;border:none;border-radius:50%;touch-action:manipulation;user-select:none;}
.btn:active{background:#ff9800;}
.row{display:flex;justify-content:center;}
</style></head><body>
<h2>WanderBin Mover</h2>
<button class="btn" onmousedown="move('forward')" onmouseup="move('stop')" ontouchstart="move('forward')" ontouchend="move('stop')">&#8593;</button>
<div class="row">
<button class="btn" onmousedown="move('left')" onmouseup="move('stop')" ontouchstart="move('left')" ontouchend="move('stop')">&#8592;</button>
<button class="btn" style="background:red;" onmousedown="move('stop')" ontouchstart="move('stop')">&#9724;</button>
<button class="btn" onmousedown="move('right')" onmouseup="move('stop')" ontouchstart="move('right')" ontouchend="move('stop')">&#8594;</button>
</div>
<button class="btn" onmousedown="move('backward')" onmouseup="move('stop')" ontouchstart="move('backward')" ontouchend="move('stop')">&#8595;</button>
<script>
function move(dir){fetch('/'+dir);}
</script></body></html>
)rawliteral";

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
  WiFi.softAP(host_ap_ssid, host_ap_pass);
  Serial.print("📡 Mover AP Started: ");
  Serial.println(host_ap_ssid);
  Serial.print("🔗 Connect to 192.168.4.1 to Drive.");

  // Register Web Routes
  server.on("/", [](){ server.send(200, "text/html", rc_html); });
  server.on("/forward",  [](){ Serial.println("🚗 FWD"); moveForward(); server.send(204); });
  server.on("/backward", [](){ Serial.println("🚗 BWD"); moveBackward(); server.send(204); });
  server.on("/left",     [](){ Serial.println("🚗 LEFT"); turnLeft(); server.send(204); });
  server.on("/right",    [](){ Serial.println("🚗 RIGHT"); turnRight(); server.send(204); });
  server.on("/stop",     [](){ Serial.println("🚗 STOP"); stopMotors(); server.send(204); });
  
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
