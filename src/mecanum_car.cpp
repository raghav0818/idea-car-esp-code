#include <Arduino.h>

// Omnidirectional 4WD Mecanum RC Car
// ESP32-S3-WROOM + 2x L298N Motor Drivers
// WiFi AP mode with web-based controller

#include <WiFi.h>
#include <WebServer.h>

// ----- WiFi Credentials -----
const char* ssid     = "MecanumCar";
const char* password = "12345678";

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
// Mecanum wheel kinematics (corrected for axis swap):
//   FL  FR
//   BL  BR
//
// The physical car has its fwd/back and left/right axes swapped
// relative to the motor labels, so the motor patterns are remapped:
//
// Forward:      FL fwd,  FR back, BL back, BR fwd
// Backward:     FL back, FR fwd,  BL fwd,  BR back
// Strafe left:  all forward
// Strafe right: all backward
// Rotate left:  FL fwd,  FR back, BL fwd,  BR back
// Rotate right: FL back, FR fwd,  BL back, BR fwd
// Diag FL:      FL fwd,  FR stop, BL stop, BR fwd
// Diag FR:      FL stop, FR back, BL back, BR stop
// Diag BL:      FL stop, FR fwd,  BL fwd,  BR stop
// Diag BR:      FL back, FR stop, BL stop, BR back

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

// ----- Command Handler -----
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
  Serial.begin(115200);

  // Initialize all motor pins as outputs
  for (int i = 0; i < NUM_MOTOR_PINS; i++) {
    pinMode(motorPins[i], OUTPUT);
    digitalWrite(motorPins[i], LOW);
  }

  // Start WiFi Access Point
  WiFi.softAP(ssid, password);
  Serial.println("AP started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  // Register web routes
  server.on("/", handleRoot);
  server.on("/cmd", handleCommand);
  server.begin();
  Serial.println("Server started");
}

// ----- Main Loop -----
void loop() {
  server.handleClient();
}
