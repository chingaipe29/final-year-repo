#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>
#include <LiquidCrystal.h>
#include <HTTPClient.h>

// -------- DHT11 Settings --------
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// -------- LCD Settings --------
LiquidCrystal lcd(27, 26, 14, 12, 13, 15);

// -------- RTC Settings --------
RTC_DS3231 rtc;

// -------- WiFi Credentials --------
const char* WIFI_SSID = "A20s";
const char* WIFI_PASSWORD = "amue9397";

// -------- Django Server Configuration --------
const char* DJANGO_SERVER_URL = "http://172.16.50.189:8000/api/sensor-data/";

unsigned long lastSendTime = 0;
const unsigned long sendInterval = 15000; // 15 seconds

void setup() {
  Serial.begin(115200);

  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Initializing...");

  dht.begin();

  Wire.begin(21, 22); // SDA, SCL
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    lcd.setCursor(0, 1);
    lcd.print("RTC not found!");
    while (true);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retry++;
    if (retry > 30) {
      lcd.setCursor(0, 1);
      lcd.print("WiFi Failed!");
      while (true);
    }
  }

  lcd.clear();
  lcd.print("WiFi Connected!");
  Serial.println("WiFi connected. IP: " + WiFi.localIP().toString());
  delay(2000);
}

void loop() {
  if (millis() - lastSendTime >= sendInterval || lastSendTime == 0) {
    lastSendTime = millis();

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor!");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("DHT Error");
      delay(3000);
      return;
    }

    DateTime now = rtc.now();
    char timestampStr[20];
    sprintf(timestampStr, "%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Temp: ");
    lcd.print(temperature, 1);
    lcd.print("C");

    lcd.setCursor(0, 1);
    lcd.print("Hum: ");
    lcd.print(humidity, 1);
    lcd.print("%");

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(DJANGO_SERVER_URL);
      http.addHeader("Content-Type", "application/json");

      String payload = "{";
      payload += "\"temperature\": " + String(temperature, 1) + ",";
      payload += "\"humidity\": " + String(humidity, 1) + ",";
      payload += "\"timestamp\": \"" + String(timestampStr) + "\"";
      payload += "}";

      Serial.println("Sending payload: " + payload);

      int httpCode = http.POST(payload);

      if (httpCode > 0) {
        Serial.print("HTTP Code: ");
        Serial.println(httpCode);
        String response = http.getString();
        Serial.println("Server Response: " + response);
      } else {
        Serial.print("Failed to send data: ");
        Serial.println(http.errorToString(httpCode));
      }

      http.end();
    } else {
      Serial.println("WiFi not connected");
    }
  }

  delay(100);
}