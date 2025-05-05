#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <SoftwareSerial.h> // Thêm thư viện SoftwareSerial

#define DHTPIN 7
#define DHTTYPE DHT11 // DHT 11
DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27, 16, 2); // Đảm bảo địa chỉ I2C đúng

#define MoistureSensorPin A0
#define LightSensorPin A1

// Bỏ các nút bấm nếu không dùng nữa hoặc giữ lại làm backup local
// #define bt_set  10
// #define bt_up   8
// #define bt_down 11

#define motorRelay 9
#define fanRelay 6
#define lightRelay 2 // Relay đèn giữ nguyên
#define redPin 3
#define greenPin 4
#define bluePin 5

// --- Software Serial Communication with ESP32 ---
#define ESP_RX_PIN 13 // Nối với TX của ESP32
#define ESP_TX_PIN 14// Nối với RX của ESP32
SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN); // RX, TX

int moistureValue, lightValue;
float temperature, humidity; // Nên đọc cả humidity
float moisturePercent;
float setL_moisture = 30.0;
float setH_moisture = 70.0;

// Biến lưu trạng thái hiện tại của relay (quan trọng để báo cáo lại cho ESP32)
bool motorState = false; // false = OFF, true = ON (Giả sử relay kích LOW là ON)
bool fanState = false;
bool lightState = false;

// Biến kiểm soát chế độ Manual/Auto (nếu cần)
bool manualControl = false; // Ban đầu chạy tự động

void setup() {
  Serial.begin(9600); // Serial Monitor để debug (nếu cần)
  espSerial.begin(9600); // Serial để giao tiếp với ESP32
  Serial.println("Arduino Smart Garden Controller - Ready");

  pinMode(MoistureSensorPin, INPUT);
  pinMode(LightSensorPin, INPUT);
  // pinMode(bt_set, INPUT_PULLUP); // Bỏ comment nếu vẫn dùng nút
  // pinMode(bt_up, INPUT_PULLUP);
  // pinMode(bt_down, INPUT_PULLUP);
  pinMode(motorRelay, OUTPUT);
  pinMode(fanRelay, OUTPUT);
  pinMode(lightRelay, OUTPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);

  // Đặt trạng thái ban đầu cho relay (OFF - thường là HIGH cho relay kích LOW)
  digitalWrite(motorRelay, HIGH);
  digitalWrite(fanRelay, HIGH);
  digitalWrite(lightRelay, HIGH);
  setRGB(0, 0, 0); // Tắt LED RGB ban đầu
  motorState = false;
  fanState = false;
  lightState = false;


  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print(" Smart Garden ");
  delay(1500);
  lcd.clear();
  lcd.print("Waiting ESP...");
  Serial.println("LCD Initialized");

  dht.begin();
  Serial.println("DHT Initialized");

  // --- Đọc cài đặt từ EEPROM ---
  // Địa chỉ lưu khác nhau một chút để tránh ghi đè cờ
  if (EEPROM.read(0) != 1) { // Kiểm tra cờ (địa chỉ 0)
    Serial.println("Writing default thresholds to EEPROM");
    EEPROM.put(2, setL_moisture); // Lưu ở địa chỉ 2
    EEPROM.put(8, setH_moisture); // Lưu ở địa chỉ 8 (float cần 4 byte)
    EEPROM.write(0, 1); // Đặt cờ là đã ghi
  }
  EEPROM.get(2, setL_moisture);
  EEPROM.get(8, setH_moisture);
  Serial.print("Initial Low Moisture: "); Serial.println(setL_moisture);
  Serial.print("Initial High Moisture: "); Serial.println(setH_moisture);
}

void setRGB(int r, int g, int b) {
  analogWrite(redPin, r);
  analogWrite(greenPin, g);
  analogWrite(bluePin, b);
}

// Hàm bật/tắt motor (Giả sử LOW là ON)
void controlMotor(bool turnOn) {
  digitalWrite(motorRelay, turnOn ? LOW : HIGH);
  motorState = turnOn;
  Serial.print("Motor "); Serial.println(turnOn ? "ON" : "OFF");
}

// Hàm bật/tắt quạt (Giả sử LOW là ON)
void controlFan(bool turnOn) {
  digitalWrite(fanRelay, turnOn ? LOW : HIGH);
  fanState = turnOn;
  Serial.print("Fan "); Serial.println(turnOn ? "ON" : "OFF");
}

// Hàm bật/tắt đèn (Giả sử LOW là ON)
void controlLight(bool turnOn) {
  digitalWrite(lightRelay, turnOn ? LOW : HIGH);
  lightState = turnOn;
  Serial.print("Light "); Serial.println(turnOn ? "ON" : "OFF");
}


// Hàm xử lý lệnh từ ESP32
void handleSerialCommand() {
  if (espSerial.available() > 0) {
    String command = espSerial.readStringUntil('\n');
    command.trim(); // Xóa khoảng trắng thừa
    Serial.print("Received command: "); Serial.println(command);

    if (command.equals("GET_DATA")) {
      // Gửi dữ liệu hiện tại về ESP32
      String data = "T:" + String(temperature, 1) +
                    ",M:" + String(moisturePercent, 0) +
                    ",L:" + String(lightValue) + // Gửi giá trị thô của ánh sáng
                    ",MOT:" + String(motorState ? 1 : 0) +
                    ",FAN:" + String(fanState ? 1 : 0) +
                    ",LIT:" + String(lightState ? 1 : 0);
      espSerial.println(data); // Gửi lại cho ESP32
      Serial.print("Sent data: "); Serial.println(data);
    }
    else if (command.startsWith("SET_L,")) {
      float val = command.substring(6).toFloat();
      if (val >= 0 && val <= 100) {
        setL_moisture = val;
        EEPROM.put(2, setL_moisture); // Lưu vào EEPROM
        Serial.print("Set Low Moisture to: "); Serial.println(setL_moisture);
        espSerial.println("OK: SET_L updated"); // Phản hồi lại ESP32 (tùy chọn)
      } else {
        espSerial.println("ERR: Invalid SET_L value");
      }
    }
    else if (command.startsWith("SET_H,")) {
      float val = command.substring(6).toFloat();
      if (val >= 0 && val <= 100 && val > setL_moisture) { // Đảm bảo ngưỡng cao > thấp
        setH_moisture = val;
        EEPROM.put(8, setH_moisture); // Lưu vào EEPROM
        Serial.print("Set High Moisture to: "); Serial.println(setH_moisture);
        espSerial.println("OK: SET_H updated");
      } else {
         espSerial.println("ERR: Invalid SET_H value");
      }
    }
    else if (command.equals("MOTOR_ON")) {
      manualControl = true; // Chuyển sang manual nếu muốn
      controlMotor(true);
      espSerial.println("OK: MOTOR_ON executed");
    }
    else if (command.equals("MOTOR_OFF")) {
      manualControl = true;
      controlMotor(false);
      espSerial.println("OK: MOTOR_OFF executed");
    }
    else if (command.equals("FAN_ON")) {
      manualControl = true;
      controlFan(true);
      espSerial.println("OK: FAN_ON executed");
    }
     else if (command.equals("FAN_OFF")) {
      manualControl = true;
      controlFan(false);
      espSerial.println("OK: FAN_OFF executed");
    }
     else if (command.equals("LIGHT_ON")) {
      manualControl = true;
      controlLight(true);
      espSerial.println("OK: LIGHT_ON executed");
    }
    else if (command.equals("LIGHT_OFF")) {
      manualControl = true;
      controlLight(false);
      espSerial.println("OK: LIGHT_OFF executed");
    }
    else if (command.equals("AUTO_MODE")) {
      manualControl = false; // Quay lại chế độ tự động
      Serial.println("Switched to Auto Mode");
      espSerial.println("OK: AUTO_MODE activated");
    }
    // Thêm các lệnh khác nếu cần
    else {
       Serial.println("Unknown command");
       espSerial.println("ERR: Unknown command");
    }
  }
}

void loop() {
  // 1. Đọc cảm biến
  moistureValue = analogRead(MoistureSensorPin);
  lightValue = analogRead(LightSensorPin);
  temperature = dht.readTemperature();
  humidity = dht.readHumidity(); // Đọc cả độ ẩm không khí

  // Kiểm tra giá trị đọc từ DHT
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read from DHT sensor!");
    // Có thể không cập nhật LCD hoặc hiển thị lỗi
  } else {
     // 2. Xử lý dữ liệu
     // Map giá trị analog 10-bit (0-1023) sang % (0-100)
     moisturePercent = map(moistureValue, 0, 1023, 100, 0); // Giả sử ẩm cao -> giá trị thấp
     moisturePercent = constrain(moisturePercent, 0, 100); // Giới hạn trong khoảng 0-100
  }


  // 3. Logic điều khiển tự động (CHỈ chạy nếu không ở chế độ manual)
  if (!manualControl) {
    // Điều khiển Motor bơm nước tự động
    if (moisturePercent < setL_moisture) {
      if (!motorState) controlMotor(true); // Chỉ bật nếu đang tắt
    } else if (moisturePercent > setH_moisture) {
      if (motorState) controlMotor(false); // Chỉ tắt nếu đang bật
    }
    // Có thể thêm: else if (moisturePercent >= setL_moisture && motorState) controlMotor(false); // Tắt khi vượt ngưỡng dưới

    // Điều khiển Quạt làm mát tự động
    if (temperature > 30.0) { // Ngưỡng nhiệt độ ví dụ
       if (!fanState) controlFan(true);
    } else {
       if (fanState) controlFan(false);
    }

    // Điều khiển Đèn chiếu sáng tự động (và LED RGB)
    if (lightValue < 300) { // Rất tối
      setRGB(255, 0, 0); // LED Đỏ
      if (!lightState) controlLight(true); // Bật Đèn
    } else if (lightValue < 600) { // Ánh sáng yếu
      setRGB(255, 255, 0); // LED Vàng
       if (lightState) controlLight(false); // Tắt Đèn
    } else { // Đủ sáng
      setRGB(0, 0, 255); // LED Xanh dương
       if (lightState) controlLight(false); // Tắt Đèn
    }
  } else {
    // Đang ở chế độ Manual, có thể cập nhật LED RGB dựa trên trạng thái manual?
    // Hoặc không làm gì cả, chờ lệnh mới hoặc lệnh AUTO_MODE
    setRGB(0, 255, 0); // Ví dụ: LED xanh lá cây báo hiệu Manual Mode
  }


  // 4. Cập nhật LCD (Cập nhật thường xuyên)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("T:"); lcd.print(temperature, 1);
  lcd.print(" H:"); lcd.print(humidity, 0); // Hiển thị cả độ ẩm không khí
  lcd.print(" "); lcd.print(manualControl ? "MAN" : "AUT"); // Hiển thị chế độ
  lcd.setCursor(0, 1);
  lcd.print("M:"); lcd.print(moisturePercent, 0); lcd.print("%");
  lcd.print(" L:"); lcd.print(lightValue);
  // Hiển thị trạng thái relay
  lcd.print(motorState ? " P" : " -"); // P=Pump
  lcd.print(fanState ? " F" : " -");   // F=Fan
  lcd.print(lightState ? " L" : " -"); // L=Light


  // 5. Kiểm tra và xử lý lệnh từ ESP32
  handleSerialCommand();

  // 6. Delay nhỏ
  delay(100); // Giảm delay để phản hồi lệnh nhanh hơn, nhưng vẫn đủ để đọc LCD
}