#include <WiFi.h>
#include <PubSubClient.h>

// ===============================
// WIFI / MQTT CONFIGURATION
// ===============================

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* MQTT_BROKER = "test.mosquitto.org";
const int MQTT_PORT = 1883;

// Sensor Node -> Gateway
const char* MQTT_TOPIC =
  "ruralwater/VILLAGE_01/telemetry";

// Gateway -> Website
const char* WEBSITE_TOPIC =
  "ruralwater/VILLAGE_01/dashboard";

// ===============================
// GATEWAY PINS
// ===============================

#define PIN_LED_GREEN 26
#define PIN_LED_RED   27
#define PIN_BUZZER    14

// ===============================
// TIMING
// ===============================

const unsigned long SENSOR_TIMEOUT = 15000;

// ===============================
// MQTT
// ===============================

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ===============================
// TELEMETRY STRUCTURE
// ===============================

struct Telemetry {

  String nodeId;

  float flow;
  float totalLiters;
  float pressure;

  int tds;

  String flowStatus;
  String pressureStatus;
  String qualityStatus;

  bool leakageAlert;

  bool valid;
};

Telemetry lastReading;

unsigned long lastSensorData = 0;
unsigned long redAlertUntil = 0;

// ===============================
// SETUP
// ===============================

void setup() {

  Serial.begin(115200);

  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  lastReading.valid = false;
  lastReading.leakageAlert = false;

  Serial.println();
  Serial.println("==========================================");
  Serial.println("        RURAL WATER GATEWAY");
  Serial.println("==========================================");

  Serial.println();
  Serial.println("Connecting to WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected!");

  Serial.print("Gateway IP: ");
  Serial.println(WiFi.localIP());

  // MQTT

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);

  mqttClient.setBufferSize(512);

  mqttClient.setCallback(mqttCallback);

  connectMQTT();
}

// ===============================
// MAIN LOOP
// ===============================

void loop() {

  if (!mqttClient.connected()) {

    digitalWrite(PIN_LED_GREEN, LOW);

    connectMQTT();
  }

  mqttClient.loop();

  updateIndicators();

  delay(10);
}

// ===============================
// MQTT CONNECTION
// ===============================

void connectMQTT() {

  while (!mqttClient.connected()) {

    Serial.println();
    Serial.println("Connecting to MQTT broker...");

    String clientId =
      "villageWaterGateway01_2026";

    if (mqttClient.connect(clientId.c_str())) {

      Serial.println("MQTT Connected!");

      Serial.println();
      Serial.println("Subscribed to SENSOR topic:");
      Serial.println(MQTT_TOPIC);

      mqttClient.subscribe(MQTT_TOPIC);

      Serial.println();
      Serial.println("Gateway ready.");
      Serial.println("------------------------------------------");

    } else {

      Serial.print("MQTT connection failed. State: ");
      Serial.println(mqttClient.state());

      delay(1000);
    }
  }
}

// ===============================
// RECEIVE DATA FROM SENSOR
// ===============================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
) {

  String packet = "";

  for (unsigned int i = 0; i < length; i++) {

    packet += (char)payload[i];
  }

  lastSensorData = millis();

  // =========================================
  // SHOW RAW DATA RECEIVED FROM SENSOR
  // =========================================

  Serial.println();
  Serial.println("==========================================");
  Serial.println("       DATA RECEIVED FROM SENSOR");
  Serial.println("==========================================");

  Serial.print("Topic: ");
  Serial.println(topic);

  Serial.println();
  Serial.println("RAW SENSOR DATA:");
  Serial.println(packet);

  // =========================================
  // CHECK LEAKAGE
  // =========================================

  if (packet.indexOf("WATER_LEAKAGE=ALERT") >= 0) {

    Serial.println();
    Serial.println("!!! WATER LEAKAGE ALERT RECEIVED !!!");

    redAlertUntil = millis() + 5000;

    tone(PIN_BUZZER, 2000, 1000);
  }

  // =========================================
  // PROCESS TELEMETRY
  // =========================================

  receiveTelemetry(packet);
}

// ===============================
// PROCESS SENSOR DATA
// ===============================

void receiveTelemetry(String packet) {

  Telemetry t;

  t.valid = false;
  t.leakageAlert = false;

  int start = 0;

  while (start < packet.length()) {

    int commaIndex =
      packet.indexOf(',', start);

    String field;

    if (commaIndex == -1) {

      field = packet.substring(start);

      start = packet.length();

    } else {

      field =
        packet.substring(start, commaIndex);

      start = commaIndex + 1;
    }

    int equalIndex =
      field.indexOf('=');

    if (equalIndex == -1)
      continue;

    String key =
      field.substring(0, equalIndex);

    String val =
      field.substring(equalIndex + 1);

    // =========================
    // PARSE VALUES
    // =========================

    if (key == "NODE_ID")
      t.nodeId = val;

    else if (key == "FLOW")
      t.flow = val.toFloat();

    else if (key == "TOTAL_LITERS")
      t.totalLiters = val.toFloat();

    else if (key == "PRESSURE")
      t.pressure = val.toFloat();

    else if (key == "TDS")
      t.tds = val.toInt();

    else if (key == "FLOW_STATUS")
      t.flowStatus = val;

    else if (key == "PRESSURE_STATUS")
      t.pressureStatus = val;

    else if (key == "QUALITY_STATUS")
      t.qualityStatus = val;

    else if (key == "WATER_LEAKAGE")
      t.leakageAlert =
        (val == "ALERT");
  }

  t.valid = true;

  lastReading = t;

  // =========================================
  // SHOW PROCESSED DATA
  // =========================================

  Serial.println();
  Serial.println("------------- PROCESSED DATA -------------");

  Serial.print("Node ID       : ");
  Serial.println(t.nodeId);

  Serial.print("Flow          : ");
  Serial.print(t.flow, 1);
  Serial.println(" L/min");

  Serial.print("Total Water   : ");
  Serial.print(t.totalLiters, 1);
  Serial.println(" L");

  Serial.print("Pressure      : ");
  Serial.print(t.pressure, 1);
  Serial.println(" bar");

  Serial.print("TDS           : ");
  Serial.print(t.tds);
  Serial.println(" ppm");

  Serial.print("Flow Status   : ");
  Serial.println(t.flowStatus);

  Serial.print("Pressure      : ");
  Serial.println(t.pressureStatus);

  Serial.print("Water Quality : ");
  Serial.println(t.qualityStatus);

  Serial.print("Leakage       : ");

  if (t.leakageAlert)
    Serial.println("ALERT");
  else
    Serial.println("NORMAL");

  Serial.println("------------------------------------------");

  // Send to website

  sendToWebsite(t);
}

// ===============================
// SEND DATA TO WEBSITE
// ===============================

void sendToWebsite(Telemetry t) {

  if (!mqttClient.connected())
    return;

  // =========================================
  // CREATE JSON
  // =========================================

  String json = "{";

  json += "\"nodeId\":\"";
  json += t.nodeId;
  json += "\",";

  json += "\"flow\":";
  json += String(t.flow, 1);
  json += ",";

  json += "\"totalLiters\":";
  json += String(t.totalLiters, 1);
  json += ",";

  json += "\"pressure\":";
  json += String(t.pressure, 1);
  json += ",";

  json += "\"tds\":";
  json += String(t.tds);
  json += ",";

  json += "\"flowStatus\":\"";
  json += t.flowStatus;
  json += "\",";

  json += "\"pressureStatus\":\"";
  json += t.pressureStatus;
  json += "\",";

  json += "\"qualityStatus\":\"";
  json += t.qualityStatus;
  json += "\",";

  json += "\"leakage\":";

  if (t.leakageAlert)
    json += "1";
  else
    json += "0";

  json += "}";

  // =========================================
  // PUBLISH TO WEBSITE
  // =========================================

  bool sent =
    mqttClient.publish(
      WEBSITE_TOPIC,
      json.c_str()
    );

  // =========================================
  // SHOW EXACT WEBSITE DATA
  // =========================================

  Serial.println();
  Serial.println("==========================================");
  Serial.println("          DATA SENT TO WEBSITE");
  Serial.println("==========================================");

  Serial.print("Topic: ");
  Serial.println(WEBSITE_TOPIC);

  Serial.println();
  Serial.println("JSON:");

  Serial.println(json);

  Serial.println();

  if (sent)
    Serial.println("Status: SUCCESS");
  else
    Serial.println("Status: FAILED");

  Serial.println("==========================================");
}

// ===============================
// GATEWAY INDICATORS
// ===============================

void updateIndicators() {

  // Sensor communication status

  bool sensorConnected =
    (millis() - lastSensorData < SENSOR_TIMEOUT);

  if (sensorConnected)
    digitalWrite(PIN_LED_GREEN, HIGH);
  else
    digitalWrite(PIN_LED_GREEN, LOW);


  // Leakage alert

  if (millis() < redAlertUntil)
    digitalWrite(PIN_LED_RED, HIGH);
  else
    digitalWrite(PIN_LED_RED, LOW);
}
