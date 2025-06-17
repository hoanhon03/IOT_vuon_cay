#define BLYNK_TEMPLATE_ID "TMPL6i-bkM51m"
#define BLYNK_TEMPLATE_NAME "Vuon"
#define BLYNK_AUTH_TOKEN "dzE4BlA-E5Dh9vHP5lKWAJARYz8N6EVP"

#define BLYNK_PRINT Serial
// #define BLYNK_DEBUG // Bỏ comment nếu cần debug chi tiết

#include <WiFi.h>             // Cho kết nối Wi-Fi cơ bản
#include <WiFiManager.h>      // <<<--- THƯ VIỆN ĐỂ CẤU HÌNH WIFI QUA WEB PORTAL
#include <BlynkSimpleEsp32.h> // Thư viện Blynk cơ bản
#include <HardwareSerial.h>   // Cho Serial2

// --- Giao tiếp Serial với Arduino ---
#define ARDUINO_RX_PIN 16
#define ARDUINO_TX_PIN 17
HardwareSerial& arduinoSerial = Serial2;

// --- Biến lưu trữ dữ liệu đọc từ Arduino  ---
float currentTemp = -99.0;
float currentMoisture1 = -1.0; \
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

// --- Biến lưu trữ cài đặt  ---
float currentSetL = 30.0;
float currentSetH = 70.0;

BlynkTimer timer;

// --- Định nghĩa Virtual Pin ---
#define VPIN_TEMP V0 
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
#define VPIN_MOISTURE_1 V13 
#define VPIN_MOISTURE_2 V14
#define VPIN_MOISTURE_3 V15
#define VPIN_MOISTURE_4 V16
#define VPIN_ARDUINO_AUTO_STATUS V17

// --- Hàm gửi lệnh và đọc phản hồi ---
String sendArduinoCommand(String cmd, unsigned long timeout = 1000) {
  Serial.print("Gửi đến Arduino: "); Serial.println(cmd);
  arduinoSerial.println(cmd);
  unsigned long startTime = millis(); String response = "";
  while (millis() - startTime < timeout) {
    if (arduinoSerial.available()) {
      response = arduinoSerial.readStringUntil('\n'); response.trim();
      Serial.print("Phản hồi từ Arduino: "); Serial.println(response);
      lastArduinoResponse = response;
      if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, lastArduinoResponse);
      return response;
    }
  }
  Serial.println("Không có phản hồi từ Arduino (Hết thời gian chờ)");
  lastArduinoResponse = "Timeout";
  if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, lastArduinoResponse);
  return "";
}

// --- Hàm đọc và phân tích dữ liệu từ Arduino  ---
void parseArduinoData(String data) {
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

// --- Hàm gửi dữ liệu cảm biến lên Blynk ---
void sendDataToBlynkAndOtherServices() { // Đổi tên để rõ ràng hơn nếu sau này thêm MQTT
  if (!Blynk.connected()) {
    // Nếu không kết nối Blynk, có thể bạn vẫn muốn lấy dữ liệu từ Arduino cho các mục đích khác (ví dụ MQTT)
    // Hoặc đơn giản là thoát nếu chỉ quan tâm đến Blynk
    // Serial.println("Blynk not connected, skipping data send to Blynk.");
    // return;
  }

  String response = sendArduinoCommand("GET_DATA");
  if (response.length() > 0 && response.indexOf("T:") != -1) {
      parseArduinoData(response);

      if (Blynk.connected()) { // Chỉ gửi lên Blynk nếu đang kết nối
          Blynk.virtualWrite(VPIN_TEMP, currentTemp);
          Blynk.virtualWrite(VPIN_MOISTURE, currentMoistureAvg); // Độ ẩm trung bình
          Blynk.virtualWrite(VPIN_LIGHT, currentLight);
          Blynk.virtualWrite(VPIN_MOTOR_STATUS, motorStatus ? 255 : 0);
          Blynk.virtualWrite(VPIN_FAN_STATUS, fanStatus ? 255 : 0);
          Blynk.virtualWrite(VPIN_LIGHT_STATUS, lightStatus ? 255 : 0);
          Blynk.virtualWrite(VPIN_ARDUINO_AUTO_STATUS, arduinoAutoMode ? 255 : 0);
          // Gửi 4 giá trị độ ẩm riêng lẻ
          Blynk.virtualWrite(VPIN_MOISTURE_1, currentMoisture1);
          Blynk.virtualWrite(VPIN_MOISTURE_2, currentMoisture2);
          Blynk.virtualWrite(VPIN_MOISTURE_3, currentMoisture3);
          Blynk.virtualWrite(VPIN_MOISTURE_4, currentMoisture4);
      }
      // TODO: Thêm code publish MQTT ở đây nếu cần

  } else if (response.length() > 0) {
      Serial.println("Nhận được phản hồi không phải dữ liệu cho GET_DATA: " + response);
      if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, "GET_DATA resp: " + response);
  }

  if (Blynk.connected()) {
      Blynk.virtualWrite(VPIN_SET_L, currentSetL);
      Blynk.virtualWrite(VPIN_SET_H, currentSetH);
  }
}

BLYNK_CONNECTED() {
  Serial.println("BLYNK_CONNECTED: Đã kết nối với máy chủ Blynk!");
  Blynk.syncAll();
  Blynk.virtualWrite(VPIN_SET_L, currentSetL);
  Blynk.virtualWrite(VPIN_SET_H, currentSetH);
  timer.setTimeout(1000L, sendDataToBlynkAndOtherServices);
}

// Các hàm BLYNK_WRITE 
BLYNK_WRITE(VPIN_MOTOR_CONTROL) { /* ... gọi sendDataToBlynkAndOtherServices ... */
  int value = param.asInt(); sendArduinoCommand(value == 1 ? "MOTOR_ON" : "MOTOR_OFF");
  if (Blynk.connected()) timer.setTimeout(500L, sendDataToBlynkAndOtherServices);
}
BLYNK_WRITE(VPIN_FAN_CONTROL) { 
  int value = param.asInt(); sendArduinoCommand(value == 1 ? "FAN_ON" : "FAN_OFF");
  if (Blynk.connected()) timer.setTimeout(500L, sendDataToBlynkAndOtherServices);
}
BLYNK_WRITE(VPIN_LIGHT_CONTROL) {
  int value = param.asInt(); sendArduinoCommand(value == 1 ? "LIGHT_ON" : "LIGHT_OFF");
  if (Blynk.connected()) timer.setTimeout(500L, sendDataToBlynkAndOtherServices);
}
BLYNK_WRITE(VPIN_AUTO_MODE) { 
  if (param.asInt() == 1) { sendArduinoCommand("AUTO_MODE");
    if (Blynk.connected()) Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Lệnh AUTO_MODE đã gửi");
  }
  if (Blynk.connected()) timer.setTimeout(500L, sendDataToBlynkAndOtherServices);
}
BLYNK_WRITE(VPIN_SET_L) { 
  float val = param.asFloat(); String cmd = "SET_L," + String(val, 1);
  String resp = sendArduinoCommand(cmd);
  if (Blynk.connected()) {
      if (resp.startsWith("OK")) { currentSetL = val; Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Ngưỡng Dưới OK: " + String(val,1)); }
      else { Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Lỗi Ngưỡng Dưới: " + resp); Blynk.virtualWrite(VPIN_SET_L, currentSetL); }
  }
}
BLYNK_WRITE(VPIN_SET_H) { 
  float val = param.asFloat(); String cmd = "SET_H," + String(val, 1);
  String resp = sendArduinoCommand(cmd);
  if (Blynk.connected()){
      if (resp.startsWith("OK")) { currentSetH = val; Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Ngưỡng Trên OK: " + String(val,1)); }
      else { Blynk.virtualWrite(VPIN_LAST_RESPONSE, "Lỗi Ngưỡng Trên: " + resp); Blynk.virtualWrite(VPIN_SET_H, currentSetH); }
  }
}


void setup() {
  Serial.begin(115200);
  delay(100); // Cho Serial ổn định
  Serial.println("\nESP32 Blynk Controller with WiFiManager - Starting...");

  arduinoSerial.begin(9600, SERIAL_8N1, ARDUINO_RX_PIN, ARDUINO_TX_PIN);
  Serial.println("Serial2 for Arduino Initialized (9600 baud)");

  // --- KHỞI TẠO VÀ CHẠY WIFIMANAGER ---
  WiFiManager wm;

  // Xóa thông tin Wi-Fi đã lưu để test (bỏ comment nếu muốn mỗi lần khởi động đều vào trang config)
  //wm.resetSettings();

  // Đặt thời gian timeout cho trang cấu hình (ví dụ 180 giây)
  // Nếu không có ai cấu hình trong thời gian này, nó sẽ thoát hoặc restart tùy cài đặt.
  wm.setConfigPortalTimeout(180);

  // Tự động kết nối với Wi-Fi đã lưu.
  // Nếu không có thông tin đã lưu, hoặc kết nối thất bại, nó sẽ tạo AP tên "VuonThongMinhPortal"
  // và mở trang cấu hình.
  String apName = "VuonThongMinhPortal-" + String((uint32_t)ESP.getEfuseMac(), HEX); // Tạo tên AP duy nhất
  if (!wm.autoConnect(apName.c_str())) {
    Serial.println("Failed to connect to WiFi and hit timeout during configuration.");
    Serial.println("Restarting ESP...");
    delay(3000);
    ESP.restart(); // Khởi động lại ESP nếu không cấu hình được
  }

  // Nếu đến được đây, nghĩa là đã kết nối Wi-Fi thành công
  Serial.println("");
  Serial.println("WiFi connected successfully!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // --- KẾT NỐI BLYNK SAU KHI WIFI ĐÃ SẴN SÀNG ---
  Blynk.config(BLYNK_AUTH_TOKEN); // Cấu hình Auth Token
  // Cố gắng kết nối Blynk, có thể cần một vòng lặp nhỏ ở đây
  int blynkConnectRetries = 0;
  while (!Blynk.connected() && blynkConnectRetries < 10) { // Thử kết nối trong 5s
      Serial.print("Connecting to Blynk server... ");
      Blynk.connect(10000); // Timeout 10 giây cho mỗi lần thử kết nối Blynk
      if(Blynk.connected()){
          Serial.println("Connected to Blynk!");
          break;
      } else {
          Serial.println("Failed. Retrying...");
          delay(500); // Chờ 0.5s trước khi thử lại
      }
      blynkConnectRetries++;
  }
  if (!Blynk.connected()) {
      Serial.println("Could not connect to Blynk server after multiple retries. Check token or server status.");
      // Có thể không cần restart ở đây, thiết bị vẫn có thể hoạt động offline (nếu có logic đó)
  }


  // Thiết lập timer để gửi dữ liệu định kỳ
  timer.setInterval(5000L, []() { // Gửi dữ liệu mỗi 5 giây
    if (Blynk.connected()) { // Chỉ gửi nếu Blynk đang kết nối
      sendDataToBlynkAndOtherServices();
    }
  });
}

void loop() {
  // WiFiManager không cần gọi hàm run() trong loop sau khi đã kết nối
  // Nó chỉ chạy một lần trong setup để thiết lập Wi-Fi.

  if (WiFi.status() == WL_CONNECTED) {
    if (Blynk.connected()) {
      Blynk.run(); // Xử lý các tác vụ của Blynk
    } else {
      // Cố gắng kết nối lại Blynk nếu bị mất kết nối
      // (Nhưng không nên gọi Blynk.connect() quá thường xuyên trong loop chính)
      // Blynk.Edgent có cơ chế reconnect tốt hơn. Với cách thủ công, cần cẩn thận.
      // Serial.println("Blynk disconnected. Will attempt reconnect on next timer cycle or specific event.");
      // Một cách tiếp cận là thử reconnect trong BLYNK_APP_DISCONNECTED event nếu dùng thư viện Blynk mới hơn
      // Hoặc định kỳ thử reconnect (ví dụ mỗi 30s) bằng 1 timer khác.
      // Hiện tại, nếu mất kết nối, nó sẽ không tự kết nối lại ngay trong loop.
      // Bạn có thể thêm 1 timer để thử Blynk.connect() định kỳ.
    }
  } else {
    Serial.println("WiFi disconnected! Attempting to reconnect WiFi...");
    // Cố gắng kết nối lại Wi-Fi một cách cẩn thận để không làm treo ESP.
    // Cách đơn giản nhất là để WiFiManager xử lý khi khởi động lại.
    // Hoặc bạn có thể gọi wm.autoConnect() lại ở đây, nhưng cần quản lý trạng thái.
    delay(5000); // Chờ rồi thử lại ở vòng lặp tiếp theo (nếu không có WiFiManager)
                 // Hoặc đơn giản là chờ ESP.restart() nếu cài đặt timeout cho portal
    // ESP.restart(); // Cách mạnh tay để buộc vào lại trang config nếu mất WiFi
  }

  timer.run(); // Chạy các tác vụ của timer

  // Xử lý dữ liệu đến không đồng bộ từ Arduino (Giữ nguyên)
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
              Blynk.virtualWrite(VPIN_MOISTURE_1, currentMoisture1);
              Blynk.virtualWrite(VPIN_MOISTURE_2, currentMoisture2);
              Blynk.virtualWrite(VPIN_MOISTURE_3, currentMoisture3);
              Blynk.virtualWrite(VPIN_MOISTURE_4, currentMoisture4);
          }
      }
  }
}
