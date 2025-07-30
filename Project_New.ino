#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- PIN & WiFi ---
#define ONE_WIRE_BUS 15
#define RXD2 16
#define TXD2 17
#define LED_PIN 13
#define Alarm_PIN 33
#define FAN_PIN 26

#define WIFI_SSID     "iot"
#define WIFI_PASSWORD "1qazxsw2"

// --- MQTT ---
const char* mqttServer = "broker.hivemq.com";
const int mqttPort = 1883;
const char* controlTopic = "LED/Control";
const char* dataTopic = "Test/DATA";
const char* tempTopic = "Temp/node";
const char* blinkTopic = "LED/BlinkNodeRed"; 
const char* tempTargetTopic = "Temp/target";

// --- LINE Notify ---
const char* LINE_TOKEN = "Bearer JB+UmeCeIqrHVYo2wX0+sQIfYERdvItpIjwoqnngzN+6omULnQdkJnQ6x6nvLoXTOVbnsDf0NYnHl6i32YjoCTlH6w1lqa18azc1uQUpyUR3mwi2DxmTdGsitIh3pGp+rKsqxvIwTY1I91rKZn+AsAdB04t89/1O/w1cDnyilFU=";
const String USER_ID = "U23e3ebafa5b2669455fa1a5bd8709d7e";

WiFiClient espClient;
PubSubClient client(espClient);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

String EndChar = String(char(0xFF)) + String(char(0xFF)) + String(char(0xFF));
const int pos_show = 176;
const int pos_hide = 500;

int tempThreshold = -1;
bool ledBlinking = false;
bool ledState = false;
bool manualControl = false;
unsigned long lastBlink = 0;
bool waitingToBlink = false;
int selectedIndex = -1;
float targetTemp = 0;
bool tempHigh = false;
unsigned long lastNotifyTime = 0;
const unsigned long notifyInterval = 60000;

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(200);
  }
  Serial.println("WiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void sendLineMessage(String message) {
  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("api.line.me", 443)) {
    Serial.println("LINE connect failed");
    return;
  }
  String payload = "{\"to\":\"" + USER_ID + "\",\"messages\":[{\"type\":\"text\",\"text\":\"" + message + "\"}]}";
  String headers = String("POST /v2/bot/message/push HTTP/1.1\r\n") +
                   "Host: api.line.me\r\n" +
                   "Authorization: " + LINE_TOKEN + "\r\n" +
                   "Content-Type: application/json\r\n" +
                   "Content-Length: " + payload.length() + "\r\n\r\n";
  client.print(headers);
  client.println(payload);
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }
  Serial.println("LINE message sent: " + message);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();
  if (String(topic) == controlTopic) {
    if (message == "ON") {
      manualControl = true;
      ledBlinking = false;
      ledState = true;
      digitalWrite(LED_PIN, HIGH);
      Serial2.print("Main.t1.txt=\"ON\"" + EndChar);
      Serial2.print("Main.p1.x=" + String(pos_hide) + EndChar);
      Serial2.print("Main.p2.x=" + String(pos_show) + EndChar);
      client.publish(dataTopic, "ON");
    } else if (message == "OFF") {
      manualControl = false;
      ledBlinking = false;
      ledState = false;
      digitalWrite(LED_PIN, LOW);
      Serial2.print("Main.t1.txt=\"OFF\"" + EndChar);
      Serial2.print("Main.p1.x=" + String(pos_show) + EndChar);
      Serial2.print("Main.p2.x=" + String(pos_hide) + EndChar);
      client.publish(dataTopic, "OFF");
    }
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    String clientID = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientID.c_str())) client.subscribe(controlTopic);
    else delay(5000);
  }
}

void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  sensors.begin();
  initWiFi();
  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback);

  pinMode(LED_PIN, OUTPUT);
  pinMode(Alarm_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(Alarm_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);

  Serial2.print("Main.t1.txt=\"OFF\"" + EndChar);
  Serial2.print("Main.p1.x=" + String(pos_show) + EndChar);
  Serial2.print("Main.p2.x=" + String(pos_hide) + EndChar);
  Serial2.print("Main.p4.x=500" + EndChar);
  Serial.println("System ready...");
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  sensors.requestTemperatures();
  float temperatureC = sensors.getTempCByIndex(0);
  if (temperatureC != DEVICE_DISCONNECTED_C) {
    char tempText[7];
    dtostrf(temperatureC, 4, 1, tempText);
    Serial2.print("Main.t0.txt=\"" + String(tempText) + "\"" + EndChar);
    client.publish(tempTopic, tempText);
  }

  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '1') {
      manualControl = true;
      ledBlinking = false;
      ledState = true;
      digitalWrite(LED_PIN, HIGH);
      Serial2.print("Main.t1.txt=\"ON\"" + EndChar);
      Serial2.print("Main.p1.x=" + String(pos_hide) + EndChar);
      Serial2.print("Main.p2.x=" + String(pos_show) + EndChar);
      client.publish(dataTopic, "ON");
    } else if (c == '0') {
      manualControl = false;
      ledBlinking = false;
      ledState = false;
      digitalWrite(LED_PIN, LOW);
      Serial2.print("Main.t1.txt=\"OFF\"" + EndChar);
      Serial2.print("Main.p2.x=" + String(pos_hide) + EndChar);
      Serial2.print("Main.p1.x=" + String(pos_show) + EndChar);
      client.publish(dataTopic, "OFF");
    } else if (c >= 0 && c <= 9) {
      selectedIndex = c;
      targetTemp = 27 + selectedIndex;
      client.publish(tempTargetTopic, String(targetTemp).c_str());
    }

    if (Serial2.available() >= 3) {
      byte b[4];
      b[0] = c;
      for (int i = 1; i < 4; i++) b[i] = Serial2.read();
      int val = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
      if (val >= 1 && val <= 100) {
        tempThreshold = val;
        Serial2.print("Main.t2.txt=\"" + String(val) + "\"" + EndChar);
      }
    }
  }

  if (selectedIndex >= 0) {
    if (temperatureC >= targetTemp) {
      Serial2.print("p3.x=295" + EndChar);
      digitalWrite(FAN_PIN, HIGH);
    } else {
      Serial2.print("p3.x=560" + EndChar);
      digitalWrite(FAN_PIN, LOW);
    }
  }

  if (tempThreshold != -1) {
    if (temperatureC > tempThreshold) {
      ledBlinking = true;

      if (!tempHigh || millis() - lastNotifyTime >= notifyInterval) {
        String msg = "⚠️ อุณหภูมิสูงเกิน: " + String(temperatureC, 1) + "°C (ตั้งไว้: " + String(tempThreshold) + "°C)";
        sendLineMessage(msg);
        lastNotifyTime = millis();
        tempHigh = true;
      }

    } else {
      ledBlinking = false;
      digitalWrite(LED_PIN, manualControl);
      ledState = manualControl;
      Serial2.print("Main.p4.x=500" + EndChar);
      digitalWrite(Alarm_PIN, LOW);

      if (tempHigh) {
        String msg = "✅ อุณหภูมิลดลงเหลือ: " + String(temperatureC, 1) + "°C แล้ว";
        sendLineMessage(msg);
        tempHigh = false;
      }
    }
  }

  if (ledBlinking) {
    unsigned long now = millis();
    if (now - lastBlink >= 500) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(Alarm_PIN, ledState);
      if (ledState) {
        Serial2.print("Main.p4.x=315" + EndChar);
        client.publish(blinkTopic, "OFF");
      } else {
        Serial2.print("Main.p4.x=500" + EndChar);
        client.publish(blinkTopic, "ON");
      }
    }
  }

  delay(100);
}
