#include <WiFi.h>
#include <PubSubClient.h>

// ===============================
// DEVICE CONFIGURATION
// ===============================

const char* NODE_ID = "VILLAGE_01";

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* MQTT_BROKER = "test.mosquitto.org";
const int MQTT_PORT = 1883;

const char* MQTT_TOPIC =
  "ruralwater/VILLAGE_01/telemetry";

// ===============================
// PINS
// ===============================

#define PIN_FLOW       34
#define PIN_PRESSURE   35
#define PIN_TDS        32
#define PIN_LEAK       25

#define PIN_LED_GREEN  14
#define PIN_LED_YELLOW 27
#define PIN_LED_RED    26
#define PIN_BUZZER     13

// ===============================
// THRESHOLDS
// ===============================

const float FLOW_LOW_LPM = 3.0;
const float FLOW_NEAR_ZERO_LPM = 1.0;

const float PRESSURE_LOW_BAR = 1.5;
const float PRESSURE_HIGH_BAR = 4.0;

const int TDS_WARNING_PPM = 500;
const int TDS_POOR_PPM = 800;

// ===============================
// TIMING
// ===============================

// SEND DATA EVERY 10 SECONDS
const unsigned long TX_INTERVAL_MS = 10000;

const unsigned long DEBOUNCE_MS = 250;

// ===============================
// MQTT
// ===============================

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ===============================
// VARIABLES
// ===============================

float flowLpm = 0;
float totalLiters = 0;
float pressureBar = 0;
int tdsPpm = 0;

String flowStatus = "NO_FLOW";
String pressureStatus = "NORMAL";
String qualityStatus = "GOOD";

bool leakageAlert = false;

unsigned long lastTxMs = 0;
unsigned long lastButtonChangeMs = 0;

bool lastButtonState = HIGH;

// ===============================
// SETUP
// ===============================

void setup() {

  Serial.begin(115200);

  pinMode(PIN_LEAK, INPUT_PULLUP);

  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.println();
  Serial.println("================================");
  Serial.println("     RURAL WATER SENSOR NODE");
  Serial.println("================================");

  Serial.println("Connecting WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected!");

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);

  connectMQTT();
}

// ===============================
// MAIN LOOP
// ===============================

void loop() {

  if (!mqttClient.connected()) {
    connectMQTT();
  }

  mqttClient.loop();

  // Check leakage continuously
  checkLeakageButton();

  // Send sensor data every 10 seconds
  if (millis() - lastTxMs >= TX_INTERVAL_MS) {

    lastTxMs = millis();

    readFlowSensor();
    readPressureSensor();
    readWaterQuality();

    updateStatus();

    sendTelemetry();

    printDashboard();
  }

  updateLEDs();

  delay(10);
}

// ===============================
// MQTT CONNECTION
// ===============================

void connectMQTT() {

  while (!mqttClient.connected()) {

    Serial.println("Connecting MQTT...");

    String clientId = "villageWaterSensor01_2026";

    if (mqttClient.connect(clientId.c_str())) {

      Serial.println("MQTT Connected!");

    } else {

      Serial.print("MQTT failed. State: ");
      Serial.println(mqttClient.state());

      delay(1000);
    }
  }
}

// ===============================
// FLOW SENSOR
// ===============================

void readFlowSensor() {

  int raw = analogRead(PIN_FLOW);

  flowLpm = map(raw, 0, 4095, 0, 150) / 10.0;

  // Calculate water used during 10 seconds
  totalLiters += flowLpm * (TX_INTERVAL_MS / 60000.0);
}

// ===============================
// PRESSURE SENSOR
// ===============================

void readPressureSensor() {

  int raw = analogRead(PIN_PRESSURE);

  pressureBar = map(raw, 0, 4095, 0, 60) / 10.0;
}

// ===============================
// TDS SENSOR
// ===============================

void readWaterQuality() {

  int raw = analogRead(PIN_TDS);

  tdsPpm = map(raw, 0, 4095, 0, 1000);
}

// ===============================
// STATUS CALCULATION
// ===============================

void updateStatus() {

  // FLOW STATUS

  if (flowLpm <= FLOW_NEAR_ZERO_LPM) {

    flowStatus = "NO_FLOW";

  } else if (flowLpm < FLOW_LOW_LPM) {

    flowStatus = "LOW_FLOW";

  } else {

    flowStatus = "NORMAL";
  }


  // PRESSURE STATUS

  if (pressureBar < PRESSURE_LOW_BAR) {

    pressureStatus = "LOW";

  } else if (pressureBar > PRESSURE_HIGH_BAR) {

    pressureStatus = "HIGH";

  } else {

    pressureStatus = "NORMAL";
  }


  // WATER QUALITY

  if (tdsPpm >= TDS_POOR_PPM) {

    qualityStatus = "POOR";

  } else if (tdsPpm >= TDS_WARNING_PPM) {

    qualityStatus = "WARNING";

  } else {

    qualityStatus = "GOOD";
  }
}

// ===============================
// LEAKAGE BUTTON
// ===============================

void checkLeakageButton() {

  bool currentButtonState = digitalRead(PIN_LEAK);

  // Detect button press

  if (lastButtonState == HIGH &&
      currentButtonState == LOW &&
      millis() - lastButtonChangeMs > DEBOUNCE_MS) {

    lastButtonChangeMs = millis();

    leakageAlert = !leakageAlert;

    Serial.println();
    Serial.println("********************************");

    if (leakageAlert) {

      Serial.println("!!! WATER LEAKAGE ALERT !!!");

      // RED LED ON

      digitalWrite(PIN_LED_RED, HIGH);
      digitalWrite(PIN_LED_GREEN, LOW);
      digitalWrite(PIN_LED_YELLOW, LOW);

      // BUZZER

      tone(PIN_BUZZER, 2000, 1000);

    } else {

      Serial.println("Water Leakage: NORMAL");

      digitalWrite(PIN_LED_RED, LOW);
    }

    Serial.println("********************************");

    // Send leakage status immediately
    sendTelemetry();
  }

  lastButtonState = currentButtonState;
}

// ===============================
// SEND MQTT TELEMETRY
// ===============================

void sendTelemetry() {

  String packet = "";

  packet += "NODE_ID=";
  packet += NODE_ID;

  packet += ",FLOW=";
  packet += String(flowLpm, 1);

  packet += ",TOTAL_LITERS=";
  packet += String(totalLiters, 1);

  packet += ",PRESSURE=";
  packet += String(pressureBar, 1);

  packet += ",TDS=";
  packet += String(tdsPpm);

  packet += ",FLOW_STATUS=";
  packet += flowStatus;

  packet += ",PRESSURE_STATUS=";
  packet += pressureStatus;

  packet += ",QUALITY_STATUS=";
  packet += qualityStatus;

  packet += ",WATER_LEAKAGE=";

  if (leakageAlert) {
    packet += "ALERT";
  } else {
    packet += "NORMAL";
  }

  mqttClient.publish(MQTT_TOPIC, packet.c_str());

  Serial.println();
  Serial.println("MQTT SENT:");
  Serial.println(packet);
}

// ===============================
// LED STATUS
// ===============================

void updateLEDs() {

  // =========================
  // LEAKAGE = HIGHEST PRIORITY
  // =========================

  if (leakageAlert) {

    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);

    return;
  }


  // =========================
  // CRITICAL CONDITION
  // =========================

  if (flowStatus == "NO_FLOW" ||
      qualityStatus == "POOR") {

    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);

    return;
  }


  // =========================
  // WARNING CONDITION
  // =========================

  if (flowStatus == "LOW_FLOW" ||
      pressureStatus == "LOW" ||
      pressureStatus == "HIGH" ||
      qualityStatus == "WARNING") {

    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_YELLOW, HIGH);

    return;
  }


  // =========================
  // NORMAL
  // =========================

  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_LED_GREEN, HIGH);
}

// ===============================
// SERIAL DASHBOARD
// ===============================

void printDashboard() {

  Serial.println();
  Serial.println("------------- SENSOR DATA -------------");

  Serial.print("Flow       : ");
  Serial.print(flowLpm);
  Serial.println(" L/min");

  Serial.print("Total      : ");
  Serial.print(totalLiters);
  Serial.println(" L");

  Serial.print("Pressure   : ");
  Serial.print(pressureBar);
  Serial.println(" bar");

  Serial.print("TDS        : ");
  Serial.print(tdsPpm);
  Serial.println(" ppm");

  Serial.print("Flow Status: ");
  Serial.println(flowStatus);

  Serial.print("Pressure   : ");
  Serial.println(pressureStatus);

  Serial.print("Quality    : ");
  Serial.println(qualityStatus);

  Serial.print("Leakage    : ");

  if (leakageAlert) {
    Serial.println("ALERT");
  } else {
    Serial.println("NORMAL");
  }

  Serial.println("---------------------------------------");
}
