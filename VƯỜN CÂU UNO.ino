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

// Giá trị hiệu chuẩn cảm biến (QUAN TRỌNG: Cần hiệu chuẩn cho từng cảm biến)
// Cảm biến điện dung: giá trị analog cao khi khô, thấp khi ẩm (CẦN ĐO THỰC TẾ)
const int SENSOR_DRY_VALUE_1 = 750; const int SENSOR_WET_VALUE_1 = 350;
const int SENSOR_DRY_VALUE_2 = 760; const int SENSOR_WET_VALUE_2 = 360;
const int SENSOR_DRY_VALUE_3 = 740; const int SENSOR_WET_VALUE_3 = 340;
const int SENSOR_DRY_VALUE_4 = 755; const int SENSOR_WET_VALUE_4 = 355;

#define LightSensorPin A5 // Chân cảm biến ánh sáng (quang trở)

// --- Cấu hình LCD I2C ---
LiquidCrystal_I2C lcd(0x27, 16, 2); // Địa chỉ I2C của LCD là 0x27, kích thước 16 cột x 2 dòng

// --- Chân điều khiển Relay ---
#define motorRelayPin 8  // Relay cho Bơm
#define fanRelay 6       // Relay cho Quạt
#define lightRelay 2     // Relay cho Đèn

// --- Software Serial Communication with ESP32 ---
#define ESP_RX_PIN 12  // Chân RX của Arduino (nối với TX của ESP32)
#define ESP_TX_PIN 13  // Chân TX của Arduino (nối với RX của ESP32)
SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN); // Tạo cổng serial ảo tên là espSerial

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
const unsigned long dataSendIntervalToEsp = 30UL * 60UL * 1000UL; // Khoảng thời gian gửi: 30 phút

unsigned long lastSensorReadAndLogicTime = 0; // Thời điểm cuối cùng đọc cảm biến và chạy logic
const unsigned long sensorReadAndLogicInterval = 1000UL; // Khoảng thời gian: 1 giây cho logic/LCD

void setup() {
  Serial.begin(9600);       // Mở cổng Serial cứng để debug (xem trên Serial Monitor)
  espSerial.begin(9600);    // Mở cổng Serial ảo để giao tiếp với ESP32
  Serial.println("Vườn cây thông minh -- Ready (Periodic Send)");

  // Cài đặt các chân Relay là OUTPUT
  pinMode(motorRelayPin, OUTPUT);
  pinMode(fanRelay, OUTPUT);
  pinMode(lightRelay, OUTPUT);

  // Đặt trạng thái ban đầu cho các Relay (TẮT)
  // Giả sử Relay kích LOW là ON, nên HIGH là TẮT
  digitalWrite(motorRelayPin, HIGH); motorState = false;
  digitalWrite(fanRelay, HIGH);      fanState = false;
  digitalWrite(lightRelay, HIGH);    lightState = false;

  // Khởi tạo LCD
  Wire.begin();       // Bắt đầu giao tiếp I2C
  lcd.init();         // Khởi tạo LCD
  lcd.backlight();    // Bật đèn nền LCD
  lcd.clear();        // Xóa màn hình LCD
  lcd.print("Vườn cây");
  delay(1000);
  lcd.setCursor(0, 1); // Di chuyển con trỏ đến dòng 1, cột 0
  lcd.print("dang-ket-noi...");
  delay(1500);
  Serial.println("LCD-dang-ket-noi...");

  // Khởi tạo cảm biến DHT
  dht.begin();
  Serial.println("DHT-dang-ket-noi....");

  // Đọc ngưỡng từ EEPROM (bộ nhớ không bị mất khi tắt nguồn)
  if (EEPROM.read(0) != 1) {  // Kiểm tra cờ ở địa chỉ 0, nếu chưa có thì ghi giá trị mặc định
    Serial.println("Writing default thresholds to EEPROM");
    EEPROM.put(2, setL_moisture);  // Lưu ngưỡng thấp vào địa chỉ 2
    EEPROM.put(8, setH_moisture);  // Lưu ngưỡng cao vào địa chỉ 8
    EEPROM.write(0, 1);            // Đặt cờ là đã ghi
  }
  EEPROM.get(2, setL_moisture);  // Đọc ngưỡng thấp từ địa chỉ 2
  EEPROM.get(8, setH_moisture);  // Đọc ngưỡng cao từ địa chỉ 8
  Serial.print("load... ngưỡng độ ẩm thấp: "); Serial.println(setL_moisture);
  Serial.print("load... ngưỡng độ ẩm cao: "); Serial.println(setH_moisture);

  // Đọc và gửi dữ liệu lần đầu khi khởi động
  readAllSensors(); // Đọc tất cả các cảm biến
  sendDataToEsp32(); // Gửi dữ liệu đã đọc lên ESP32
  lastDataSendTimeToEsp = millis(); // Cập nhật thời điểm gửi cuối cùng
  lastSensorReadAndLogicTime = millis(); // Cập nhật thời điểm logic cuối cùng
}

// --- Hàm điều khiển thiết bị qua Relay ---
// Giả định: Relay được kích hoạt (ON) khi chân điều khiển ở mức LOW
void controlMotor(bool turnOn) { // Hàm điều khiển bơm
  digitalWrite(motorRelayPin, turnOn ? LOW : HIGH); // Ghi LOW để BẬT, HIGH để TẮT
  if (motorState != turnOn) { // Chỉ cập nhật và in nếu trạng thái thực sự thay đổi
    motorState = turnOn;
    Serial.print("Motor "); Serial.println(motorState ? "ON" : "OFF");
  }
}

void controlFan(bool turnOn) { // Hàm điều khiển quạt
  digitalWrite(fanRelay, turnOn ? LOW : HIGH);
  if (fanState != turnOn) {
    fanState = turnOn;
    Serial.print("Fan "); Serial.println(fanState ? "ON" : "OFF");
  }
}

void controlLight(bool turnOn) { // Hàm điều khiển đèn
  digitalWrite(lightRelay, turnOn ? LOW : HIGH);
  if (lightState != turnOn) {
    lightState = turnOn;
    Serial.print("Light "); Serial.println(lightState ? "ON" : "OFF");
  }
}

// --- Hàm đọc và chuyển đổi giá trị cảm biến độ ẩm ---
float mapRawToPercent(int rawValue, int dryValue, int wetValue) {
  // Cảm biến điện dung thường cho giá trị analog cao khi khô, thấp khi ẩm
  rawValue = constrain(rawValue, wetValue, dryValue); // Giới hạn giá trị đọc được trong khoảng hiệu chuẩn
  float percent = map(rawValue, dryValue, wetValue, 0, 100); // Ánh xạ ngược giá trị thô sang 0-100%
  return percent;
}

// --- Hàm đọc TẤT CẢ các giá trị cảm biến ---
void readAllSensors() {
  // Đọc nhiệt độ và độ ẩm không khí
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  if (isnan(temperature) || isnan(humidity)) { // Kiểm tra nếu đọc lỗi
    Serial.println("không đọc được giá trị từ cảm biến DHT!");
  }
  // Đọc cảm biến ánh sáng
  lightValue = analogRead(LightSensorPin);
  // Đọc 4 cảm biến độ ẩm đất
  moisturePercent1 = mapRawToPercent(analogRead(MOISTURE_SENSOR_PIN_1), SENSOR_DRY_VALUE_1, SENSOR_WET_VALUE_1);
  moisturePercent2 = mapRawToPercent(analogRead(MOISTURE_SENSOR_PIN_2), SENSOR_DRY_VALUE_2, SENSOR_WET_VALUE_2);
  moisturePercent3 = mapRawToPercent(analogRead(MOISTURE_SENSOR_PIN_3), SENSOR_DRY_VALUE_3, SENSOR_WET_VALUE_3);
  moisturePercent4 = mapRawToPercent(analogRead(MOISTURE_SENSOR_PIN_4), SENSOR_DRY_VALUE_4, SENSOR_WET_VALUE_4);
  // Tính độ ẩm trung bình
  averageMoisturePercent = (moisturePercent1 + moisturePercent2 + moisturePercent3 + moisturePercent4) / 4.0;
}


// --- Hàm GỬI chuỗi dữ liệu đã định dạng lên ESP32 ---
void sendDataToEsp32() {
    // Tạo chuỗi dữ liệu theo định dạng "KEY:VALUE,KEY:VALUE,..."
    String data = "T:" + String(temperature, 1) +  // Nhiệt độ, 1 chữ số thập phân
                  ",M1:" + String(moisturePercent1, 0) + // Độ ẩm 1, 0 chữ số thập phân
                  ",M2:" + String(moisturePercent2, 0) + // Độ ẩm 2
                  ",M3:" + String(moisturePercent3, 0) + // Độ ẩm 3
                  ",M4:" + String(moisturePercent4, 0) + // Độ ẩm 4
                  ",MAVG:" + String(averageMoisturePercent, 0) + // Độ ẩm trung bình
                  ",L:" + String(lightValue) + // Giá trị ánh sáng thô
                  ",MOT:" + String(motorState ? 1 : 0) + // Trạng thái bơm (1=ON, 0=OFF)
                  ",FAN:" + String(fanState ? 1 : 0) +   // Trạng thái quạt
                  ",LIT:" + String(lightState ? 1 : 0) +  // Trạng thái đèn
                  ",AUTO:" + String(manualControl ? 0 : 1); // Chế độ (1=AUTO, 0=MANUAL)
    espSerial.println(data); // Gửi chuỗi dữ liệu qua SoftwareSerial tới ESP32
    Serial.print("Periodically Sent to ESP32: "); Serial.println(data); // In ra Serial Monitor để debug
}


// --- Hàm xử lý lệnh nhận được từ ESP32 ---
void handleEspCommand() {
  if (espSerial.available() > 0) { // Nếu có dữ liệu từ ESP32 gửi đến
    String command = espSerial.readStringUntil('\n'); // Đọc chuỗi lệnh cho đến khi gặp ký tự xuống dòng
    command.trim(); // Xóa khoảng trắng thừa ở đầu và cuối lệnh
    Serial.print("ESP32 cmd: ["); Serial.print(command); Serial.println("]"); // In lệnh nhận được để debug

    if (command.equals("GET_DATA")) { // Nếu ESP32 yêu cầu dữ liệu
      readAllSensors(); // Đọc lại cảm biến để có dữ liệu mới nhất
      sendDataToEsp32(); // Gửi dữ liệu đi
    } else if (command.startsWith("SET_L,")) { // Nếu lệnh là cài đặt ngưỡng thấp (ví dụ: "SET_L,25.5")
      float val = command.substring(6).toFloat(); // Lấy giá trị sau "SET_L," và chuyển thành số thực
      if (val >= 0 && val <= 100) { // Kiểm tra giá trị hợp lệ
        setL_moisture = val;
        EEPROM.put(2, setL_moisture); // Lưu vào EEPROM
        Serial.print("Set Low Moisture to: "); Serial.println(setL_moisture);
        espSerial.println("OK:SET_L"); // Gửi phản hồi thành công cho ESP32
      } else {
        espSerial.println("ERR:SET_L_VAL"); // Gửi phản hồi lỗi
      }
    } else if (command.startsWith("SET_H,")) { // Tương tự cho cài đặt ngưỡng cao
      float val = command.substring(6).toFloat();
      if (val >= 0 && val <= 100 && val > setL_moisture) { // Ngưỡng cao phải lớn hơn ngưỡng thấp
        setH_moisture = val;
        EEPROM.put(8, setH_moisture);
        Serial.print("Set High Moisture to: "); Serial.println(setH_moisture);
        espSerial.println("OK:SET_H");
      } else {
        espSerial.println("ERR:SET_H_VAL");
      }
    } else if (command.equals("MOTOR_ON")) { // Nếu lệnh bật bơm
      manualControl = true; // Chuyển sang chế độ thủ công
      controlMotor(true);   // Bật bơm
      espSerial.println("OK:MOTOR_ON");
    } else if (command.equals("MOTOR_OFF")) { // Tắt bơm
      manualControl = true; controlMotor(false); espSerial.println("OK:MOTOR_OFF");
    } else if (command.equals("FAN_ON")) {    // Bật quạt
      manualControl = true; controlFan(true); espSerial.println("OK:FAN_ON");
    } else if (command.equals("FAN_OFF")) {   // Tắt quạt
      manualControl = true; controlFan(false); espSerial.println("OK:FAN_OFF");
    } else if (command.equals("LIGHT_ON")) {  // Bật đèn
      manualControl = true; controlLight(true); espSerial.println("OK:LIGHT_ON");
    } else if (command.equals("LIGHT_OFF")) { // Tắt đèn
      manualControl = true; controlLight(false); espSerial.println("OK:LIGHT_OFF");
    } else if (command.equals("AUTO_MODE")) { // Chuyển sang chế độ tự động
      manualControl = false; Serial.println("Switched to Auto Mode"); espSerial.println("OK:AUTO_MODE");
    } else { // Nếu không nhận dạng được lệnh
      Serial.println("Unknown command"); espSerial.println("ERR:CMD_UNKNOWN");
    }
  }
}

// --- Hàm cập nhật thông tin hiển thị trên màn hình LCD ---
void updateLcdDisplay() {
  lcd.clear(); // Xóa nội dung cũ trên LCD
  // Dòng 1: Nhiệt độ (T), Độ ẩm trung bình (M), Chế độ (AUT/MAN)
  lcd.setCursor(0, 0); // Đặt con trỏ về đầu dòng 1
  lcd.print("T:"); lcd.print(temperature, 0); lcd.print((char)223); lcd.print("C "); // (char)223 là ký tự độ °
  lcd.print("M:"); lcd.print(averageMoisturePercent, 0); lcd.print("%");
  lcd.setCursor(12, 0); // Vị trí góc phải dòng 1
  lcd.print(manualControl ? "MAN" : "AUT"); // Hiển thị MAN nếu thủ công, AUT nếu tự động

  // Dòng 2: Ánh sáng (AS), Trạng thái thiết bị (Bơm, Quạt, Đèn)
  lcd.setCursor(0, 1); // Đặt con trỏ về đầu dòng 2
  lcd.print("AS:"); lcd.print(lightValue); // Hiển thị giá trị ánh sáng thô
  // Tạo chuỗi trạng thái thiết bị
  String status = "";
  status += (motorState ? "B" : "-"); // Bơm: B nếu ON, - nếu OFF
  status += (fanState   ? "Q" : "-"); // Quạt: Q nếu ON, - nếu OFF
  status += (lightState ? "D" : "-"); // Đèn: D nếu ON, - nếu OFF
  lcd.setCursor(16 - status.length() - 2, 1); // Căn lề phải cho chuỗi trạng thái
  lcd.print(" "); // Khoảng cách
  lcd.print(status); // Hiển thị trạng thái
}

// --- Hàm thực thi logic điều khiển tự động ---
void runAutomaticControlLogic() {
  if (manualControl) return; // Nếu đang ở chế độ thủ công thì không làm gì cả

  // 1. Điều khiển Bơm dựa trên độ ẩm đất trung bình
  if (averageMoisturePercent < setL_moisture) { // Nếu độ ẩm dưới ngưỡng thấp
    if (!motorState) controlMotor(true);        // Mà bơm đang tắt -> Bật bơm
  } else if (averageMoisturePercent > setH_moisture) { // Nếu độ ẩm trên ngưỡng cao
    if (motorState) controlMotor(false);       // Mà bơm đang bật -> Tắt bơm
  }

  // 2. Điều khiển Quạt dựa trên nhiệt độ không khí
  if (temperature > 38.0) { // Nếu nhiệt độ quá cao (ví dụ > 38°C)
    if (!fanState) controlFan(true);  // Mà quạt đang tắt -> Bật quạt
  } else if (temperature < 28.0) { // Nếu nhiệt độ đã giảm (ví dụ < 28°C - tạo khoảng trễ)
    if (fanState) controlFan(false); // Mà quạt đang bật -> Tắt quạt
  }

  // 3. Điều khiển Đèn dựa trên cảm biến ánh sáng
  if (lightValue < 400) { // Nếu trời tối (giá trị ánh sáng thấp, ví dụ < 400)
    if (!lightState) controlLight(true); // Mà đèn đang tắt -> Bật đèn
  } else if (lightValue > 800) { // Nếu trời đủ sáng (giá trị ánh sáng cao, ví dụ > 800)
    if (lightState) controlLight(false); // Mà đèn đang bật -> Tắt đèn
  }
}

// --- Hàm lặp chính của chương trình ---
void loop() {
  unsigned long currentTime = millis(); // Lấy thời gian hiện tại (số mili giây từ khi Arduino khởi động)

  // Tác vụ 1: Đọc cảm biến, chạy logic tự động, cập nhật LCD (thực hiện mỗi `sensorReadAndLogicInterval`)
  if (currentTime - lastSensorReadAndLogicTime >= sensorReadAndLogicInterval) {
    readAllSensors(); // Đọc tất cả giá trị cảm biến mới
    runAutomaticControlLogic(); // Chạy logic điều khiển tự động
    updateLcdDisplay(); // Cập nhật màn hình LCD
    lastSensorReadAndLogicTime = currentTime; // Cập nhật thời điểm thực hiện tác vụ này
  }

  // Tác vụ 2: Gửi dữ liệu lên ESP32 định kỳ (thực hiện mỗi `dataSendIntervalToEsp`)
  if (currentTime - lastDataSendTimeToEsp >= dataSendIntervalToEsp) {
    // Tùy chọn: Gọi readAllSensors() lại ở đây nếu muốn đảm bảo dữ liệu gửi đi là mới nhất ngay tại thời điểm gửi.
    // Thường thì không cần nếu sensorReadAndLogicInterval đủ nhỏ.
    // readAllSensors();
    sendDataToEsp32(); // Gửi dữ liệu lên ESP32
    lastDataSendTimeToEsp = currentTime; // Cập nhật thời điểm thực hiện tác vụ này
  }

  // Tác vụ 3: Luôn lắng nghe và xử lý lệnh từ ESP32
  handleEspCommand();

  // Không cần delay lớn ở cuối loop() vì đã sử dụng millis() để quản lý thời gian
}
