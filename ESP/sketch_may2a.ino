#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h> // Để dùng Serial2

// --- Cấu hình Wi-Fi ---
const char* ssid = "Khay"; // Thay bằng tên Wi-Fi của bạn
const char* password = "khay2005"; // Thay bằng mật khẩu Wi-Fi

// --- Serial Communication with Arduino ---
// Sử dụng Serial2 của ESP32 (Thường là GPIO 16 (RX2), GPIO 17 (TX2))
// Nối ESP32 TX2 (GPIO17) với Arduino RX (Pin 2 trong code Arduino)
// Nối ESP32 RX2 (GPIO16) với Arduino TX (Pin 3 trong code Arduino)
#define ARDUINO_RX_PIN 16
#define ARDUINO_TX_PIN 17
HardwareSerial& arduinoSerial = Serial2; // Đặt tên dễ hiểu

// Web Server
WebServer server(80);

// Biến lưu trữ dữ liệu đọc từ Arduino
float currentTemp = -99.0;
float currentMoisture = -1.0;
int currentLight = -1;
bool motorStatus = false;
bool fanStatus = false;
bool lightStatus = false;
String lastArduinoResponse = ""; // Lưu phản hồi gần nhất từ Arduino

// Biến lưu trữ cài đặt đọc từ Arduino (hoặc giữ bản sao)
float currentSetL = 30.0; // Giá trị khởi tạo, sẽ được cập nhật
float currentSetH = 70.0;

// Thời gian cho việc gửi yêu cầu dữ liệu định kỳ
unsigned long lastDataRequestTime = 0;
const long dataRequestInterval = 5000; // Yêu cầu dữ liệu mỗi 5 giây

// --- Hàm Setup ---
void setup() {
  Serial.begin(115200); // Serial Monitor để debug ESP32
  Serial.println("\nESP32 Web Controller Starting...");

  // Khởi tạo Serial2 để giao tiếp với Arduino
  arduinoSerial.begin(9600, SERIAL_8N1, ARDUINO_RX_PIN, ARDUINO_TX_PIN);
  Serial.println("Serial2 for Arduino Initialized (9600 baud)");

  // Kết nối Wi-Fi
  Serial.print("Connecting to "); Serial.println(ssid);
  WiFi.begin(ssid, password);
  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 30) {
    delay(500); Serial.print(".");
    wifi_retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection Failed!");
    // Có thể khởi động lại ESP32 hoặc vào chế độ AP dự phòng
  }

  // --- Cấu hình Web Server ---
  server.on("/", HTTP_GET, handleRoot);         // Trang chính hiển thị dữ liệu + control
  server.on("/get_data", HTTP_GET, handleGetData); // API Endpoint để cập nhật data (dùng cho AJAX)
  server.on("/send_command", HTTP_GET, handleSendCommand); // Xử lý gửi lệnh (ví dụ: ?cmd=MOTOR_ON)
  server.on("/set_thresholds", HTTP_POST, handleSetThresholds); // Xử lý form cài đặt ngưỡng
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  // Gửi yêu cầu đọc ngưỡng ban đầu từ Arduino (nếu muốn)
  // Hoặc giả định giá trị mặc định và cập nhật khi người dùng set
}

// --- Hàm gửi lệnh và đọc phản hồi (nếu cần) ---
String sendArduinoCommand(String cmd, unsigned long timeout = 1000) {
  Serial.print("Sending to Arduino: "); Serial.println(cmd);
  arduinoSerial.println(cmd); // Gửi lệnh kèm newline

  unsigned long startTime = millis();
  String response = "";
  while (millis() - startTime < timeout) {
    if (arduinoSerial.available()) {
      response = arduinoSerial.readStringUntil('\n');
      response.trim();
      Serial.print("Response from Arduino: "); Serial.println(response);
      return response; // Trả về phản hồi đầu tiên nhận được
    }
  }
  Serial.println("No response from Arduino (Timeout)");
  return ""; // Trả về chuỗi rỗng nếu timeout
}

// --- Hàm đọc và phân tích dữ liệu từ Arduino ---
void parseArduinoData(String data) {
   if (data.length() == 0) return;
   lastArduinoResponse = data; // Lưu lại để debug hoặc hiển thị thô

   // Phân tích chuỗi dạng "T:28.5,M:55,L:450,MOT:1,FAN:0,LIT:1"
   int t_start = data.indexOf("T:") + 2;
   int m_start = data.indexOf("M:") + 2;
   int l_start = data.indexOf("L:") + 2;
   int mot_start = data.indexOf("MOT:") + 4;
   int fan_start = data.indexOf("FAN:") + 4;
   int lit_start = data.indexOf("LIT:") + 4;

   if (t_start < 2 || m_start < 2 || l_start < 2 || mot_start < 4 || fan_start < 4 || lit_start < 4) {
      Serial.println("Error parsing Arduino data string");
      return;
   }

   // Tìm vị trí dấu phẩy để tách giá trị
   int t_end = data.indexOf(',', t_start);
   int m_end = data.indexOf(',', m_start);
   int l_end = data.indexOf(',', l_start);
   int mot_end = data.indexOf(',', mot_start);
   int fan_end = data.indexOf(',', fan_start);
   // Giá trị cuối không cần dấu phẩy

   currentTemp = data.substring(t_start, t_end).toFloat();
   currentMoisture = data.substring(m_start, m_end).toFloat();
   currentLight = data.substring(l_start, l_end).toInt();
   motorStatus = (data.substring(mot_start, mot_end).toInt() == 1);
   fanStatus = (data.substring(fan_start, fan_end).toInt() == 1);
   lightStatus = (data.substring(lit_start).toInt() == 1); // Lấy phần còn lại

   Serial.printf("Parsed Data: T=%.1f, M=%.0f, L=%d, Mot=%d, Fan=%d, Lit=%d\n",
                  currentTemp, currentMoisture, currentLight, motorStatus, fanStatus, lightStatus);
}


// --- Hàm Loop ---
void loop() {
  server.handleClient(); // Xử lý các yêu cầu web

  unsigned long currentTime = millis();

  // Định kỳ gửi yêu cầu lấy dữ liệu từ Arduino
  if (currentTime - lastDataRequestTime >= dataRequestInterval) {
    lastDataRequestTime = currentTime;
    // Gửi lệnh yêu cầu dữ liệu và nhận phản hồi ngay lập tức
    String response = sendArduinoCommand("GET_DATA");
    if (response.length() > 0) {
        parseArduinoData(response);
        // Có thể cần cập nhật currentSetL, currentSetH nếu Arduino gửi cả thông tin đó
    }
  }

  // Đọc các phản hồi không đồng bộ khác từ Arduino (nếu có)
  // Ví dụ: Arduino có thể gửi thông báo lỗi hoặc xác nhận lệnh
  while (arduinoSerial.available()) {
      String asyncResponse = arduinoSerial.readStringUntil('\n');
      asyncResponse.trim();
      Serial.print("Async Arduino msg: "); Serial.println(asyncResponse);
      lastArduinoResponse = asyncResponse; // Cập nhật phản hồi cuối cùng
      // Xử lý các phản hồi này nếu cần (ví dụ: hiển thị trên web)
  }

  // Thêm delay nhỏ để tránh ESP32 quá bận rộn
  delay(10);
}

// --- Các hàm xử lý yêu cầu Web ---

// Tạo trang HTML chính
String buildHtmlPage() {
  String page = "<!DOCTYPE html><html lang='vi'><head>";
  page += "<meta charset='UTF-8'>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  // Thêm refresh tự động sau mỗi 10 giây
  page += "<meta http-equiv='refresh' content='10'>";
  page += "<title>VƯỜN CÂY THÔNG MINH</title>";
  page += "<style>";
  // (Copy CSS từ code ESP32 trước đó hoặc tự style)
  page += "body { font-family: sans-serif; max-width: 700px; margin: 20px auto; padding: 15px; border: 1px solid #ccc; }";
  page += "h1, h2 { color: #2a9d8f; }";
  page += ".grid-container { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }";
  page += ".section { padding: 15px; border: 1px solid #eee; border-radius: 5px; }";
  page += ".label { font-weight: bold; min-width: 100px; display: inline-block; }";
  page += ".value { color: #e76f51; }";
  page += ".status-on { color: green; font-weight: bold; }";
  page += ".status-off { color: red; font-weight: bold; }";
  page += "button, input[type='submit'] { background-color: #2a9d8f; color: white; padding: 8px 12px; border: none; border-radius: 4px; cursor: pointer; margin: 5px 2px; }";
  page += "button:hover, input[type='submit']:hover { background-color: #264653; }";
  page += ".cmd-button { background-color: #f4a261; }"; // Màu khác cho nút lệnh
  page += "</style>";
  page += "</head><body>";
  page += "<h1>Smart Garden Control (ESP32+Uno)</h1>";
  page += "<p>IP Address: " + WiFi.localIP().toString() + "</p>";
  page += "<p>Last Arduino Response: <i id='arduino-resp'>" + lastArduinoResponse + "</i></p>"; // Hiển thị phản hồi cuối cùng

  page += "<div class='grid-container'>";

  // --- Phần trạng thái ---
  page += "<div class='section status'>";
  page += "<h2>Trạng thái</h2>";
  page += "<p><span class='label'>Nhiệt độ:</span> <span class='value'>" + String(currentTemp, 1) + " °C</span></p>";
  page += "<p><span class='label'>Độ ẩm đất:</span> <span class='value'>" + String(currentMoisture, 0) + " %</span></p>";
  page += "<p><span class='label'>Ánh sáng:</span> <span class='value'>" + String(currentLight) + " (raw)</span></p>";
  page += "<hr>";
  page += "<p><span class='label'>Bơm:</span> <span class='" + String(motorStatus ? "status-on" : "status-off") + "'>" + (motorStatus ? "ON" : "OFF") + "</span></p>";
  page += "<p><span class='label'>Quạt:</span> <span class='" + String(fanStatus ? "status-on" : "status-off") + "'>" + (fanStatus ? "ON" : "OFF") + "</span></p>";
  page += "<p><span class='label'>Đèn:</span> <span class='" + String(lightStatus ? "status-on" : "status-off") + "'>" + (lightStatus ? "ON" : "OFF") + "</span></p>";
  page += "</div>"; // end status section

  // --- Phần điều khiển ---
  page += "<div class='section controls'>";
  page += "<h2>Điều khiển Manual</h2>";
   // Sử dụng link để gửi lệnh GET đơn giản
  page += "<p>Bơm: <a href='/send_command?cmd=MOTOR_ON'><button class='cmd-button'>ON</button></a> <a href='/send_command?cmd=MOTOR_OFF'><button class='cmd-button'>OFF</button></a></p>";
  page += "<p>Quạt: <a href='/send_command?cmd=FAN_ON'><button class='cmd-button'>ON</button></a> <a href='/send_command?cmd=FAN_OFF'><button class='cmd-button'>OFF</button></a></p>";
  page += "<p>Đèn: <a href='/send_command?cmd=LIGHT_ON'><button class='cmd-button'>ON</button></a> <a href='/send_command?cmd=LIGHT_OFF'><button class='cmd-button'>OFF</button></a></p>";
  page += "<hr>";
  page += "<p><a href='/send_command?cmd=AUTO_MODE'><button>Chuyển về Tự Động</button></a></p>";
  page += "</div>"; // end controls section


  // --- Phần cài đặt ---
  page += "<div class='section settings'>";
  page += "<h2>Cài đặt ngưỡng ẩm</h2>";
  page += "<form action='/set_thresholds' method='POST'>";
  // Lấy giá trị ngưỡng hiện tại từ biến global (cần có cách cập nhật chúng từ Arduino)
  // Hiện tại đang dùng giá trị mặc định hoặc giá trị cuối cùng được set thành công
  page += "<label for='low'>Ngưỡng dưới (%):</label>";
  page += "<input type='number' step='0.1' id='low' name='low' value='" + String(currentSetL, 1) + "' required><br>";
  page += "<label for='high'>Ngưỡng trên (%):</label>";
  page += "<input type='number' step='0.1' id='high' name='high' value='" + String(currentSetH, 1) + "' required><br>";
  page += "<input type='submit' value='Lưu ngưỡng'>";
  page += "</form>";
  page += "</div>"; // end settings section

  page += "</div>"; // end grid-container
  page += "</body></html>";
  return page;
}

// Xử lý yêu cầu trang gốc "/"
void handleRoot() {
  // Yêu cầu dữ liệu mới nhất trước khi hiển thị (hoặc dựa vào dữ liệu polling)
  // String response = sendArduinoCommand("GET_DATA");
  // if (response.length() > 0) {
  //     parseArduinoData(response);
  // }
  server.send(200, "text/html", buildHtmlPage());
}

// Xử lý gửi lệnh điều khiển
void handleSendCommand() {
  if (server.hasArg("cmd")) {
    String command = server.arg("cmd");
    String response = sendArduinoCommand(command); // Gửi lệnh đến Arduino
    lastArduinoResponse = response; // Cập nhật phản hồi để hiển thị
    // Chuyển hướng về trang chính để xem kết quả
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");
  } else {
    server.send(400, "text/plain", "Bad Request: Missing command parameter 'cmd'");
  }
}

// Xử lý form cài đặt ngưỡng
void handleSetThresholds() {
  if (server.hasArg("low") && server.hasArg("high")) {
    String lowStr = server.arg("low");
    String highStr = server.arg("high");
    float lowVal = lowStr.toFloat();
    float highVal = highStr.toFloat();

    // Gửi lệnh SET_L
    String cmdL = "SET_L," + lowStr;
    String respL = sendArduinoCommand(cmdL);

    // Gửi lệnh SET_H
    String cmdH = "SET_H," + highStr;
    String respH = sendArduinoCommand(cmdH);

    lastArduinoResponse = "Set L: " + respL + " | Set H: " + respH;

     // Cập nhật giá trị ngưỡng hiện tại trên ESP32 nếu thành công
    if (respL.startsWith("OK")) currentSetL = lowVal;
    if (respH.startsWith("OK")) currentSetH = highVal;

    // Chuyển hướng về trang chính
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "");

  } else {
    server.send(400, "text/plain", "Bad Request: Missing threshold values.");
  }
}

// (Optional) Endpoint để lấy dữ liệu JSON cho AJAX update
void handleGetData() {
  // Yêu cầu dữ liệu mới nhất
  String response = sendArduinoCommand("GET_DATA");
   if (response.length() > 0) {
       parseArduinoData(response);
   }

  String json = "{";
  json += "\"temperature\":" + String(currentTemp, 1) + ",";
  json += "\"moisture\":" + String(currentMoisture, 0) + ",";
  json += "\"light\":" + String(currentLight) + ",";
  json += "\"motor_on\":" + String(motorStatus ? "true" : "false") + ",";
  json += "\"fan_on\":" + String(fanStatus ? "true" : "false") + ",";
  json += "\"light_on\":" + String(lightStatus ? "true" : "false") + ",";
  json += "\"set_l\":" + String(currentSetL, 1) + ","; // Thêm ngưỡng nếu có
  json += "\"set_h\":" + String(currentSetH, 1) + ",";
  json += "\"last_response\":\"" + lastArduinoResponse + "\""; // Thêm dấu nháy kép cho chuỗi JSON
  json += "}";
  server.send(200, "application/json", json);
}


// Xử lý khi không tìm thấy trang
void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}