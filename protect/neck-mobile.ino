/****************************************************
 * GO TIME NECKLACE – ESP32-C3 (Rear Radar Version)
 * - Rear-facing radar on back of leather necklace
 * - IMU for your movement
 * - Touch sensor for modes + phone ring
 * - Single vibration motor
 * - ESP-NOW for van alerts
 * - BLE hook for FIND_PHONE
 ****************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// =====================
// PIN MAP
// =====================
#define PIN_VIBE        5   // vibration motor via transistor
#define PIN_TOUCH       6   // touch sensor
#define PIN_RADAR       7   // rear-facing radar input (ADC or digital)
#define PIN_IMU_SCL     8   // I2C SCL (IMU)
#define PIN_IMU_SDA     9   // I2C SDA (IMU)

// =====================
// MODES
// =====================
enum Mode {
  MODE_PERSONAL_AWARE = 0,  // people near + dangerous approach
  MODE_VAN_LINKED     = 1,  // personal + van tamper
  MODE_STEALTH        = 2,  // only true danger
  MODE_AUTO           = 3   // auto walking/skating sensitivity
};

Mode currentMode = MODE_PERSONAL_AWARE;

// =====================
// STATE STRUCTS
// =====================
struct RadarState {
  float dopplerHz;
  float strength;
  float distanceM;
};

struct IMUState {
  float forwardAccel;
  float stepFrequency;
};

struct VanAlert {
  uint8_t type; // 0=none,1=door,2=tilt,3=motion,4=radar
};

RadarState radar;
IMUState imu;
VanAlert vanAlert;

float userSpeedMps = 0.0;
unsigned long lastSpeedUpdateMs = 0;

// =====================
// ESP-NOW (Van Link)
// =====================
uint8_t vanPeerMac[6] = {0x24,0x6F,0x28,0xAA,0xBB,0xCC}; // CHANGE to your van ESP MAC

typedef struct {
  uint8_t alertType; // 1=door,2=tilt,3=motion,4=radar
} VanPacket;

void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len == sizeof(VanPacket)) {
    VanPacket pkt;
    memcpy(&pkt, data, sizeof(VanPacket));
    vanAlert.type = pkt.alertType;
  }
}

// =====================
// BLE (Phone Ring Hook)
// =====================
BLEServer* pServer = nullptr;
BLECharacteristic* pChar = nullptr;

#define BLE_SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define BLE_CHARACTERISTIC_UUID "87654321-4321-4321-4321-ba0987654321"

void setupBLE() {
  BLEDevice::init("GoTimeNecklace");
  pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(BLE_SERVICE_UUID);
  pChar = pService->createCharacteristic(
    BLE_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pChar->addDescriptor(new BLE2902());
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void sendFindPhone() {
  if (!pChar) return;
  pChar->setValue("FIND_PHONE");
  pChar->notify();
}

// =====================
// FORWARD DECLARATIONS
// =====================
void setupEspNow();
void readRadar(RadarState &r);
void readIMU(IMUState &i);
void computeUserSpeed();
float classifyObjectType(const RadarState &r); // 0=unknown,1=person,2=vehicle
float estimateObjectSpeed(const RadarState &r);
float computeTOC(float objectSpeed, float userSpeed, float distance);
void applyLogic();
void vibrateMotor(int ms);
void vibratePerson(float toc);
void vibrateVehicle(float toc);
void vibrateUnknown(float toc);
void vibrateDanger();
void vibrateVanAlert(uint8_t type);
void handleTouch();

// =====================
// SETUP
// =====================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_VIBE, OUTPUT);
  pinMode(PIN_TOUCH, INPUT);
  pinMode(PIN_RADAR, INPUT);

  // IMU init (wire up your IMU here)
  // Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL);

  WiFi.mode(WIFI_STA);
  setupEspNow();
  setupBLE();

  vanAlert.type = 0;
  lastSpeedUpdateMs = millis();
}

// =====================
// LOOP
// =====================
void loop() {
  readRadar(radar);
  readIMU(imu);
  computeUserSpeed();
  handleTouch();
  applyLogic();
}

// =====================
// ESP-NOW SETUP
// =====================
void setupEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onEspNowRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, vanPeerMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add van peer");
  }
}

// =====================
// SENSOR STUBS
// =====================
void readRadar(RadarState &r) {
  // TODO: replace with real radar reads
  // Rear-facing radar: distance is behind you.
  r.dopplerHz = 0.0;
  r.strength  = 0.0;
  r.distanceM = 4.0; // assume 4m behind
}

void readIMU(IMUState &i) {
  // TODO: replace with real IMU reads
  i.forwardAccel  = 0.0;
  i.stepFrequency = 0.0;
}

// =====================
// SPEED FUSION
// =====================
void computeUserSpeed() {
  unsigned long now = millis();
  float dt = (now - lastSpeedUpdateMs) / 1000.0;
  if (dt <= 0) dt = 0.01;
  lastSpeedUpdateMs = now;

  bool walking = false;
  bool skating = false;

  if (currentMode == MODE_AUTO || currentMode == MODE_PERSONAL_AWARE || currentMode == MODE_VAN_LINKED) {
    if (imu.stepFrequency > 1.2f) walking = true;
    if (imu.forwardAccel > 0.4f) skating = true;
  }

  if (walking) {
    const float STEP_LENGTH_M = 0.78f;
    userSpeedMps = imu.stepFrequency * STEP_LENGTH_M;
    return;
  }

  if (skating) {
    userSpeedMps += imu.forwardAccel * dt;
    if (userSpeedMps < 0) userSpeedMps = 0;
    return;
  }

  userSpeedMps *= 0.95f;
  if (userSpeedMps < 0.1f) userSpeedMps = 0.0f;
}

// =====================
// OBJECT CLASSIFICATION
// =====================
float classifyObjectType(const RadarState &r) {
  const float DOPPLER_PERSON   = 50.0;
  const float DOPPLER_VEHICLE  = 150.0;
  const float STRENGTH_VEHICLE = 0.7;

  if (r.dopplerHz > DOPPLER_VEHICLE && r.strength > STRENGTH_VEHICLE) return 2.0;
  if (r.dopplerHz > DOPPLER_PERSON) return 1.0;
  return 0.0;
}

float estimateObjectSpeed(const RadarState &r) {
  return r.dopplerHz * 0.02; // placeholder mapping
}

float computeTOC(float objectSpeed, float userSpeed, float distance) {
  float relative = objectSpeed - userSpeed;
  if (relative <= 0.01) return 9999.0;
  return distance / relative;
}

// =====================
// LOGIC + VIBRATION
// =====================
void applyLogic() {
  // Van alerts first (if mode allows)
  if ((currentMode == MODE_VAN_LINKED || currentMode == MODE_AUTO) && vanAlert.type != 0) {
    vibrateVanAlert(vanAlert.type);
    vanAlert.type = 0;
    return;
  }

  // Rear radar-based personal awareness
  if (radar.strength < 0.1) return;

  float objType   = classifyObjectType(radar);
  float objSpeed  = estimateObjectSpeed(radar);
  float toc       = computeTOC(objSpeed, userSpeedMps, radar.distanceM);
  const float TOC_DANGER = 2.0;

  if (currentMode == MODE_STEALTH) {
    if (toc < TOC_DANGER) vibrateDanger();
    return;
  }

  if (toc < TOC_DANGER) {
    vibrateDanger();
    return;
  }

  if (objType == 2.0) {
    vibrateVehicle(toc);
  } else if (objType == 1.0) {
    vibratePerson(toc);
  } else {
    vibrateUnknown(toc);
  }
}

void vibrateMotor(int ms) {
  digitalWrite(PIN_VIBE, HIGH);
  delay(ms);
  digitalWrite(PIN_VIBE, LOW);
  delay(80);
}

void vibratePerson(float toc) {
  vibrateMotor(120);
  vibrateMotor(120);
}

void vibrateVehicle(float toc) {
  vibrateMotor(300);
}

void vibrateUnknown(float toc) {
  vibrateMotor(180);
}

void vibrateDanger() {
  digitalWrite(PIN_VIBE, HIGH);
  delay(500);
  digitalWrite(PIN_VIBE, LOW);
  delay(150);
}

void vibrateVanAlert(uint8_t type) {
  switch (type) {
    case 1: // door
      vibrateMotor(250);
      break;
    case 2: // tilt/tow
      vibrateMotor(200);
      vibrateMotor(200);
      vibrateMotor(200);
      break;
    case 3: // motion inside
      vibrateMotor(150);
      vibrateMotor(150);
      break;
    case 4: // radar near van
      vibrateMotor(180);
      vibrateMotor(180);
      break;
    default:
      break;
  }
}

// =====================
// TOUCH NAVIGATION
// =====================
void handleTouch() {
  static bool lastTouch = false;
  static unsigned long lastTapTime = 0;
  static int tapCount = 0;

  bool touchNow = digitalRead(PIN_TOUCH) == HIGH;
  unsigned long now = millis();

  if (touchNow && !lastTouch) {
    tapCount++;
    lastTapTime = now;
  }

  if (tapCount >= 2 && (now - lastTapTime) < 500) {
    sendFindPhone();
    tapCount = 0;
  }

  if (!touchNow && lastTouch) {
    if ((now - lastTapTime) > 500 && tapCount == 1) {
      int m = (int)currentMode + 1;
      if (m > 3) m = 0;
      currentMode = (Mode)m;
      tapCount = 0;
    }
  }

  if ((now - lastTapTime) > 1000) {
    tapCount = 0;
  }

  lastTouch = touchNow;
}
