#include <EEPROM.h>          // Thư viện để lưu và đọc dữ liệu từ bộ nhớ EEPROM
#include <Wire.h>            // Thư viện cho giao tiếp I2C (dùng cho LCD)
#include <LiquidCrystal_I2C.h> // Thư viện điều khiển màn hình LCD qua I2C
#include <DHT.h>             // Thư viện cho cảm biến nhiệt độ và độ ẩm DHT
#include <SoftwareSerial.h>  // Thư viện để tạo cổng serial ảo giao tiếp với ESP32

// --- Cấu hình Cảm biến ---
#define DHTPIN 7        // Chân DATA của cảm biến DHT11/DHT22
#define DHTTYPE DHT11   // Loại cảm biến DHT (DHT11 hoặc DHT22)
DHT dht(DHTPIN, DHTTYPE); // Khởi tạo đối tượng cảm biến DHT
// Chân cảm biến độ ẩm đất (Analog) - Sử dụng 4 cảm biến
#define MOISTURE_SENSOR_PIN_1 A0
#define MOISTURE_SENSOR_PIN_2 A1
#define MOISTURE_SENSOR_PIN_3 A2
#define MOISTURE_SENSOR_PIN_4 A3
// Chân cảm biến ánh sáng (quang trở)
#define LightSensorPin A5 
// --- Chân điều khiển Relay ---
// [QUAN TRỌNG] ĐẢM BẢO MODULE RELAY ĐƯỢC CẤP NGUỒN RIÊNG QUA CHÂN JD-VCC
// ĐỂ TRÁNH SỤT ÁP LÀM TREO ARDUINO.
#define motorRelayPin 8  // Relay cho Bơm
#define fanRelay 6       // Relay cho Quạt
#define lightRelay 2     // Relay cho Đèn
// --- Software Serial Communication with ESP32 ---
#define ESP_RX_PIN 12  // Chân RX của Arduino (nối với TX của ESP32)
#define ESP_TX_PIN 13  // Chân TX của Arduino (nối với RX của ESP32)
SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN); // Tạo cổng serial ảo tên là espSerial

// --- Cấu hình LCD I2C ---
LiquidCrystal_I2C lcd(0x27, 16, 2); // Địa chỉ I2C của LCD là 0x27, kích thước 16 cột x 2 dòng

// [QUAN TRỌNG] BẠN CẦN HIỆU CHUẨN LẠI CÁC GIÁ TRỊ NÀY CHO TỪNG CẢM BIẾN
// BẰNG CÁCH ĐO GIÁ TRỊ THÔ KHI KHÔ (TRONG KHÔNG KHÍ) VÀ KHI ƯỚT (TRONG NƯỚC)
const int SENSOR_DRY_VALUE_1 = 514; const int SENSOR_WET_VALUE_1 = 233; // Ví dụ giá trị đã hiệu chuẩn
const int SENSOR_DRY_VALUE_2 = 503; const int SENSOR_WET_VALUE_2 = 256;
const int SENSOR_DRY_VALUE_3 = 504; const int SENSOR_WET_VALUE_3 = 256;
const int SENSOR_DRY_VALUE_4 = 506; const int SENSOR_WET_VALUE_4 = 248;

// --- Biến lưu trữ giá trị cảm biến ---
float temperature, humidity; // Nhiệt độ, độ ẩm không khí
int lightValue;              // Giá trị thô của cảm biến ánh sáng
float moisturePercent1, moisturePercent2, moisturePercent3, moisturePercent4; // Độ ẩm từng cảm biến
float averageMoisturePercent;  // Độ ẩm trung bình của 4 cảm biến

// --- Ngưỡng và trạng thái ---
float setL_moisture = 30.0; // Ngưỡng độ ẩm đất thấp để bật bơm
float setH_moisture = 70.0; // Ngưỡng độ ẩm đất cao để tắt bơm
bool motorState = false;  // Trạng thái bơm (false = OFF, true = ON)
bool fanState = false;    // Trạng thái quạt
bool lightState = false;  // Trạng thái đèn
bool manualControl = false;  // Chế độ điều khiển (false = tự động, true = thủ công)

// --- Biến thời gian cho các tác vụ định kỳ ---
unsigned long lastDataSendTimeToEsp = 0; // Thời điểm cuối cùng gửi dữ liệu lên ESP32
// [SỬA ĐỔI] Thay đổi thời gian gửi từ 30 phút thành 5 phút
const unsigned long dataSendIntervalToEsp = 5UL * 60UL * 1000UL; // Khoảng thời gian gửi: 5 phút

unsigned long lastSensorReadAndLogicTime = 0; // Thời điểm cuối cùng đọc cảm biến và chạy logic
const unsigned long sensorReadAndLogicInterval = 2000UL; // [CẢI TIẾN] Tăng lên 2 giây để hệ thống ổn định hơn

void setup() {
  Serial.begin(9600);       // Mở cổng Serial cứng để debug (xem trên Serial Monitor)
  espSerial.begin(9600);    // Mở cổng Serial ảo để giao tiếp với ESP32
  Serial.println("Vườn cây thông minh -- Ready");

  pinMode(motorRelayPin, OUTPUT);
  pinMode(fanRelay, OUTPUT);
  pinMode(lightRelay, OUTPUT);

  // Đặt trạng thái ban đầu cho các Relay (TẮT)
  // Giả sử Relay kích LOW là ON, nên HIGH là TẮT
  digitalWrite(motorRelayPin, HIGH); motorState = false;
  digitalWrite(fanRelay, HIGH);      fanState = false;
  digitalWrite(lightRelay, HIGH);    lightState = false;

  Wire.begin();       
  lcd.init();        
  lcd.backlight();    
  lcd.clear();       
  lcd.print("VUON_CAY_THONG_MINH");
  lcd.setCursor(0, 1); 
  lcd.print("Khoi Dong...");
  delay(2000);
  
  dht.begin();

  // Đọc ngưỡng từ EEPROM
  if (EEPROM.read(0) != 1) { 
    Serial.println("Ghi nguong mac dinh vao EEPROM");
    EEPROM.put(2, setL_moisture);
    EEPROM.put(8, setH_moisture);
    EEPROM.write(0, 1);          
  }
  EEPROM.get(2, setL_moisture); 
  EEPROM.get(8, setH_moisture); 
  Serial.print("Da tai nguong do am: "); 
  Serial.print(setL_moisture); Serial.print("% - "); Serial.println(setH_moisture, 1);

  // Đọc và gửi dữ liệu lần đầu
  readAllSensors(); 
  sendDataToEsp32(); 
  lastDataSendTimeToEsp = millis(); 
  lastSensorReadAndLogicTime = millis();
}

void controlMotor(bool turnOn) {
  digitalWrite(motorRelayPin, turnOn ? LOW : HIGH); 
  if (motorState != turnOn) { 
    motorState = turnOn;
    Serial.print("BOM "); Serial.println(motorState ? "BAT" : "TAT");
  }
}

void controlFan(bool turnOn) {
  digitalWrite(fanRelay, turnOn ? LOW : HIGH);
  if (fanState != turnOn) {
    fanState = turnOn;
    Serial.print("QUAT "); Serial.println(fanState ? "BAT" : "TAT");
  }
}

void controlLight(bool turnOn) {
  digitalWrite(lightRelay, turnOn ? LOW : HIGH);
  if (lightState != turnOn) {
    lightState = turnOn;
    Serial.print("DEN "); Serial.println(lightState ? "BAT" : "TAT");
  }
}

float mapRawToPercent(int rawValue, int dryValue, int wetValue) {
  rawValue = constrain(rawValue, wetValue, dryValue); 
  float percent = map(rawValue, dryValue, wetValue, 0, 100); 
  return percent;
}

void readAllSensors() {
  Serial.println("--- Dang doc cam bien ---");
  // Đọc nhiệt độ và độ ẩm không khí
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  if (isnan(temperature) || isnan(humidity)) { 
    Serial.println("Loi doc cam bien DHT!");
    // Giữ giá trị cũ nếu đọc lỗi để tránh ảnh hưởng logic
  } else {
    Serial.print("Nhiet do: "); Serial.print(temperature); Serial.println(" *C");
  }
  
  lightValue = analogRead(LightSensorPin);
  Serial.print("Anh sang (tho): "); Serial.println(lightValue);

  // Đọc 4 cảm biến độ ẩm đất và in ra giá trị để debug
  int rawMoisture1 = analogRead(MOISTURE_SENSOR_PIN_1);
  moisturePercent1 = mapRawToPercent(rawMoisture1, SENSOR_DRY_VALUE_1, SENSOR_WET_VALUE_1);
  Serial.print("Do am 1: "); Serial.print(rawMoisture1); Serial.print(" -> "); Serial.print(moisturePercent1, 1); Serial.println("%");
  
  int rawMoisture2 = analogRead(MOISTURE_SENSOR_PIN_2);
  moisturePercent2 = mapRawToPercent(rawMoisture2, SENSOR_DRY_VALUE_2, SENSOR_WET_VALUE_2);
  Serial.print("Do am 2: "); Serial.print(rawMoisture2); Serial.print(" -> "); Serial.print(moisturePercent2, 1); Serial.println("%");
  
  int rawMoisture3 = analogRead(MOISTURE_SENSOR_PIN_3);
  moisturePercent3 = mapRawToPercent(rawMoisture3, SENSOR_DRY_VALUE_3, SENSOR_WET_VALUE_3);
  Serial.print("Do am 3: "); Serial.print(rawMoisture3); Serial.print(" -> "); Serial.print(moisturePercent3, 1); Serial.println("%");

  int rawMoisture4 = analogRead(MOISTURE_SENSOR_PIN_4);
  moisturePercent4 = mapRawToPercent(rawMoisture4, SENSOR_DRY_VALUE_4, SENSOR_WET_VALUE_4);
  Serial.print("Do am 4: "); Serial.print(rawMoisture4); Serial.print(" -> "); Serial.print(moisturePercent4, 1); Serial.println("%");

  averageMoisturePercent = (moisturePercent1 + moisturePercent2 + moisturePercent3 + moisturePercent4) / 4.0;
  Serial.print("=> Do am TRUNG BINH: "); Serial.print(averageMoisturePercent, 1); Serial.println("%");
  Serial.println("-------------------------");
}

void sendDataToEsp32() {
    String data = "T:" + String(temperature, 1) +
                  ",M1:" + String(moisturePercent1, 0) +
                  ",M2:" + String(moisturePercent2, 0) +
                  ",M3:" + String(moisturePercent3, 0) +
                  ",M4:" + String(moisturePercent4, 0) +
                  ",MAVG:" + String(averageMoisturePercent, 0) +
                  ",L:" + String(lightValue) + 
                  ",MOT:" + String(motorState ? 1 : 0) +
                  ",FAN:" + String(fanState ? 1 : 0) +
                  ",LIT:" + String(lightState ? 1 : 0) +
                  ",AUTO:" + String(manualControl ? 0 : 1);
    espSerial.println(data); 
    Serial.print("Gui dinh ky len ESP32: "); Serial.println(data);
}

// [CẢI TIẾN] Hàm xử lý lệnh từ ESP32 được viết lại để không bị chặn (non-blocking)
void handleEspCommand() {
  static String command_buffer = ""; // Buffer để lưu trữ lệnh đang nhận
  while (espSerial.available() > 0) {
    char c = espSerial.read();
    if (c == '\n') { // Nếu gặp ký tự kết thúc lệnh
      command_buffer.trim(); // Xóa khoảng trắng thừa
      Serial.print("Nhan lenh tu ESP32: ["); Serial.print(command_buffer); Serial.println("]");

      // Xử lý lệnh trong buffer
      if (command_buffer.equals("GET_DATA")) { 
        readAllSensors(); 
        sendDataToEsp32(); 
      } else if (command_buffer.startsWith("SET_L,")) {
        float val = command_buffer.substring(6).toFloat();
        if (val >= 0 && val <= 100) { 
          setL_moisture = val;
          EEPROM.put(2, setL_moisture); 
          Serial.print("Dat nguong thap: "); Serial.println(setL_moisture);
          espSerial.println("OK:SET_L"); 
        } else {
          espSerial.println("ERR:SET_L_VAL"); 
        }
      } else if (command_buffer.startsWith("SET_H,")) {
        float val = command_buffer.substring(6).toFloat();
        if (val >= 0 && val <= 100 && val > setL_moisture) {
          setH_moisture = val;
          EEPROM.put(8, setH_moisture);
          Serial.print("Dat nguong cao: "); Serial.println(setH_moisture);
          espSerial.println("OK:SET_H");
        } else {
          espSerial.println("ERR:SET_H_VAL");
        }
      } else if (command_buffer.equals("MOTOR_ON"))  { manualControl = true; controlMotor(true); espSerial.println("OK:MOTOR_ON"); }
        else if (command_buffer.equals("MOTOR_OFF")) { manualControl = true; controlMotor(false); espSerial.println("OK:MOTOR_OFF");}
        else if (command_buffer.equals("FAN_ON"))    { manualControl = true; controlFan(true); espSerial.println("OK:FAN_ON");    }
        else if (command_buffer.equals("FAN_OFF"))   { manualControl = true; controlFan(false); espSerial.println("OK:FAN_OFF");   }
        else if (command_buffer.equals("LIGHT_ON"))  { manualControl = true; controlLight(true); espSerial.println("OK:LIGHT_ON"); }
        else if (command_buffer.equals("LIGHT_OFF")) { manualControl = true; controlLight(false); espSerial.println("OK:LIGHT_OFF");}
        else if (command_buffer.equals("AUTO_MODE")) { manualControl = false; Serial.println("Chuyen sang che do TU DONG"); espSerial.println("OK:AUTO_MODE"); }
        else { Serial.println("Lenh khong xac dinh"); espSerial.println("ERR:CMD_UNKNOWN"); }

      command_buffer = ""; // Xóa buffer để sẵn sàng nhận lệnh mới
    } else {
      command_buffer += c; // Nối ký tự vào buffer
    }
  }
}

void updateLcdDisplay() {
  lcd.clear(); 
  // Dòng 1
  lcd.setCursor(0, 0); 
  lcd.print("T:"); lcd.print(temperature, 0); lcd.print((char)223); lcd.print(" M:"); lcd.print(averageMoisturePercent, 0); lcd.print("%");
  lcd.setCursor(12, 0);
  lcd.print(manualControl ? "MAN" : "AUT"); 

  // Dòng 2
  lcd.setCursor(0, 1);
  lcd.print("AS:"); lcd.print(lightValue);
  // Tạo chuỗi trạng thái BQD (Bơm, Quạt, Đèn)
  String status = "";
  status += (motorState ? "B" : "-"); 
  status += (fanState   ? "Q" : "-"); 
  status += (lightState ? "D" : "-"); 
  lcd.setCursor(12, 1); 
  lcd.print(status);
}


void runAutomaticControlLogic() {
  if (manualControl) return; 

  // 1. Điều khiển Bơm
  if (averageMoisturePercent < setL_moisture) {
    if (!motorState) controlMotor(true); 
  } else if (averageMoisturePercent > setH_moisture) {
    if (motorState) controlMotor(false);
  }

  // 2. Điều khiển Quạt
  if (temperature > 35.0) { // [SỬA ĐỔI] Giảm ngưỡng nhiệt độ để dễ test
    if (!fanState) controlFan(true);
  } else if (temperature < 30.0) { // Ngưỡng tắt thấp hơn để tránh bật tắt liên tục
    if (fanState) controlFan(false);
  }

  // 3. Điều khiển Đèn
  if (lightValue < 400) {
    if (!lightState) controlLight(true); 
  } else if (lightValue > 800) { 
    if (lightState) controlLight(false);
  }
}

void loop() {
  unsigned long currentTime = millis(); 

  // Tác vụ 1: Đọc cảm biến, chạy logic, cập nhật LCD (định kỳ)
  if (currentTime - lastSensorReadAndLogicTime >= sensorReadAndLogicInterval) {
    readAllSensors(); 
    runAutomaticControlLogic(); 
    updateLcdDisplay(); 
    lastSensorReadAndLogicTime = currentTime; 
  }

  // Tác vụ 2: Gửi dữ liệu lên ESP32 (định kỳ 5 phút)
  if (currentTime - lastDataSendTimeToEsp >= dataSendIntervalToEsp) {
    sendDataToEsp32();
    lastDataSendTimeToEsp = currentTime; 
  }

  // Tác vụ 3: Luôn lắng nghe lệnh từ ESP32
  handleEspCommand();
}