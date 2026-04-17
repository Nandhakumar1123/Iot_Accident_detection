#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Wire.h>
#include <math.h>

// ---------------- WIFI ----------------
const char* ssid = "MI";
const char* password = "12345678";

// ---------------- TELEGRAM ----------------
String botToken = "8641117658:AAEFOlVjo85IqUr6sCUzZG2bEan52L39S00";
String chatID = "1792879382";

// ---------------- PINS ----------------
const int shockPin = A0;
const int soundPin = D5;
const int buttonPin = D6;   // CHANGED from D0 to D6
const int ledPin = D7;

// ---------------- MPU6050 ----------------
const int MPU_ADDR = 0x68;

// ---------------- VARIABLES ----------------
WiFiClientSecure client;

bool accidentTriggered = false;
bool systemLocked = false;

float prevAccel = 1.0;
int startupCounter = 0;
int lockMessageCount = 0;

// ---------------- TIME ----------------
const char* ntpServer = "pool.ntp.org";
long gmtOffset_sec = 19800;
int daylightOffset_sec = 0;

// ---------------- TIME FUNCTION ----------------
String getTime() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  char buffer[20];
  sprintf(buffer, "%02d:%02d:%02d",
          timeinfo->tm_hour,
          timeinfo->tm_min,
          timeinfo->tm_sec);

  return String(buffer);
}

// ---------------- TELEGRAM ----------------
void sendTelegram(String message) {
  client.setInsecure();

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Telegram connection failed");
    return;
  }

  String url = "/bot" + botToken +
               "/sendMessage?chat_id=" + chatID +
               "&text=" + message;

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: api.telegram.org\r\n" +
               "Connection: close\r\n\r\n");

  delay(100);
  client.stop();
  
  Serial.println(">>> TELEGRAM ALERT SENT <<<");
}

// ---------------- BUTTON ----------------
bool isButtonPressed() {
  return digitalRead(buttonPin) == LOW;
}

// ---------------- MPU READ ----------------
void readMPU(int16_t &ax, int16_t &ay, int16_t &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  ax = Wire.read() << 8 | Wire.read();
  ay = Wire.read() << 8 | Wire.read();
  az = Wire.read() << 8 | Wire.read();
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  pinMode(shockPin, INPUT);
  pinMode(soundPin, INPUT);

  // FIXED: internal pull-up to avoid floating
  pinMode(buttonPin, INPUT_PULLUP);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  Wire.begin(D2, D1);

  // Wake MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  delay(3000);
}

// ---------------- LOOP ----------------
void loop() {

  // ---------------- LOCK MODE ----------------
  if (systemLocked) {

    if (lockMessageCount < 2) {
      Serial.println(">>> SYSTEM LOCKED <<<");
      Serial.println("Press button to reset");
      lockMessageCount++;
    }

    if (isButtonPressed()) {
      systemLocked = false;
      accidentTriggered = false;
      lockMessageCount = 0;
      digitalWrite(ledPin, LOW);

      Serial.println(">>> SYSTEM RESET <<<");
      delay(2000);
    }

    delay(300);
    return;
  }

  // ---------------- STARTUP IGNORE ----------------
  if (startupCounter < 3) {
    startupCounter++;
    delay(1000);
    return;
  }

  // ---------------- SENSOR READINGS ----------------
  int shockValue = analogRead(shockPin);
  int soundState = digitalRead(soundPin);

  int16_t ax, ay, az;
  readMPU(ax, ay, az);

  float currentAccel = sqrt(
      (float)ax * ax +
      (float)ay * ay +
      (float)az * az
  ) / 16384.0;

  float accelChange = abs(currentAccel - prevAccel);
  prevAccel = currentAccel;

  float tiltAngle = atan2((float)ay, (float)az) * 180.0 / PI;

  String soundStatus = (soundState == LOW) ? "DETECTED" : "NORMAL";

  Serial.println("\n------ SENSOR VALUES ------");
  Serial.print("Shock: "); Serial.println(shockValue);
  Serial.print("Sound: "); Serial.println(soundStatus);
  Serial.print("Accel Change: "); Serial.println(accelChange);
  Serial.print("Tilt Angle: "); Serial.println(tiltAngle);

  int score = 0;

  // Better thresholds
  if (shockValue > 50) score++;
  if (soundState == LOW) score++;
  if (accelChange > 1.0) score++;
  if (abs(tiltAngle) > 25) score++;

  Serial.print("Score: ");
  Serial.println(score);

  // ---------------- ACCIDENT DETECTION ----------------
  if (score >= 2 && !accidentTriggered) {

    accidentTriggered = true;
    digitalWrite(ledPin, HIGH);

    Serial.println(">>> ACCIDENT DETECTED <<<");

    bool cancelled = false;
    unsigned long startTime = millis();
    unsigned long lastPrint = 0;

    while (millis() - startTime < 15000) {

      int remaining = 15 - ((millis() - startTime) / 1000);

      if (millis() - lastPrint >= 1000) {
        Serial.print("Cancel within ");
        Serial.print(remaining);
        Serial.println(" sec");
        lastPrint = millis();
      }

      if (isButtonPressed()) {
        cancelled = true;
        Serial.println(">>> ALERT CANCELLED <<<");
        break;
      }

      delay(50);
      yield();
    }

    if (!cancelled) {
      String msg = "Accident Detected!\n";
      msg += "Location: https://maps.google.com/?q=13.0827,80.2707\n";
      msg += "Time: " + getTime();

      msg.replace(" ", "%20");
      msg.replace("\n", "%0A");

      sendTelegram(msg);
    }

    digitalWrite(ledPin, LOW);
    systemLocked = true;

    Serial.println(">>> SYSTEM LOCKED <<<");
  }

  // 5 sec pause for easy monitoring
  delay(5000);
}