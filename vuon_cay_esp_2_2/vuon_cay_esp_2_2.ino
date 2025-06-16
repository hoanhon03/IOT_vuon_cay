// Kích hoạt Blynk.Edgent
#define BLYNK_TEMPLATE_ID "TMPL6i-bkM51m"
#define BLYNK_TEMPLATE_NAME "Vuon"
#define BLYNK_AUTH_TOKEN "dzE4BlA-E5Dh9vHP5lKWAJARYz8N6EVP"

#define BLYNK_PRINT Serial
// #define BLYNK_DEBUG

#include "BlynkEdgent.h"
#include <HardwareSerial.h>
#include <WiFi.h> // Cần thiết cho WiFiClient dùng bởi PubSubClient
#include <PubSubClient.h> // <<<--- THÊM MỚI: Thư viện MQTT

// --- Cấu hình MQTT Broker ---
const char* mqtt_server = "test.mosquitto.org"; // Hoặc broker.hivemq.com, hoặc IP broker của bạn
const int mqtt_port = 1883; // Port MQTT mặc định
// const char* mqtt_user = "your_mqtt_username"; // Nếu broker yêu cầu
// const char* mqtt_password = "your_mqtt_password"; // Nếu broker yêu cầu
const char* mqtt_client_id_base = "SmartGardenESP32_"; // ID client MQTT, nên là duy nhất

// --- Định nghĩa các MQTT Topic ---
const char* topic_temperature = "smartgarden/esp32/temperature";
const char* topic_moisture_avg = "smartgarden/esp32/moisture_avg";
const char* topic_moisture_1 = "smartgarden/esp32/moisture/1";
const char* topic_moisture_2 = "smartgarden/esp32/moisture/2";
const char* topic_moisture_3 = "smartgarden/esp32/moisture/3";
const char* topic_moisture_4 = "smartgarden/esp32/moisture/4";
const char* topic_light = "smartgarden/esp32/light";
const char* topic_motor_status = "smartgarden/esp32/status/motor";
const char* topic_fan_status = "smartgarden/esp32/status/fan";
const char* topic_light_status = "smartgarden/esp32/status/light_device"; // Đổi tên để tránh trùng topic_light
const char* topic_auto_mode = "smartgarden/esp32/status/automode";
const char* topic_last_arduino_response = "smartgarden/esp32/arduino_response";

// --- Khởi tạo MQTT Client ---
WiFiClient espClient; // Đối tượng WiFi client
PubSubClient mqttClient(espClient); // Đối tượng MQTT client

// --- Giao tiếp Serial với Arduino ---
#define ARDUINO_RX_PIN 16
#define ARDUINO_TX_PIN 17
HardwareSerial& arduinoSerial = Serial2;

// --- Biến lưu trữ (Giữ nguyên) ---
float currentTemp = -99.0;
float currentMoisture1 = -1.0; /* ... các biến khác giữ nguyên ... */
float currentMoisture2 = -1.0;
float currentMoisture3 = -1.0;
float currentMoisture4 = -1.0;
float currentMoistureAvg = -1.0;
int currentLight = -1;
bool motorStatus = false;
bool fanStatus = false;
bool lightStatus = false; // Trạng thái của thiết bị đèn
bool arduinoAutoMode = true;
String lastArduinoResponse = "";
float currentSetL = 30.0;
float currentSetH = 70.0;

BlynkTimer timer;

// --- Định nghĩa Virtual Pin (Giữ nguyên) ---
#define VPIN_TEMP V0 /* ... các VPIN khác giữ nguyên ... */
#define VPIN_MOISTURE V1
#define VPIN_LIGHT V2
#define VPIN_MOTOR_STATUS V3
#define VPIN_FAN_STATUS V4
#define VPIN_LIGHT_STATUS V5
#define VPIN_MOTOR_CONTROL V6
#define VPIN_FAN_CONTROL V7
#define VPIN_LIGHT_CONTROL V8
#define VPIN_AUTO_MODE V9
#define VPIN_SET_L V10
#define VPIN_SET_H V11
#define VPIN_LAST_RESPONSE V12
#define VPIN_MOISTURE_1     V13
#define VPIN_MOISTURE_2     V14
#define VPIN_MOISTURE_3     V15
#define VPIN_MOISTURE_4     V16
#define VPIN_ARDUINO_AUTO_STATUS V17

// --- Hàm gửi lệnh và đọc phản hồi (Giữ nguyên) ---
String sendArduinoCommand(String cmd, unsigned long timeout = 1000) { /* ... giữ nguyên ... */
  Serial.print("Gửi đến Arduino: "); Serial.println(cmd);
  arduinoSerial.println(cmd);
  unsigned long startTime = millis(); String response = "";
  while (millis() - startTime < timeout) {
    if (arduinoSerial.available()) {
      response = arduinoSerial.readStringUntil('\n'); response.trim();
      Serial.print("Phản hồi từ Arduino: "); Serial.println(response);
      lastArduinoResponse = response;
      if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, lastArduinoResponse);
      if (mqttClient.connected()) mqttClient.publish(topic_last_arduino_response, lastArduinoResponse.c_str(), true); // Publish cả phản hồi Arduino
      return response;
    }
  }
  Serial.println("Không có phản hồi từ Arduino (Hết thời gian chờ)");
  lastArduinoResponse = "Timeout";
  if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, lastArduinoResponse);
  if (mqttClient.connected()) mqttClient.publish(topic_last_arduino_response, lastArduinoResponse.c_str(), true);
  return "";
}

// --- Hàm đọc và phân tích dữ liệu từ Arduino (Giữ nguyên) ---
void parseArduinoData(String data) { /* ... giữ nguyên ... */
   if (data.length() == 0) return;
   auto getValue = [&](int key_idx, int key_len) -> String {
       if (key_idx == -1) return ""; int val_start = key_idx + key_len;
       int val_end = data.indexOf(',', val_start);
       if (val_end == -1) { return data.substring(val_start); }
       return data.substring(val_start, val_end);
   };
   int t_idx = data.indexOf("T:"); int m1_idx = data.indexOf("M1:"); int m2_idx = data.indexOf("M2:");
   int m3_idx = data.indexOf("M3:"); int m4_idx = data.indexOf("M4:");
   int mavg_idx = data.indexOf("MAVG:"); int l_idx = data.indexOf("L:");
   int mot_idx = data.indexOf("MOT:"); int fan_idx = data.indexOf("FAN:");
   int lit_idx = data.indexOf("LIT:"); int auto_idx = data.indexOf("AUTO:");
   if (t_idx != -1) currentTemp = getValue(t_idx, 2).toFloat();
   if (m1_idx != -1) currentMoisture1 = getValue(m1_idx, 3).toFloat();
   if (m2_idx != -1) currentMoisture2 = getValue(m2_idx, 3).toFloat();
   if (m3_idx != -1) currentMoisture3 = getValue(m3_idx, 3).toFloat();
   if (m4_idx != -1) currentMoisture4 = getValue(m4_idx, 3).toFloat();
   if (mavg_idx != -1) currentMoistureAvg = getValue(mavg_idx, 5).toFloat();
   if (l_idx != -1) currentLight = getValue(l_idx, 2).toInt();
   if (mot_idx != -1) motorStatus = (getValue(mot_idx, 4).toInt() == 1);
   if (fan_idx != -1) fanStatus = (getValue(fan_idx, 4).toInt() == 1);
   if (lit_idx != -1) lightStatus = (getValue(lit_idx, 4).toInt() == 1);
   if (auto_idx != -1) arduinoAutoMode = (getValue(auto_idx, 5).toInt() == 1);
   Serial.printf("Parsed: T=%.1f, MAvg=%.0f (M1=%.0f,M2=%.0f,M3=%.0f,M4=%.0f), L=%d, Mot=%d, Fan=%d, Lit=%d, Auto=%d\n",
                  currentTemp, currentMoistureAvg, currentMoisture1, currentMoisture2, currentMoisture3, currentMoisture4,
                  currentLight, motorStatus, fanStatus, lightStatus, arduinoAutoMode);
}


// --- THÊM MỚI: Hàm kết nối MQTT ---
void reconnectMQTT() {
  // Lặp cho đến khi kết nối được
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Tạo client ID ngẫu nhiên hoặc dựa trên MAC address để tránh trùng lặp
    String clientId = mqtt_client_id_base;
    clientId += String(random(0xffff), HEX); // Thêm phần ngẫu nhiên
    // clientId += WiFi.macAddress(); clientId.replace(":", ""); // Hoặc dùng MAC address

    // Cố gắng kết nối
    // if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password)) { // Nếu có user/pass
    if (mqttClient.connect(clientId.c_str())) { // Không có user/pass
      Serial.println("connected to MQTT broker");
      // Đăng ký (subscribe) vào các topic nếu cần nhận lệnh từ MQTT
      // Ví dụ: mqttClient.subscribe("smartgarden/esp32/command/motor");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Chờ 5 giây trước khi thử lại
      delay(5000);
    }
  }
}

// --- THÊM MỚI: Hàm publish dữ liệu lên MQTT ---
void publishSensorDataToMQTT() {
  if (!mqttClient.connected()) {
    reconnectMQTT(); // Cố gắng kết nối lại nếu mất kết nối
    if(!mqttClient.connected()) return; // Nếu vẫn không kết nối được thì thoát
  }

  char msgBuffer[10]; // Buffer để chuyển số thành chuỗi

  // Publish nhiệt độ
  dtostrf(currentTemp, 4, 1, msgBuffer); // Chuyển float thành chuỗi (width 4, 1 decimal)
  mqttClient.publish(topic_temperature, msgBuffer, true); // true = retained message

  // Publish độ ẩm trung bình
  dtostrf(currentMoistureAvg, 4, 0, msgBuffer);
  mqttClient.publish(topic_moisture_avg, msgBuffer, true);

  // Publish các độ ẩm riêng lẻ (tùy chọn)
  dtostrf(currentMoisture1, 4, 0, msgBuffer); mqttClient.publish(topic_moisture_1, msgBuffer, true);
  dtostrf(currentMoisture2, 4, 0, msgBuffer); mqttClient.publish(topic_moisture_2, msgBuffer, true);
  dtostrf(currentMoisture3, 4, 0, msgBuffer); mqttClient.publish(topic_moisture_3, msgBuffer, true);
  dtostrf(currentMoisture4, 4, 0, msgBuffer); mqttClient.publish(topic_moisture_4, msgBuffer, true);

  // Publish ánh sáng
  itoa(currentLight, msgBuffer, 10); // Chuyển int thành chuỗi (hệ 10)
  mqttClient.publish(topic_light, msgBuffer, true);

  // Publish trạng thái thiết bị
  mqttClient.publish(topic_motor_status, motorStatus ? "ON" : "OFF", true);
  mqttClient.publish(topic_fan_status, fanStatus ? "ON" : "OFF", true);
  mqttClient.publish(topic_light_status, lightStatus ? "ON" : "OFF", true);
  mqttClient.publish(topic_auto_mode, arduinoAutoMode ? "AUTO" : "MANUAL", true);

  Serial.println("Data published to MQTT topics.");
}


// --- Cập nhật hàm sendSensorDataToBlynk để gọi cả publish MQTT ---
void processAndSendData() {
  if (!Blynk.connected() && !mqttClient.connected()) { // Nếu cả Blynk và MQTT đều không kết nối
      Serial.println("Neither Blynk nor MQTT is connected. Skipping data processing.");
      return;
  }

  String response = sendArduinoCommand("GET_DATA");
  if (response.length() > 0 && response.indexOf("T:") != -1) {
      parseArduinoData(response);

      // Gửi lên Blynk (nếu kết nối)
      if (Blynk.connected()) {
          Blynk.virtualWrite(VPIN_TEMP, currentTemp);
          Blynk.virtualWrite(VPIN_MOISTURE, currentMoistureAvg);
          Blynk.virtualWrite(VPIN_LIGHT, currentLight);
          Blynk.virtualWrite(VPIN_MOTOR_STATUS, motorStatus ? 255 : 0);
          Blynk.virtualWrite(VPIN_FAN_STATUS, fanStatus ? 255 : 0);
          Blynk.virtualWrite(VPIN_LIGHT_STATUS, lightStatus ? 255 : 0);
          Blynk.virtualWrite(VPIN_ARDUINO_AUTO_STATUS, arduinoAutoMode ? 255 : 0);
      }

      // Gửi lên MQTT (nếu kết nối)
      publishSensorDataToMQTT(); // Gọi hàm publish MQTT

  } else if (response.length() > 0) {
      Serial.println("Nhận được phản hồi không phải dữ liệu cho GET_DATA: " + response);
      if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, "GET_DATA resp: " + response);
      // Có thể publish lỗi này lên MQTT nếu muốn
      if (mqttClient.connected()) mqttClient.publish(topic_last_arduino_response, ("GET_DATA resp: " + response).c_str(), true);
  }

  // Cập nhật giá trị cài đặt lên Blynk (nếu kết nối)
  if (Blynk.connected()) {
      Blynk.virtualWrite(VPIN_SET_L, currentSetL);
      Blynk.virtualWrite(VPIN_SET_H, currentSetH);
  }
}

// --- Xử lý sự kiện Blynk ---
BLYNK_CONNECTED() {
  Serial.println("BLYNK_CONNECTED: Đã kết nối với máy chủ Blynk!");
  Blynk.syncAll();
  Blynk.virtualWrite(VPIN_SET_L, currentSetL);
  Blynk.virtualWrite(VPIN_SET_H, currentSetH);

  // <<<--- THÊM MỚI: Kết nối MQTT sau khi Blynk (và Wi-Fi) đã kết nối ---
  if (WiFi.status() == WL_CONNECTED) { // Đảm bảo Wi-Fi đã kết nối
      mqttClient.setServer(mqtt_server, mqtt_port);
      // mqttClient.setCallback(callback); // Nếu bạn muốn nhận (subscribe) tin nhắn MQTT
      reconnectMQTT(); // Thử kết nối MQTT lần đầu
  }
  timer.setTimeout(1000L, processAndSendData);
}

// Các hàm BLYNK_WRITE (nên gọi processAndSendData thay vì sendSensorDataToBlynk trực tiếp)
BLYNK_WRITE(VPIN_MOTOR_CONTROL) {
  int value = param.asInt();
  sendArduinoCommand(value == 1 ? "MOTOR_ON" : "MOTOR_OFF");
  timer.setTimeout(500L, processAndSendData);
}
BLYNK_WRITE(VPIN_FAN_CONTROL) {
  int value = param.asInt();
  sendArduinoCommand(value == 1 ? "FAN_ON" : "FAN_OFF");
  timer.setTimeout(500L, processAndSendData);
}
BLYNK_WRITE(VPIN_LIGHT_CONTROL) {
  int value = param.asInt();
  sendArduinoCommand(value == 1 ? "LIGHT_ON" : "LIGHT_OFF");
  timer.setTimeout(500L, processAndSendData);
}
BLYNK_WRITE(VPIN_AUTO_MODE) {
  if (param.asInt() == 1) {
    sendArduinoCommand("AUTO_MODE");
    if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Lệnh AUTO_MODE đã gửi");
  }
  timer.setTimeout(500L, processAndSendData);
}
BLYNK_WRITE(VPIN_SET_L) { /* ... Giữ nguyên, chỉ cần đảm bảo nó gọi hàm sendArduinoCommand ... */
  float val = param.asFloat(); String cmd = "SET_L," + String(val, 1);
  String resp = sendArduinoCommand(cmd);
  if (Blynk.connected()) {
      if (resp.startsWith("OK")) { currentSetL = val; Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Ngưỡng Dưới OK: " + String(val,1)); }
      else { Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Lỗi Ngưỡng Dưới: " + resp); Blynk.virtualWrite(VPIN_SET_L, currentSetL); }
  }
}
BLYNK_WRITE(VPIN_SET_H) { /* ... Giữ nguyên ... */
  float val = param.asFloat(); String cmd = "SET_H," + String(val, 1);
  String resp = sendArduinoCommand(cmd);
  if (Blynk.connected()){
      if (resp.startsWith("OK")) { currentSetH = val; Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Ngưỡng Trên OK: " + String(val,1)); }
      else { Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Lỗi Ngưỡng Trên: " + resp); Blynk.virtualWrite(VPIN_SET_H, currentSetH); }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 Blynk.Edgent + MQTT Publisher - Starting...");

  arduinoSerial.begin(9600, SERIAL_8N1, ARDUINO_RX_PIN, ARDUINO_TX_PIN);
  Serial.println("Serial2 for Arduino Initialized");

  randomSeed(micros()); // Khởi tạo seed cho số ngẫu nhiên (dùng cho MQTT client ID)

  BlynkEdgent.begin(); // Edgent quản lý WiFi & kết nối Blynk

  // Timer sẽ gọi processAndSendData, hàm này sẽ xử lý cả Blynk và MQTT
  timer.setInterval(10000L, []() { // Gửi dữ liệu mỗi 10 giây (điều chỉnh nếu cần)
    processAndSendData();
  });
}

void loop() {
  BlynkEdgent.run();
  timer.run();

  // <<<--- THÊM MỚI: Duy trì kết nối MQTT và xử lý tin nhắn đến (nếu có sub) ---
  if (WiFi.status() == WL_CONNECTED) { // Chỉ chạy MQTT client nếu có Wi-Fi
      if (!mqttClient.connected()) {
          long now = millis();
          // Thử kết nối lại MQTT mỗi 5 giây nếu bị mất kết nối
          // (Tránh thử liên tục để không làm nghẽn vòng lặp)
          static unsigned long lastMqttReconnectAttempt = 0;
          if (now - lastMqttReconnectAttempt > 5000) {
              lastMqttReconnectAttempt = now;
              reconnectMQTT();
          }
      } else {
          mqttClient.loop(); // QUAN TRỌNG: để PubSubClient xử lý các tác vụ nền
      }
  }


  // Xử lý dữ liệu không đồng bộ từ Arduino (Giữ nguyên)
  while (arduinoSerial.available()) {
      String asyncResponse = arduinoSerial.readStringUntil('\n'); asyncResponse.trim();
      Serial.print("Async Arduino msg: "); Serial.println(asyncResponse);
      if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Async: " + asyncResponse);
      if (mqttClient.connected()) mqttClient.publish(topic_last_arduino_response, ("Async: "+asyncResponse).c_str(), true);

      if (asyncResponse.startsWith("T:") && asyncResponse.indexOf("MAVG:") != -1) {
          Serial.println("Parsing periodic data from Arduino...");
          parseArduinoData(asyncResponse);
          if (Blynk.connected()) { /* ... cập nhật Blynk ... */
            Blynk.virtualWrite(VPIN_TEMP, currentTemp); Blynk.virtualWrite(VPIN_MOISTURE, currentMoistureAvg);
            Blynk.virtualWrite(VPIN_LIGHT, currentLight); Blynk.virtualWrite(VPIN_MOTOR_STATUS, motorStatus ? 255 : 0);
            Blynk.virtualWrite(VPIN_FAN_STATUS, fanStatus ? 255 : 0); Blynk.virtualWrite(VPIN_LIGHT_STATUS, lightStatus ? 255 : 0);
            Blynk.virtualWrite(VPIN_ARDUINO_AUTO_STATUS, arduinoAutoMode ? 255 : 0);
          }
          publishSensorDataToMQTT(); // Publish cả dữ liệu không đồng bộ này lên MQTT
      }
  }
}