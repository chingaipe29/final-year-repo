
#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// WiFi credentials
const char* ssid = "WWW.et";
const char* password = "123456788";

// Django API endpoint
const char* apiEndpoint = "http://192.168.137.204:8000/api/gps-data/";

// GPS setup
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);  // UART2 (RX=16, TX=17)
#define INDICATOR_LED 13

// Geofence config
float fenceLat = -15.391967;
float fenceLon = 28.330280;
float radiusMeters = 1000000.0;

unsigned long lastSendTime = 0;

void sendToAPI(float lat, float lon, float speed, float alt, int hour, int minute, int second);
void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);
  pinMode(INDICATOR_LED, OUTPUT);
  
  Serial.println("\n🔍 Starting GPS Tracker...");
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Wi-Fi Connected");
}

void loop() {
  // Read and process GPS data
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    
    // Debug: Echo raw GPS data (comment out if too verbose)
    // Serial.write(c); 
    
    if (gps.encode(c)) {
      digitalWrite(INDICATOR_LED, HIGH);
      delay(50);
      digitalWrite(INDICATOR_LED, LOW);
    }
  }

  // Process and send data every 5 seconds
  if (gps.location.isUpdated() && millis() - lastSendTime > 5000) {
    lastSendTime = millis();

    // Get GPS data
    float lat = gps.location.lat();
    float lon = gps.location.lng();
    float speed = gps.speed.kmph();
    float alt = gps.altitude.meters();
    int hour = gps.time.hour();
    int minute = gps.time.minute();
    int second = gps.time.second();

    // Print debug info
    Serial.println("\n📍 GPS Fix:");
    Serial.print("Chars processed: "); Serial.println(gps.charsProcessed());
    Serial.print("Satellites: "); Serial.println(gps.satellites.value());
    Serial.print("Latitude: "); Serial.println(lat, 6);
    Serial.print("Longitude: "); Serial.println(lon, 6);
    Serial.print("Speed: "); Serial.print(speed); Serial.println(" km/h");
    Serial.print("Altitude: "); Serial.print(alt); Serial.println(" m");
    Serial.print("Time: ");
    Serial.printf("%02d:%02d:%02d\n", hour, minute, second);

    // Geofence check
    float distance = TinyGPSPlus::distanceBetween(lat, lon, fenceLat, fenceLon);
    Serial.print("Distance from center: ");
    Serial.print(distance); Serial.println(" meters");
    Serial.println(distance > radiusMeters ? "⚠️ OUTSIDE GEOFENCE!" : "✅ Inside geofence");

    // Send to Django server
    sendToAPI(lat, lon, speed, alt, hour, minute, second);
  }
}

void sendToAPI(float lat, float lon, float speed, float alt, int hour, int minute, int second) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;

    http.begin(client, apiEndpoint);
    http.addHeader("Content-Type", "application/json");

    // Create JSON payload
    String timestamp = "2024-" + String(gps.date.month()) + "-" + String(gps.date.day()) + 
                      " " + String(hour) + ":" + String(minute) + ":" + String(second);
    
    String json = "{";
    json += "\"device_id\":\"esp32_001\",";
    json += "\"timestamp\":\"" + timestamp + "\",";
    json += "\"latitude\":" + String(lat, 6) + ",";
    json += "\"longitude\":" + String(lon, 6) + ",";
    json += "\"speed\":" + String(speed, 2) + ",";
    json += "\"altitude\":" + String(alt, 2);
    json += "}";

    Serial.println("📤 Sending to server:");
    Serial.println(json);

    int httpCode = http.POST(json);
    String response = http.getString();

    Serial.print("HTTP status: ");
    Serial.println(httpCode);
    Serial.print("Response: ");
    Serial.println(response);

    http.end();
  } else {
    Serial.println("❌ Wi-Fi disconnected - attempting to reconnect...");
    WiFi.reconnect();
  }
}
