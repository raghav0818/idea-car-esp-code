#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ----- WiFi AP Credentials -----
const char* ap_ssid     = "WanderBin-Robot";
const char* ap_password = "wanderbinpass"; // Must be at least 8 characters

WebServer server(80);

// ----- Ultrasonic Sensor Pins -----
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;

// ----- Servo Pin (MG996R) -----
const int SERVO_PIN = 4;
Servo lidServo;

// ----- Lid Control State -----
bool lidAllowOpen = false;        // Set by the web app JSON
bool lidIsOpen = false;           // Current lid position
String lastItemName = "";
String lastReason = "";

const float HAND_DISTANCE_CM = 15.0;           // Trigger distance
const int LID_OPEN_ANGLE = 90;                 // Servo angle when open
const int LID_CLOSED_ANGLE = 0;                // Servo angle when closed
const unsigned long LID_OPEN_DURATION = 8000;  // Keep lid open for 8 seconds

unsigned long lidOpenedAt = 0;                 // Timestamp when lid was opened

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

// ----- Basic Root Endpoint -----
void handleRoot() {
  sendCORSHeaders();
  server.send(200, "text/plain", "Wander-Bin Lid Controller & API Hub is Online.");
}

// ----- Setup -----
void setup() {
  Serial.begin(9600);

  // Initialize ultrasonic sensor pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize servo
  lidServo.attach(SERVO_PIN, 500, 2500);  // MG996R pulse range: 500–2500µs
  lidServo.write(LID_CLOSED_ANGLE);       // Start closed
  Serial.println("🔧 Servo initialized at 0° (closed)");

  // ─── START ACCESS POINT MODE ───
  Serial.println("\nStarting WiFi Access Point...");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  
  delay(500); // Give the AP a moment to spin up
  
  IPAddress myIP = WiFi.softAPIP(); // Default is usually 192.168.4.1

  Serial.println("\n========================================");
  Serial.println("   🗑️ Wander-Bin Hub Started (AP Mode)!");
  Serial.println("========================================");
  Serial.print("   📡 Connect your Mac/Screen to WiFi: ");
  Serial.println(ap_ssid);
  Serial.print("   🔑 Password: ");
  Serial.println(ap_password);
  Serial.println("----------------------------------------");
  Serial.print("   🚀 API Hub IP: http://");
  Serial.println(myIP);
  Serial.println("========================================\n");

  // Register web routes
  server.on("/", handleRoot); // Replaced the HTML UI with a simple text response
  server.on("/lid-control", HTTP_POST, handleLidControl);
  server.on("/lid-control", HTTP_OPTIONS, []() {
    sendCORSHeaders();
    server.send(204);
  });
  server.on("/status", handleStatus);
  
  server.begin();
  Serial.println("✅ API Web server started\n");
}

// ----- Main Loop -----
void loop() {
  server.handleClient();

  // ── Lid control logic ──
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
