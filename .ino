/****************************************************
 *  GO TIME NECKLACE – ESP32-C3 (Async + Secure SPIFFS)
 *  Rear-approach notifier for walking & skating
 *  - 1 microwave radar sensor
 *  - 1 IMU
 *  - 1 vibration motor
 *  - 1 touch sensor (mode navigation)
 *  - Phone-enhanced via Wi-Fi API
 *  - Go Time themed web UI + secure /upload
 ****************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

// ====== USER CONFIG ======
const char* AP_SSID     = "GoTimeNecklace";
const char* AP_PASSWORD = "boardwalk";

// Secure uploader credentials
const char* UPLOAD_USER = "admin";
const char* UPLOAD_PASS = "gotime123";

// Pins (adjust to your board)
#define PIN_VIBE      5    // vibration motor
#define PIN_TOUCH     6    // touch sensor
#define PIN_RADAR     7    // radar input (analog or digital)
#define PIN_IMU_SCL   8    // I2C SCL
#define PIN_IMU_SDA   9    // I2C SDA

// ====== SERVER ======
AsyncWebServer server(80);

// ====== MODES ======
enum Mode {
  MODE_WALK = 0,
  MODE_SKATE = 1,
  MODE_AUTO = 2
};

Mode currentMode = MODE_AUTO;

// ====== STATE STRUCTS ======
struct RadarState {
  float dopplerHz;     // estimated doppler frequency
  float strength;      // signal strength
  float distanceM;     // rough distance estimate
};

struct IMUState {
  float forwardAccel;  // m/s^2
  float stepFrequency; // steps per second
};

struct PhoneState {
  bool  connected;
  bool  hasSpeed;
  float speedMps;      // phone-reported speed
};

RadarState radar;
IMUState imu;
PhoneState phone;

// User speed (fused)
float userSpeedMps = 0.0;

// Time tracking
unsigned long lastSpeedUpdateMs = 0;

// ====== FORWARD DECLARATIONS ======
void setupWiFiAP();
void setupWebServer();

void readRadar(RadarState &r);
void readIMU(IMUState &i);

void computeUserSpeed();
float classifyObjectType(const RadarState &r); // 0=unknown,1=person,2=vehicle
float estimateObjectSpeed(const RadarState &r);
float computeTOC(float objectSpeed, float userSpeed, float distance);

void applyLogic();
void vibratePatternPerson(float toc);
void vibratePatternVehicle(float toc);
void vibratePatternUnknown(float toc);
void vibrateDanger();

void handleTouch();

// ====== INLINE HTML (INDEX) ======
const char HTML_INDEX[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Go Time Necklace</title>
<style>
body{margin:0;padding:16px;background:#020617;color:#e5e7eb;font-family:sans-serif;}
h1{color:#38bdf8;margin-bottom:4px;}
h2{color:#facc15;margin-top:0;}
button{padding:8px 12px;margin:4px;border-radius:6px;border:none;background:#facc15;color:#111827;font-weight:600;}
pre{background:#020617;border:1px solid #1f2937;padding:8px;border-radius:6px;font-size:12px;}
.logo{font-weight:800;color:#f97316;font-size:18px;}
.mode-label{margin-top:8px;font-size:14px;}
</style>
</head>
<body>
<div class="logo">BOARDWALK CLAY · GO TIME</div>
<h1>Rear Approach Necklace</h1>
<h2>Modes</h2>
<button onclick="setMode(0)">Walking</button>
<button onclick="setMode(1)">Skating</button>
<button onclick="setMode(2)">Auto</button>
<div class="mode-label" id="modeLabel"></div>
<h2>State</h2>
<pre id="stateBox">Loading...</pre>
<script>
function fetchState(){
  fetch('/state').then(r=>r.json()).then(j=>{
    document.getElementById('stateBox').innerText = JSON.stringify(j,null,2);
    let m = j.mode;
    let label = 'Unknown';
    if(m===0) label='Walking Mode';
    if(m===1) label='Skating Mode';
    if(m===2) label='Auto Mode';
    document.getElementById('modeLabel').innerText = label;
  });
}
function setMode(m){
  fetch('/mode',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'mode='+m})
    .then(fetchState);
}
setInterval(fetchState,1000);
fetchState();
</script>
</body>
</html>
)HTML";

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_VIBE, OUTPUT);
  pinMode(PIN_TOUCH, INPUT);
  pinMode(PIN_RADAR, INPUT);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  // Create upload.html in SPIFFS (once per boot)
  File uploadPage = SPIFFS.open("/upload.html", FILE_WRITE);
  uploadPage.print(
    "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>Go Time Upload</title>"
    "<style>body{background:#020617;color:#e5e7eb;font-family:sans-serif;padding:16px;}h1{color:#38bdf8;}input,button{margin:8px 0;padding:8px;border-radius:6px;border:none;}button{background:#facc15;color:#111827;font-weight:600;}</style>"
    "</head><body>"
    "<h1>Go Time Necklace · File Upload</h1>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'>"
    "<input type='file' name='data'><br>"
    "<button type='submit'>Upload</button>"
    "</form>"
    "<p>Upload index.html, style.css, app.js, etc.</p>"
    "</body></html>"
  );
  uploadPage.close();

  setupWiFiAP();
  setupWebServer();

  phone.connected = false;
  phone.hasSpeed = false;
  phone.speedMps = 0.0;

  lastSpeedUpdateMs = millis();
}

// ====== LOOP ======
void loop() {
  readRadar(radar);
  readIMU(imu);
  computeUserSpeed();
  handleTouch();
  applyLogic();
}

// ====== WIFI + WEB ======
void setupWiFiAP() {
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void setupWebServer() {
  // Root UI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", HTML_INDEX);
  });

  // State JSON
  server.on("/state", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"mode\":" + String((int)currentMode) + ",";
    json += "\"userSpeedMps\":" + String(userSpeedMps,2) + ",";
    json += "\"radarDopplerHz\":" + String(radar.dopplerHz,2) + ",";
    json += "\"radarStrength\":" + String(radar.strength,2) + ",";
    json += "\"radarDistanceM\":" + String(radar.distanceM,2) + ",";
    json += "\"phoneConnected\":" + String(phone.connected ? "true":"false") + ",";
    json += "\"phoneSpeedMps\":" + String(phone.speedMps,2) + ",";
    json += "\"phoneHasSpeed\":" + String(phone.hasSpeed ? "true":"false");
    json += "}";
    request->send(200, "application/json", json);
  });

  // Phone speed update
  server.on("/phone", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("speed", true)) {
      AsyncWebParameter* p = request->getParam("speed", true);
      phone.speedMps = p->value().toFloat();
      phone.hasSpeed = true;
      phone.connected = true;
    }
    request->send(200, "text/plain", "PHONE UPDATE OK");
  });

  // Mode change
  server.on("/mode", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("mode", true)) {
      AsyncWebParameter* p = request->getParam("mode", true);
      int m = p->value().toInt();
      if (m >= 0 && m <= 2) currentMode = (Mode)m;
    }
    request->send(200, "text/plain", "MODE OK");
  });

  // Secure SPIFFS uploader
  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->authenticate(UPLOAD_USER, UPLOAD_PASS))
      return request->requestAuthentication();
    request->send(SPIFFS, "/upload.html", "text/html");
  });

  server.on(
    "/upload",
    HTTP_POST,
    [](AsyncWebServerRequest *request){
      if (!request->authenticate(UPLOAD_USER, UPLOAD_PASS))
        return request->requestAuthentication();
      request->send(200, "text/plain", "Upload complete. Refresh the page.");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      if (!request->authenticate(UPLOAD_USER, UPLOAD_PASS))
        return;

      String path = "/" + filename;

      if (index == 0) {
        Serial.printf("UploadStart: %s\n", filename.c_str());
        if (SPIFFS.exists(path)) {
          SPIFFS.remove(path);
        }
      }

      File f = SPIFFS.open(path, FILE_APPEND);
      if (!f) {
        Serial.println("File open failed");
        return;
      }
      f.write(data, len);
      f.close();

      if (final) {
        Serial.printf("UploadEnd: %s (%u bytes)\n", filename.c_str(), index + len);
      }
    }
  );

  server.begin();
  Serial.println("Go Time Necklace server started (Async + secure uploader)");
}

// ====== SENSOR STUBS ======
void readRadar(RadarState &r) {
  // TODO: replace with real radar reads
  r.dopplerHz  = 0.0;
  r.strength   = 0.0;
  r.distanceM  = 5.0; // assume 5m behind for now
}

void readIMU(IMUState &i) {
  // TODO: replace with real IMU reads
  i.forwardAccel  = 0.0;
  i.stepFrequency = 0.0;
}

// ====== SPEED FUSION ======
void computeUserSpeed() {
  unsigned long now = millis();
  float dt = (now - lastSpeedUpdateMs) / 1000.0;
  if (dt <= 0) dt = 0.01;
  lastSpeedUpdateMs = now;

  // 1) Prefer phone speed if available
  if (phone.hasSpeed) {
    userSpeedMps = phone.speedMps;
    return;
  }

  // 2) Walking: use step frequency
  if (currentMode == MODE_WALK || currentMode == MODE_AUTO) {
    if (imu.stepFrequency > 0.1f) {
      const float STEP_LENGTH_M = 0.78f;
      userSpeedMps = imu.stepFrequency * STEP_LENGTH_M;
      return;
    }
  }

  // 3) Skating: integrate forward acceleration
  if (currentMode == MODE_SKATE || currentMode == MODE_AUTO) {
    userSpeedMps += imu.forwardAccel * dt;
    if (userSpeedMps < 0) userSpeedMps = 0;
  }
}

// ====== OBJECT CLASSIFICATION ======
float classifyObjectType(const RadarState &r) {
  // 0 = unknown, 1 = person, 2 = vehicle
  const float DOPPLER_PERSON  = 50.0;   // Hz
  const float DOPPLER_VEHICLE = 150.0;  // Hz
  const float STRENGTH_VEHICLE = 0.7;

  if (r.dopplerHz > DOPPLER_VEHICLE && r.strength > STRENGTH_VEHICLE) return 2.0;
  if (r.dopplerHz > DOPPLER_PERSON) return 1.0;
  return 0.0;
}

float estimateObjectSpeed(const RadarState &r) {
  // Very rough mapping: doppler Hz → m/s
  return r.dopplerHz * 0.02; // placeholder
}

float computeTOC(float objectSpeed, float userSpeed, float distance) {
  float relative = objectSpeed - userSpeed;
  if (relative <= 0.01) return 9999.0; // not closing
  return distance / relative;
}

// ====== LOGIC + VIBRATION ======
void vibrateMotor(int ms) {
  digitalWrite(PIN_VIBE, HIGH);
  delay(ms);
  digitalWrite(PIN_VIBE, LOW);
  delay(80);
}

void vibratePatternPerson(float toc) {
  if (currentMode == MODE_WALK) {
    vibrateMotor(120);
    vibrateMotor(120);
  } else {
    vibrateMotor(80);
    vibrateMotor(80);
    vibrateMotor(80);
  }
}

void vibratePatternVehicle(float toc) {
  if (currentMode == MODE_WALK) {
    vibrateMotor(300);
  } else {
    vibrateMotor(400);
    vibrateMotor(120);
    vibrateMotor(120);
  }
}

void vibratePatternUnknown(float toc) {
  vibrateMotor(180);
}

void vibrateDanger() {
  digitalWrite(PIN_VIBE, HIGH);
  delay(500);
  digitalWrite(PIN_VIBE, LOW);
  delay(150);
}

void applyLogic() {
  if (radar.strength < 0.1) return;

  float objType = classifyObjectType(radar);
  float objSpeed = estimateObjectSpeed(radar);
  float toc = computeTOC(objSpeed, userSpeedMps, radar.distanceM);

  const float TOC_DANGER = 2.0; // seconds

  if (toc < TOC_DANGER) {
    vibrateDanger();
    return;
  }

  if (objType == 2.0) {
    vibratePatternVehicle(toc);
  } else if (objType == 1.0) {
    vibratePatternPerson(toc);
  } else {
    vibratePatternUnknown(toc);
  }
}

// ====== TOUCH NAVIGATION ======
void handleTouch() {
  static bool lastTouch = false;
  bool touchNow = digitalRead(PIN_TOUCH) == HIGH;

  if (touchNow && !lastTouch) {
    int m = (int)currentMode + 1;
    if (m > 2) m = 0;
    currentMode = (Mode)m;
  }

  lastTouch = touchNow;
}
