#include "esp_camera.h"
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <iostream>
#include <sstream>
#include <vector>

// Structure for motor pins
struct MOTOR_PINS {
  int pinEn;   // PWM-enabled pin
  int pinIN1;  // Direction pin 1
  int pinIN2;  // Direction pin 2
};

// Update these to match wiring for L298N/L293D
std::vector<MOTOR_PINS> motorPins = {
  {12, 13, 15},  // RIGHT_MOTOR: En, IN1, IN2
  {12, 14, 2},   // LEFT_MOTOR:  En, IN1, IN2
};

#define LIGHT_PIN 4

// Control constants
#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4
#define STOP 0

#define RIGHT_MOTOR 0
#define LEFT_MOTOR 1

#define FORWARD 1
#define BACKWARD -1

// PWM config for LEDC (3.x API uses ledcAttach(pin,freq,res))
const int PWMFreq = 1000;      // 1 KHz
const int PWMResolution = 8;   // 8-bit 0..255

// Camera pin map (ESP32-CAM AI-Thinker)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// AP credentials
const char* ssid     = "IEEE_CAR"; //you need to define your own custom ssid
const char* password = "12345678"; // you need to define your own custom password

// Server and websockets
AsyncWebServer server(80);
AsyncWebSocket wsCamera("/Camera");
AsyncWebSocket wsCarInput("/CarInput");
uint32_t cameraClientId = 0;

// HTML UI 
const char* htmlHomePage PROGMEM = R"HTMLHOMEPAGE(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <meta charset="utf-8">
    <title>Cam Car</title>
    <style>
      :root {
        --bg: #0f172a;
        --panel: #111827;
        --panel-2: #0b1226;
        --text: #e5e7eb;
        --muted: #9ca3af;
        --accent: #22c55e;
        --accent-strong: #16a34a;
        --shadow: 0 10px 24px rgba(0,0,0,.35);
        --radius: 16px;
        --radius-sm: 12px;
        --space: 16px;
        --space-lg: 22px;
        --space-xl: 28px;
        --ring: 0 0 0 3px color-mix(in srgb, var(--accent), transparent 75%);
      }
      * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
      html, body { height: 100%; }
      body {
        margin: 0;
        background: radial-gradient(1200px 1200px at 10% -10%, #1e293b 0%, var(--bg) 45%) fixed;
        color: var(--text);
        font: 500 16px/1.35 system-ui, -apple-system, Segoe UI, Roboto, Inter, "Helvetica Neue", Arial, "Noto Sans";
      }
      img { display:block; }

      .wrap { max-width: 980px; margin: 0 auto; padding: var(--space-xl) var(--space); display: grid; gap: var(--space-xl); }
      .header { display: flex; align-items: center; justify-content: space-between; gap: var(--space); }
      .title { font-size: 20px; letter-spacing: .3px; color: var(--text); margin: 0; }
      .badge { font-size: 12px; padding: 6px 10px; color: var(--text); background: linear-gradient(180deg, #152039, #0e1730); border: 1px solid #203055; border-radius: 999px; box-shadow: inset 0 1px 0 rgba(255,255,255,.06); }

      .grid { display: grid; grid-template-columns: 1fr; gap: var(--space-xl); }
      @media (min-width: 820px) { .grid { grid-template-columns: 3fr 2fr; } }

      .card { background: linear-gradient(180deg, var(--panel), var(--panel-2)); border: 1px solid rgba(255,255,255,.06); border-radius: var(--radius); box-shadow: var(--shadow); }
      .video-card { padding: var(--space); }
      .video-frame {
        width: 100%;
        aspect-ratio: 4/3;
        background: #0b1220;
        border-radius: var(--radius-sm);
        overflow: hidden;
        display: grid;
        place-items: center;
        border: 1px solid rgba(255,255,255,.05);
      }
      .video-frame img#cameraImage {
        width: 100%;
        height: 100%;
        object-fit: cover;
        background: #0b1220; /* hide broken-image icon area */
      }

      .control-card { display: grid; gap: var(--space-lg); padding: var(--space-lg); }
      .section-title { font-size: 14px; color: var(--muted); margin: 0 0 8px 2px; }
      .pad { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: var(--space); user-select: none; }

      /* Button reset to prevent extra vertical space on mobile */
      .btn {
        -webkit-appearance: none;
        appearance: none;
        margin: 0;
        border: 1px solid #253251;
        background: linear-gradient(180deg, #0f1629, #0c1425);
        color: var(--text);
        width: 96px;       /* fixed square, avoids font-driven growth */
        height: 96px;
        padding: 0;        /* no text padding */
        border-radius: 14px;
        display: inline-grid;
        place-items: center;
        cursor: pointer;
        transition: transform .06s ease, background .15s ease, border-color .15s ease, box-shadow .15s ease, filter .2s ease;
        box-shadow: 0 1px 0 rgba(255,255,255,.05), 0 8px 24px rgba(0,0,0,.35);
        line-height: 1;    /* kill extra line box height */
        font-size: 0;      /* hide any whitespace glyph influence */
        overflow: hidden;  /* clip any gradient overflow */
      }
      /* Green variant for UP only */
      .btn--ok {
        background: linear-gradient(180deg, color-mix(in srgb, var(--accent), #0b141f 30%), #0c1426);
        border-color: color-mix(in srgb, var(--accent), #253251 40%);
      }
      .btn:active { transform: translateY(2px); box-shadow: 0 0 0 rgba(0,0,0,0); filter: brightness(1.05); }
      .pad > .spacer { visibility: hidden; }

      .sliders { display: grid; gap: var(--space); }
      .slider-row { display: grid; gap: 10px; }
      .slider-label { display: flex; align-items: center; justify-content: space-between; gap: 8px; font-size: 14px; color: var(--muted); }
      .slider-value { font-variant-numeric: tabular-nums; color: var(--text); font-size: 14px; opacity: .8; }

      .range {
        -webkit-appearance: none; appearance: none;
        width: 100%; height: 14px;
        background: linear-gradient(180deg, #0f172a, #0b1326);
        border: 1px solid #243153; border-radius: 999px; outline: none;
        transition: filter .15s ease;
      }
      .range:hover { filter: brightness(1.05); }
      .range::-webkit-slider-thumb {
        -webkit-appearance: none; appearance: none;
        width: 28px; height: 28px; border-radius: 50%;
        background: linear-gradient(180deg, var(--accent), var(--accent-strong));
        border: 0; box-shadow: 0 2px 8px rgba(0,0,0,.45); cursor: pointer;
      }
      .range::-moz-range-thumb {
        width: 28px; height: 28px; border: 0; border-radius: 50%;
        background: linear-gradient(180deg, var(--accent), var(--accent-strong));
        box-shadow: 0 2px 8px rgba(0,0,0,.45); cursor: pointer;
      }

      /* Arrow icon via inline SVG ensures no text metrics */
      .icon {
        width: 36px; height: 36px;
        fill: #e5e7eb;
        display: block;
      }
    </style>
  </head>
  <body>
    <div class="wrap">
      <div class="header">
        <h1 class="title">Cam Car</h1>
        <div class="badge" id="connBadge">Connecting…</div>
      </div>

      <div class="grid">
        <section class="card video-card">
          <div class="section-title">Live camera</div>
          <div class="video-frame">
            <img id="cameraImage" src="" alt="camera">
          </div>
        </section>

        <section class="card control-card">
          <div>
            <div class="section-title">Movement</div>
            <div class="pad">
              <div class="spacer"></div>

              <div class="btn btn--ok"
                   ontouchstart='sendButtonInput("MoveCar","1")'
                   ontouchend='sendButtonInput("MoveCar","0")'
                   onmousedown='sendButtonInput("MoveCar","1")'
                   onmouseup='sendButtonInput("MoveCar","0")'
                   onmouseleave='sendButtonInput("MoveCar","0")'>
                <svg class="icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 5l7 9H5l7-9z"/></svg>
              </div>

              <div class="spacer"></div>

              <div class="btn"
                   ontouchstart='sendButtonInput("MoveCar","3")'
                   ontouchend='sendButtonInput("MoveCar","0")'
                   onmousedown='sendButtonInput("MoveCar","3")'
                   onmouseup='sendButtonInput("MoveCar","0")'
                   onmouseleave='sendButtonInput("MoveCar","0")'>
                <svg class="icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M9 12l9 7V5l-9 7z"/></svg>
              </div>

              <div class="btn" style="visibility:hidden"></div>

              <div class="btn"
                   ontouchstart='sendButtonInput("MoveCar","4")'
                   ontouchend='sendButtonInput("MoveCar","0")'
                   onmousedown='sendButtonInput("MoveCar","4")'
                   onmouseup='sendButtonInput("MoveCar","0")'
                   onmouseleave='sendButtonInput("MoveCar","0")'>
                <svg class="icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 12L6 5v14l9-7z"/></svg>
              </div>

              <div class="spacer"></div>

              <div class="btn"
                   ontouchstart='sendButtonInput("MoveCar","2")'
                   ontouchend='sendButtonInput("MoveCar","0")'
                   onmousedown='sendButtonInput("MoveCar","2")'
                   onmouseup='sendButtonInput("MoveCar","0")'
                   onmouseleave='sendButtonInput("MoveCar","0")'>
                <svg class="icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 19l-7-9h14l-7 9z"/></svg>
              </div>

              <div class="spacer"></div>
            </div>
          </div>

          <div class="sliders">
            <div class="slider-row">
              <div class="slider-label">
                <span>Light</span>
                <span class="slider-value" id="LightVal">0</span>
              </div>
              <input class="range" type="range" min="0" max="255" value="0" id="Light" oninput='updateLight(this.value)'>
            </div>
          </div>
        </section>
      </div>

      <div class="footer" style="text-align:center;color:var(--muted);font-size:12px;opacity:.85;padding-bottom:8px">
        Wi‑Fi control & WebSocket streaming
      </div>
    </div>

    <script>
      var webSocketCameraUrl = "ws:\/\/" + window.location.hostname + "/Camera";
      var webSocketCarInputUrl = "ws:\/\/" + window.location.hostname + "/CarInput";
      var websocketCamera;
      var websocketCarInput;

      // Robust camera blob rendering with revoke and race-guard
      let lastBlobUrl = null;
      let frameSeq = 0;

      function initCameraWebSocket() {
        websocketCamera = new WebSocket(webSocketCameraUrl);
        websocketCamera.binaryType = 'blob';
        websocketCamera.onopen = function(){ document.getElementById("connBadge").textContent = "Connected UI"; };
        websocketCamera.onclose = function(){ document.getElementById("connBadge").textContent = "Reconnecting…"; setTimeout(initCameraWebSocket, 1500); };
        websocketCamera.onmessage = function(event){
          const img = document.getElementById("cameraImage");
          const mySeq = ++frameSeq;
          const url = URL.createObjectURL(event.data);
          img.onload = function(){
            // Only revoke the URL we actually displayed; avoid revoking a newer one
            if (lastBlobUrl && lastBlobUrl !== url) {
              URL.revokeObjectURL(lastBlobUrl);
            }
            lastBlobUrl = url;
          };
          // If decoding fails, revoke and keep last good frame
          img.onerror = function(){
            URL.revokeObjectURL(url);
          };
          // Swap source; mobile-friendly
          img.src = url;
        };
      }

      function initCarInputWebSocket() {
        websocketCarInput = new WebSocket(webSocketCarInputUrl);
        websocketCarInput.onopen = function() {
          var light = document.getElementById("Light").value;
          sendButtonInput("Light", light);
          document.getElementById("LightVal").textContent = light;
        };
        websocketCarInput.onclose = function(){ setTimeout(initCarInputWebSocket, 1500); };
        websocketCarInput.onmessage = function(event){};
      }

      function initWebSocket() {
        initCameraWebSocket();
        initCarInputWebSocket();
      }

      function sendButtonInput(key, value) {
        if (websocketCarInput && websocketCarInput.readyState === 1) {
          websocketCarInput.send(key + "," + value);
        }
      }

      function updateLight(val){
        document.getElementById("LightVal").textContent = val;
        sendButtonInput("Light", val);
      }

      window.onload = function(){
        initWebSocket();
        document.body.addEventListener("touchend", function(e){}, {passive:true});
      };
      window.onbeforeunload = function(){
        if (lastBlobUrl) try { URL.revokeObjectURL(lastBlobUrl); } catch(e) {}
      };
    </script>
  </body>
</html>
)HTMLHOMEPAGE";

// Helper: set motor direction
void rotateMotor(int motorNumber, int motorDirection) {
  if (motorDirection == FORWARD) {
    digitalWrite(motorPins[motorNumber].pinIN1, HIGH);
    digitalWrite(motorPins[motorNumber].pinIN2, LOW);
  } else if (motorDirection == BACKWARD) {
    digitalWrite(motorPins[motorNumber].pinIN1, LOW);
    digitalWrite(motorPins[motorNumber].pinIN2, HIGH);
  } else {
    digitalWrite(motorPins[motorNumber].pinIN1, LOW);
    digitalWrite(motorPins[motorNumber].pinIN2, LOW);
  }
}

// Movement command dispatcher
void moveCar(int inputValue) {
  Serial.printf("Got value as %d\n", inputValue);
  switch (inputValue) {
    case UP:
      rotateMotor(RIGHT_MOTOR, FORWARD);
      rotateMotor(LEFT_MOTOR, FORWARD);
      break;
    case DOWN:
      rotateMotor(RIGHT_MOTOR, BACKWARD);
      rotateMotor(LEFT_MOTOR, BACKWARD);
      break;
    case LEFT:
      rotateMotor(RIGHT_MOTOR, FORWARD);
      rotateMotor(LEFT_MOTOR, BACKWARD);
      break;
    case RIGHT:
      rotateMotor(RIGHT_MOTOR, BACKWARD);
      rotateMotor(LEFT_MOTOR, FORWARD);
      break;
    case STOP:
      rotateMotor(RIGHT_MOTOR, STOP);
      rotateMotor(LEFT_MOTOR, STOP);
      break;
    default:
      rotateMotor(RIGHT_MOTOR, STOP);
      rotateMotor(LEFT_MOTOR, STOP);
      break;
  }
}

// HTTP handlers
void handleRoot(AsyncWebServerRequest *request) {
  request->send_P(200, "text/html", htmlHomePage);
}

void handleNotFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "File Not Found");
}

// WebSocket: Car input (Speed removed)
void onCarInputWebSocketEvent(AsyncWebSocket *server,
                              AsyncWebSocketClient *client,
                              AwsEventType type,
                              void *arg,
                              uint8_t *data,
                              size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      moveCar(0);
      ledcWrite(LIGHT_PIN, 0);
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        std::string myData;
        myData.assign((char *)data, len);
        std::istringstream ss(myData);
        std::string key, value;
        std::getline(ss, key, ',');
        std::getline(ss, value, ',');
        Serial.printf("Key [%s] Value[%s]\n", key.c_str(), value.c_str());
        int valueInt = atoi(value.c_str());

        if (key == "MoveCar") {
          moveCar(valueInt);
        } else if (key == "Light") {
          ledcWrite(LIGHT_PIN, constrain(valueInt, 0, 255));
        }
      }
      break;
    }
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
    default:
      break;
  }
}

// WebSocket: Camera stream client tracking
void onCameraWebSocketEvent(AsyncWebSocket *server,
                            AsyncWebSocketClient *client,
                            AwsEventType type,
                            void *arg,
                            uint8_t *data,
                            size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      cameraClientId = client->id();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      cameraClientId = 0;
      break;
    case WS_EVT_DATA:
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
    default:
      break;
  }
}

// Camera setup
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 10;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  if (psramFound()) {
    heap_caps_malloc_extmem_enable(20000);
    Serial.printf("PSRAM initialized. malloc to take memory from psram above this size\n");
  }
}

// Send one JPEG frame to the current camera client
void sendCameraPicture() {
  if (cameraClientId == 0) return;

  unsigned long startTime1 = millis();

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Frame buffer could not be acquired");
    return;
  }

  unsigned long startTime2 = millis();
  wsCamera.binary(cameraClientId, fb->buf, fb->len);
  esp_camera_fb_return(fb);

  // Wait until queue has room to avoid piling up
  while (true) {
    AsyncWebSocketClient * clientPointer = wsCamera.client(cameraClientId);
    if (!clientPointer || !(clientPointer->queueIsFull())) {
      break;
    }
    delay(1);
  }

  unsigned long startTime3 = millis();
  Serial.printf("Time taken Total: %d|%d|%d\n",
                (int)(startTime3 - startTime1),
                (int)(startTime2 - startTime1),
                (int)(startTime3 - startTime2));
}

// Configure GPIO and LEDC (3.x API)
void setUpPinModes() {
  // Direction pins
  for (size_t i = 0; i < motorPins.size(); i++) {
    pinMode(motorPins[i].pinEn, OUTPUT);
    pinMode(motorPins[i].pinIN1, OUTPUT);
    pinMode(motorPins[i].pinIN2, OUTPUT);

    // Attach PWM to each enable pin (auto-assign channel)
    ledcAttach(motorPins[i].pinEn, PWMFreq, PWMResolution); // 3.x API
  }
  moveCar(STOP);

  // Light pin setup + PWM
  pinMode(LIGHT_PIN, OUTPUT);
  ledcAttach(LIGHT_PIN, PWMFreq, PWMResolution); // 3.x API

  // Set motor enable pins to fixed full duty; light off
  ledcWrite(motorPins[RIGHT_MOTOR].pinEn, 255);
  ledcWrite(motorPins[LEFT_MOTOR].pinEn, 255);
  ledcWrite(LIGHT_PIN, 0);
}

void setup(void) {
  setUpPinModes();
  Serial.begin(115200);

  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.on("/", HTTP_GET, handleRoot);
  server.onNotFound(handleNotFound);

  wsCamera.onEvent(onCameraWebSocketEvent);
  server.addHandler(&wsCamera);

  wsCarInput.onEvent(onCarInputWebSocketEvent);
  server.addHandler(&wsCarInput);

  server.begin();
  Serial.println("HTTP server started");

  setupCamera();
}

void loop() {
  wsCamera.cleanupClients();
  wsCarInput.cleanupClients();
  sendCameraPicture();
  Serial.printf("SPIRam Total heap %d, SPIRam Free Heap %d\n", ESP.getPsramSize(), ESP.getFreePsram());
}
