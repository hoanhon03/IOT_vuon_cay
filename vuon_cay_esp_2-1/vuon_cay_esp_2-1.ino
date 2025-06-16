//khai báo thư viện
#include <espConfig.h> 
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <HardwareSerial.h> // Để dùng Serial2

// Kích hoạt Blynk.Edgent
#define BLYNK_TEMPLATE_ID "TMPL6i-bkM51m"     // ID Template Blynk của bạn
#define BLYNK_TEMPLATE_NAME "Vuon" // Tên Template Blynk của bạn
#define BLYNK_AUTH_TOKEN "dzE4BlA-E5Dh9vHP5lKWAJARYz8N6EVP" // Auth Token thiết bị của bạn
// Comment out Blynk Print an disable debug messages once production version is tested
#define BLYNK_PRINT Serial // Cho phép log Blynk ra Serial Monitor
#define BLYNK_DEBUG // Bỏ comment dòng này để xem log debug chi tiết hơn từ Blynk
// --- Giao tiếp Serial với Arduino ---
#define ARDUINO_RX_PIN 16
#define ARDUINO_TX_PIN 17
HardwareSerial& arduinoSerial = Serial2; // Đặt tên dễ hiểu

// --- Biến lưu trữ dữ liệu đọc từ Arduino (Giữ nguyên như trước) ---
float currentTemp = -99.0;
float currentMoisture1 = -1.0;
float currentMoisture2 = -1.0;
float currentMoisture3 = -1.0;
float currentMoisture4 = -1.0;
float currentMoistureAvg = -1.0;
int currentLight = -1;
bool motorStatus = false;
bool fanStatus = false;
bool lightStatus = false;
bool arduinoAutoMode = true;
String lastArduinoResponse = "";

// --- Biến lưu trữ cài đặt (Giữ nguyên) ---
float currentSetL = 30.0;
float currentSetH = 70.0;

BlynkTimer timer; // Timer của Blynk

// --- Định nghĩa Virtual Pin (Giữ nguyên như trước) ---
#define VPIN_TEMP           V0
#define VPIN_MOISTURE       V1
#define VPIN_LIGHT          V2
#define VPIN_MOTOR_STATUS   V3
#define VPIN_FAN_STATUS     V4
#define VPIN_LIGHT_STATUS   V5
#define VPIN_MOTOR_CONTROL  V6
#define VPIN_FAN_CONTROL    V7
#define VPIN_LIGHT_CONTROL  V8
#define VPIN_AUTO_MODE      V9
#define VPIN_SET_L          V10
#define VPIN_SET_H          V11
#define VPIN_LAST_RESPONSE  V12
#define VPIN_MOISTURE_1     V13
#define VPIN_MOISTURE_2     V14
#define VPIN_MOISTURE_3     V15
#define VPIN_MOISTURE_4     V16
#define VPIN_ARDUINO_AUTO_STATUS V17

// --- Hàm gửi lệnh và đọc phản hồi (Giữ nguyên như trước) ---
String sendArduinoCommand(String cmd, unsigned long timeout = 1000) {
  Serial.print("Gửi đến Arduino: "); Serial.println(cmd);
  arduinoSerial.println(cmd);
  unsigned long startTime = millis();
  String response = "";
  while (millis() - startTime < timeout) {
    if (arduinoSerial.available()) {
      response = arduinoSerial.readStringUntil('\n');
      response.trim();
      Serial.print("Phản hồi từ Arduino: "); Serial.println(response);
      lastArduinoResponse = response;
      if (Blynk.connected()) { // Chỉ ghi vào Blynk nếu đã kết nối
          Blynk.virtualWrite(VPIN_LAST_RESPONSE, lastArduinoResponse);
      }
      return response;
    }
  }
  Serial.println("Không có phản hồi từ Arduino (Hết thời gian chờ)");
  lastArduinoResponse = "Timeout";
  if (Blynk.connected()) {
      Blynk.virtualWrite(VPIN_LAST_RESPONSE, lastArduinoResponse);
  }
  return "";
}

// --- Hàm đọc và phân tích dữ liệu từ Arduino (Giữ nguyên như trước) ---
void parseArduinoData(String data) {
   if (data.length() == 0) return;
   auto getValue = [&](int key_idx, int key_len) -> String { /* ... giữ nguyên ... */
       if (key_idx == -1) return "";
       int val_start = key_idx + key_len;
       int val_end = data.indexOf(',', val_start);
       if (val_end == -1) { return data.substring(val_start); }
       return data.substring(val_start, val_end);
   };
   int t_idx = data.indexOf("T:");
   int m1_idx = data.indexOf("M1:"); // Độ ẩm cảm biến 1
   int m2_idx = data.indexOf("M2:");// Độ ẩm cảm biến 2
   int m3_idx = data.indexOf("M3:"); // Độ ẩm cảm biến 3
   int m4_idx = data.indexOf("M4:");// Độ ẩm cảm biến 4
   int mavg_idx = data.indexOf("MAVG:"); // Độ ẩm trung bình
   int l_idx = data.indexOf("L:");
   int mot_idx = data.indexOf("MOT:");
    int fan_idx = data.indexOf("FAN:");
   int lit_idx = data.indexOf("LIT:"); 
   int auto_idx = data.indexOf("AUTO:");

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

// --- Hàm gửi dữ liệu cảm biến lên Blynk (Giữ nguyên như trước, nhưng kiểm tra Blynk.connected()) ---
void sendSensorDataToBlynk() {
  if (!Blynk.connected()) { // Nếu không kết nối Blynk thì không làm gì cả
    return;
  }
  String response = sendArduinoCommand("GET_DATA");
  if (response.length() > 0 && response.indexOf("T:") != -1) {
      parseArduinoData(response);
      Blynk.virtualWrite(VPIN_TEMP, currentTemp);
      Blynk.virtualWrite(VPIN_MOISTURE, currentMoistureAvg);
      Blynk.virtualWrite(VPIN_LIGHT, currentLight);
      Blynk.virtualWrite(VPIN_MOTOR_STATUS, motorStatus ? 255 : 0);
      Blynk.virtualWrite(VPIN_FAN_STATUS, fanStatus ? 255 : 0);
      Blynk.virtualWrite(VPIN_LIGHT_STATUS, lightStatus ? 255 : 0);
      Blynk.virtualWrite(VPIN_ARDUINO_AUTO_STATUS, arduinoAutoMode ? 255 : 0);
      // Tùy chọn gửi 4 cảm biến riêng lẻ
  } else if (response.length() > 0) {
      Serial.println("Nhận được phản hồi không phải dữ liệu cho GET_DATA: " + response);
      Blynk.virtualWrite(VPIN_LAST_RESPONSE, "GET_DATA resp: " + response);
  }
  Blynk.virtualWrite(VPIN_SET_L, currentSetL);
  Blynk.virtualWrite(VPIN_SET_H, currentSetH);
}

// --- Xử lý sự kiện Blynk ---
// Hàm này được gọi khi ESP32 kết nối thành công với Máy chủ Blynk
BLYNK_CONNECTED() {
  Serial.println("BLYNK_CONNECTED: Đã kết nối với máy chủ Blynk!");
  Blynk.syncAll(); // Đồng bộ tất cả các VPIN có thể ghi từ server
  // Gửi giá trị cài đặt hiện tại của ESP32 lên Blynk (phòng trường hợp server chưa có)
  Blynk.virtualWrite(VPIN_SET_L, currentSetL);
  Blynk.virtualWrite(VPIN_SET_H, currentSetH);
  // Yêu cầu dữ liệu từ Arduino lần đầu sau khi kết nối
  timer.setTimeout(1000L, sendSensorDataToBlynk); // Chờ 1s rồi mới gửi
}

// Các hàm BLYNK_WRITE (Giữ nguyên như trước, nhưng nên kiểm tra Blynk.connected() trước khi gọi timer)
BLYNK_WRITE(VPIN_MOTOR_CONTROL) {
  int value = param.asInt();
  sendArduinoCommand(value == 1 ? "MOTOR_ON" : "MOTOR_OFF");
  if (Blynk.connected()) timer.setTimeout(500L, sendSensorDataToBlynk);
}
BLYNK_WRITE(VPIN_FAN_CONTROL) {
  int value = param.asInt();
  sendArduinoCommand(value == 1 ? "FAN_ON" : "FAN_OFF");
  if (Blynk.connected()) timer.setTimeout(500L, sendSensorDataToBlynk);
}
BLYNK_WRITE(VPIN_LIGHT_CONTROL) {
  int value = param.asInt();
  sendArduinoCommand(value == 1 ? "LIGHT_ON" : "LIGHT_OFF");
  if (Blynk.connected()) timer.setTimeout(500L, sendSensorDataToBlynk);
}
BLYNK_WRITE(VPIN_AUTO_MODE) {
  if (param.asInt() == 1) {
    sendArduinoCommand("AUTO_MODE");
    if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Lệnh AUTO_MODE đã gửi");
  }
  if (Blynk.connected()) timer.setTimeout(500L, sendSensorDataToBlynk);
}
BLYNK_WRITE(VPIN_SET_L) {
  float val = param.asFloat();
  String cmd = "SET_L," + String(val, 1);
  String resp = sendArduinoCommand(cmd);
  if (Blynk.connected()) {
      if (resp.startsWith("OK")) {
        currentSetL = val;
        Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Ngưỡng Dưới OK: " + String(val,1));
      } else {
        Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Lỗi Ngưỡng Dưới: " + resp);
        Blynk.virtualWrite(VPIN_SET_L, currentSetL);
      }
  }
}
BLYNK_WRITE(VPIN_SET_H) {
  float val = param.asFloat();
  String cmd = "SET_H," + String(val, 1);
  String resp = sendArduinoCommand(cmd);
  if (Blynk.connected()){
      if (resp.startsWith("OK")) {
        currentSetH = val;
        Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Ngưỡng Trên OK: " + String(val,1));
      } else {
        Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Lỗi Ngưỡng Trên: " + resp);
        Blynk.virtualWrite(VPIN_SET_H, currentSetH);
      }
  }
}

// Hàm này được gọi một lần khi chương trình bắt đầu
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 Blynk.Edgent Controller - Starting...");

  // Khởi tạo Serial2 để giao tiếp với Arduino
  arduinoSerial.begin(9600, SERIAL_8N1, ARDUINO_RX_PIN, ARDUINO_TX_PIN);
  Serial.println("Serial2 for Arduino Initialized (9600 baud)");

  BlynkEdgent.begin(); // Khởi tạo Blynk.Edgent - phần này sẽ quản lý WiFi và kết nối Blynk

  // Thiết lập timer để yêu cầu dữ liệu từ Arduino và gửi lên Blynk định kỳ
  // CHỈ THIẾT LẬP TIMER SAU KHI BLYNK ĐÃ KẾT NỐI (trong BLYNK_CONNECTED hoặc sau khi Edgent.begin chạy xong)
  // Tuy nhiên, Edgent sẽ tự gọi BLYNK_CONNECTED khi kết nối thành công.
  // Chúng ta sẽ đặt timer trong BLYNK_CONNECTED hoặc sau 1 khoảng thời gian nếu muốn nó chạy ngay khi có kết nối.
  // Hiện tại, việc gửi dữ liệu định kỳ sẽ do timer trong BLYNK_CONNECTED quản lý.
  // Hoặc, nếu muốn một timer chung, có thể đặt ở đây, nhưng phải đảm bảo Blynk.connected()
  timer.setInterval(5000L, []() { // Lambda function cho timer
    if (Blynk.connected()) {
      sendSensorDataToBlynk();
    }
  });
}

// Hàm này được gọi lặp đi lặp lại
void loop() {
  BlynkEdgent.run(); // Hàm này phải được gọi thường xuyên để Blynk.Edgent hoạt động
  timer.run();       // Chạy các tác vụ của timer (nếu có)

  // Xử lý dữ liệu đến không đồng bộ từ Arduino
  while (arduinoSerial.available()) {
      String asyncResponse = arduinoSerial.readStringUntil('\n');
      asyncResponse.trim();
      Serial.print("Async Arduino msg: "); Serial.println(asyncResponse);
      if (Blynk.connected()) {
          Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Async: " + asyncResponse);
          if (asyncResponse.startsWith("T:") && asyncResponse.indexOf("MAVG:") != -1) {
              Serial.println("Parsing periodic data from Arduino...");
              parseArduinoData(asyncResponse);
              // Cập nhật Blynk với dữ liệu mới nhận được
              Blynk.virtualWrite(VPIN_TEMP, currentTemp);
              Blynk.virtualWrite(VPIN_MOISTURE, currentMoistureAvg);
              Blynk.virtualWrite(VPIN_LIGHT, currentLight);
              Blynk.virtualWrite(VPIN_MOTOR_STATUS, motorStatus ? 255 : 0);
              Blynk.virtualWrite(VPIN_FAN_STATUS, fanStatus ? 255 : 0);
              Blynk.virtualWrite(VPIN_LIGHT_STATUS, lightStatus ? 255 : 0);
              Blynk.virtualWrite(VPIN_ARDUINO_AUTO_STATUS, arduinoAutoMode ? 255 : 0);
          }
      }
  }
}