#include <WiFi.h>
#include <DHT.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>  // Include the ESP32 servo library
#define TINY_GSM_MODEM_SIM900
#include <TinyGsmClient.h>  // GSM library
#include <HardwareSerial.h>

// DHT setup
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);


#define RX1 26
#define TX1 27
#define RXD2 16
#define TXD2 17

HardwareSerial SerialMega(1);

// Relay (bulb)
#define RELAY_PIN 21
bool bulbState = false;

// Servo Motor
#define SERVO_PIN 12
Servo doorServo;  // Create servo object

// RFID (RC522)
#define RST_PIN 22
#define SS_PIN 5
MFRC522 mfrc522(SS_PIN, RST_PIN);

#define PWR_PIN 4      // For power management

// WiFi credentials
const char* ssid = "sysCall";
const char* password = "00000000";

const int buzzerPin = 14;
const int grantedBeepDuration = 300; // ms for access granted
const int deniedBeepDuration = 1000; // ms for access denied

#define GRANTED_LED 13
#define DENIED_LED 33
TinyGsm modem(Serial2);

bool gsmInitialized = false;

const char* ADMIN_NUMBER = "+260970846745";

// Django URLs
const char* djangoSensorUrl = "http://192.168.137.230:8000/api/sensor-data/";
const char* djangoAuthUrl = "http://192.168.137.230:8000/api/check-auth/";
const char* djangoRfidUrl = "http://192.168.137.230:8000/api/check-auth/";

unsigned long postInterval = 10000;
unsigned long lastPostTime = 0;
unsigned long lastSmsCheck = 0;
const long smsCheckInterval = 30000;  // Check for SMS every 30 seconds
bool systemEnabled = true;  // Controls overall system state


// WebSocket server
WebSocketsServer webSocket(81);

// Servo positions
const int SERVO_LOCKED_POS = 0;    // 0 degrees (locked position)
const int SERVO_UNLOCKED_POS = 90; // 90 degrees (unlocked position)
const int UNLOCK_DURATION = 3000;  // 3 seconds unlocked

// --- Function Prototypes ---
void connectWiFi();
void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t *payload, size_t length);
void handleSerialFromMega();
void handleRFID();
void postDataToDjango(float t, float h);
void checkPasswordWithDjango(String pass);
void checkRFIDWithDjango(String uid);
void unlockDoor();
void lockDoor();
void initGSM();
void sendSMS(String number, String message);
void processSMSCommands();
void handleSystemShutdown();
void handleSystemRestart();
void sendAccessAlert(String method, String identifier, bool granted);
void processCommand(String cmd);

void setup() {
  Serial.begin(115200);       // PC

   
    // Initialize GSM
  Serial2.begin(9600, SERIAL_8N1,  RXD2, TXD2);  // Initialize Serial1
  SerialMega.begin(9600, SERIAL_8N1, 26, 27);
   delay(1000);  // Wait for serial port to initialize
  
  // Test serial communication
  Serial.println("Testing GSM serial connection...");
  Serial2.println("AT");
  delay(1000);
  while (Serial2.available()) {
    Serial.write(Serial2.read());                                                                                                                              
  }
  

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
   pinMode(GRANTED_LED, OUTPUT);
  pinMode(DENIED_LED, OUTPUT);

  // Initialize servo
  doorServo.attach(SERVO_PIN);
  lockDoor(); // Start with door locked

  dht.begin();
  connectWiFi();

  SPI.begin(18,19,23,SS_PIN);
  mfrc522.PCD_Init();
  Serial.println("RFID Reader Ready!");

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
 
}

void loop() {
  webSocket.loop();
  handleSerialFromMega();
  handleRFID();

  // Check for SMS commands periodically
  unsigned long currentMillis = millis();
  if (currentMillis - lastSmsCheck > smsCheckInterval) {
    lastSmsCheck = currentMillis;
    processSMSCommands();
  }

  // Regular sensor data posting
  if (currentMillis - lastPostTime >= postInterval) {
    lastPostTime = currentMillis;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      postDataToDjango(t, h);

      // Send to Arduino Mega
      SerialMega.print("TEMP:");
      SerialMega.print(t);
      SerialMega.print(",HUM:");
      SerialMega.println(h);
                               }
  }
}
void sendSMS(String number, String message) {
  if (!gsmInitialized) {
    Serial.println("GSM not initialized. SMS not sent.");
    return;
  }

  modem.sendSMS(number, message);
  Serial.print("SMS sent to ");
  Serial.print(number);
  Serial.print(": ");
  Serial.println(message);
}

void initGSM() {
  Serial.println("Initializing GSM module...");
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  modem.init();
  // 1. Hard reset the module
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, LOW);
  delay(1200);  // 1.2s power pulse
  digitalWrite(PWR_PIN, HIGH);
  delay(8000);  // Wait 8s for boot

  // 2. Test basic communication
  Serial.println("Testing modem response...");
  Serial2.println("AT"); // Send test command
  delay(1000);
  
  // Check for response
  if (Serial2.available()) {
    String response = Serial2.readString();
    Serial.print("Modem response: ");
    Serial.println(response);
    
    if (response.indexOf("OK") >= 0) {
      Serial.println("Modem is responding");
    } else {
      Serial.println("No valid response from modem");
      return;
    }
  } else {
    Serial.println("No response from modem - check wiring");
    return;
  }

  // 3. Configure module settings
  Serial.println("Configuring modem...");
  modem.sendAT("+CFUN=1");  // Set full functionality
  modem.waitResponse();
  
  modem.sendAT("+CMGF=1");  // Text mode
  modem.waitResponse();
  
  modem.sendAT("+CNMI=2,1,0,0,0");  // SMS notifications
  modem.waitResponse();
  
  modem.sendAT("+CPMS=\"SM\",\"SM\",\"SM\"");  // SMS storage
  modem.waitResponse();

  // 4. Wait for network
  Serial.println("Waiting for network...");
  if (!modem.waitForNetwork(180000L)) {  // 3 minute timeout
    Serial.println("Failed to register on network");
    return;
  }

  // 5. Final checks
  Serial.print("Signal quality: ");
  Serial.println(modem.getSignalQuality());
  
  gsmInitialized = true;
  Serial.println("GSM initialized successfully");
  sendSMS(ADMIN_NUMBER, "System initialized and ready");
}
void processSMSCommands() {
  if (!gsmInitialized) {
    Serial.println("[SMS] GSM not initialized");
    return;
  }

  // First check for new messages in real-time
  while (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    
    if (line.startsWith("+CMT:")) {
      // Parse sender number
      int numStart = line.indexOf('"') + 1;
      int numEnd = line.indexOf('"', numStart);
      String sender = line.substring(numStart, numEnd);
      
      // Read message content (next line)
      String content = Serial2.readStringUntil('\n');
      content.trim();
      content.toUpperCase();
           
      Serial.print("[SMS] New message from ");
      Serial.print(sender);
      Serial.print(": ");
      Serial.println(content);

      // Process if from admin
      if (sender == ADMIN_NUMBER || sender == ("+26" + String(ADMIN_NUMBER).substring(1)) ){
        processCommand(content);
      }
    }
  }

  // Then check stored messages
  modem.sendAT("+CMGL=\"ALL\"");
  String response = "";
  unsigned long startTime = millis();
  
  while (millis() - startTime < 5000) {
    while (Serial2.available()) {
      char c = Serial2.read();
      response += c;
    }
    if (response.endsWith("OK\r\n")) break;
  }

  // Improved SMS parsing
  int index = 0;
  while ((index = response.indexOf("+CMGL:", index)) != -1) {
    // Parse message index
    int msgIndex = response.substring(index + 6, response.indexOf(',', index)).toInt();
    
    // Parse status (READ/UNREAD)
    int statusStart = response.indexOf(',', index) + 1;
    int statusEnd = response.indexOf(',', statusStart);
    String status = response.substring(statusStart, statusEnd);
    status.replace("\"", "");
    
    // Parse sender number
    int numStart = response.indexOf('"', statusEnd) + 1;
    int numEnd = response.indexOf('"', numStart);
    String sender = response.substring(numStart, numEnd);
    
    // Parse timestamp (skip)
    int timeStart = response.indexOf('"', numEnd) + 1;
    int timeEnd = response.indexOf('"', timeStart);
    
    // Parse message content
    int contentStart = response.indexOf('\n', timeEnd) + 1;
    int nextMsg = response.indexOf("+CMGL:", contentStart);
    int contentEnd = (nextMsg == -1) ? response.indexOf("OK\r\n") : nextMsg;
    String content = response.substring(contentStart, contentEnd);
    content.trim();
    content.toUpperCase();

    Serial.print("[SMS] Stored message #");
    Serial.print(msgIndex);
    Serial.print(" Status: ");
    Serial.print(status);
    Serial.print(" From: ");
    Serial.print(sender);
    Serial.print(" Content: ");
    Serial.println(content);

    // Process if from admin
    if (sender == ADMIN_NUMBER || sender == ("+26" + String(ADMIN_NUMBER).substring(1))) {
      processCommand(content);
      
      // Delete processed message
      modem.sendAT("+CMGD=" + String(msgIndex));
      modem.waitResponse(1000);
    }

    index = contentEnd;
  }
}
// Helper function to process commands (add this if not existing)
void processCommand(String cmd) {
 cmd.trim();
  cmd.toUpperCase(); // Force uppercase
  cmd.replace("\r", "");
  cmd.replace("\n", "");

  Serial.print("Processing command: ");
  Serial.println(cmd);

  if (cmd == "OPEN") {
    unlockDoor();
    sendSMS(ADMIN_NUMBER, "Door opened via SMS");
  } 
  else if (cmd == "CLOSE") {
    lockDoor();
    sendSMS(ADMIN_NUMBER, "Door closed via SMS");
  }
  else if (cmd == "ON") {
    digitalWrite(RELAY_PIN, HIGH);
    bulbState = true;
    sendSMS(ADMIN_NUMBER, "Light turned ON");
  }
  else if (cmd == "OFF") {
    digitalWrite(RELAY_PIN, LOW);
    bulbState = false;
    sendSMS(ADMIN_NUMBER, "Light turned OFF");
  }
  else if(cmd == "SHUTDOWN"){
    systemEnabled = false;
    handleSystemShutdown();
    sendSMS(ADMIN_NUMBER, "System SHUTDOWN activated");
}else if (cmd == "RESTART") {
    systemEnabled = true; 
    handleSystemRestart();
    sendSMS(ADMIN_NUMBER, "System RESTARTED");
  }else if (!systemEnabled) {
    sendSMS(ADMIN_NUMBER, "System is currently SHUTDOWN");
    return; 
  }
  else if (cmd == "STATUS") {
    String status = "System Status:\n";
    status += "Temp: " + String(dht.readTemperature()) + "C\n";
    status += "Humidity: " + String(dht.readHumidity()) + "%\n";
    status += "Light: " + String(bulbState ? "ON" : "OFF") + "\n";
    status += "Door: " + String(doorServo.read() == SERVO_LOCKED_POS ? "LOCKED" : "UNLOCKED");
    sendSMS(ADMIN_NUMBER, status);
  }
  else {
    sendSMS(ADMIN_NUMBER, "Unknown command. Try: OPEN, CLOSE, ON, OFF, STATUS");
  }
}
void sendAccessAlert(String method, String identifier, bool granted) {
  Serial.println("[ALERT] Attempting to send access alert...");
  String message = method + " access ";
  message += granted ? "GRANTED" : "DENIED";
  message += " for ";
  message += identifier;
  sendSMS(ADMIN_NUMBER, message);
}

// ========== System Control Functions ==========

void handleSystemShutdown() {
  // Turn off light
  bulbState = false;
  digitalWrite(RELAY_PIN, LOW);
  
  // Lock door
  lockDoor();
  
  mfrc522.PCD_AntennaOff();  // Turn off RFID antenna
  mfrc522.PCD_SoftPowerDown(); // Put RFID in low power mode
  
  // 3. Visual indication
  digitalWrite(GRANTED_LED, LOW);
  digitalWrite(DENIED_LED, HIGH);  // Red LED indicates shutdown
  
  // 4. Optional: Disable WiFi to prevent remote access
  WiFi.disconnect(true);
  
  String message = "System shutdown complete";
  Serial.println(message);
   sendSMS(ADMIN_NUMBER, message);
}

void handleSystemRestart() {
  mfrc522.PCD_Init();  // Restart RFID
  mfrc522.PCD_AntennaOn();

  // 2. Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // Turn on light temporarily
  bulbState = true;
  digitalWrite(RELAY_PIN, HIGH);
  
  // Cycle door lock
  unlockDoor();
  
  // Turn off light
  bulbState = false;
  digitalWrite(RELAY_PIN, LOW);

  String message = "System restart complete";
  Serial.println(message);
  sendSMS(ADMIN_NUMBER, message);
}


// ========== Helper Functions ==========

void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
}

void onWebSocketEvent(uint8_t client_num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_TEXT) {
    String msg = (char*)payload;
    msg.trim();
    msg.toUpperCase();

    Serial.print("Received from WebSocket: ");
    Serial.println(msg);


    // Handle simple ON/OFF/TOGGLE commands
    if (msg == "ON" || msg == "BULB_ON") {
      bulbState = true;
      digitalWrite(RELAY_PIN, HIGH);
      webSocket.sendTXT(client_num, msg == "ON" ? "{\"bulb\":\"on\"}" : "BULB_ON_OK");
    } 
    else if (msg == "OFF" || msg == "BULB_OFF") {
      bulbState = false;
      digitalWrite(RELAY_PIN, LOW);
      webSocket.sendTXT(client_num, msg == "OFF" ? "{\"bulb\":\"off\"}" : "BULB_OFF_OK");
    } 
    else if (msg == "TOGGLE") {
      bulbState = !bulbState;
      digitalWrite(RELAY_PIN, bulbState ? HIGH : LOW);
      webSocket.sendTXT(client_num, String("{\"bulb\":\"") + (bulbState ? "on" : "off") + "\"}");
    }
    // Handle door commands
    else if (msg == "GRANTED") {
      unlockDoor();
      lockDoor();
      webSocket.sendTXT(client_num, "DOOR_OPEN_OK");
    }
    else if (msg == "DENIED") {
      unlockDoor(); 
      lockDoor();
      webSocket.sendTXT(client_num, "DOOR_CLOSE_OK");
    }
    // Handle system commands
    else if (msg == "BULB_ON") {
      bulbState = false;
      digitalWrite(RELAY_PIN, HIGH);
      webSocket.sendTXT(client_num, "SHUTDOWN_STARTED");
    }
    else if (msg == "BULB_OFF") {
      webSocket.sendTXT(client_num, "RESTARTING");
      bulbState = false;
      digitalWrite(RELAY_PIN,LOW);
    }
    // Unknown command
    else {
      webSocket.sendTXT(client_num, "ERROR: Unknown command");
    }
  }
}

void postDataToDjango(float t, float h) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(djangoSensorUrl);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["temperature"] = t;
    doc["humidity"] = h;
    doc["device_id"] = WiFi.macAddress();
    String body;
    serializeJson(doc, body);

    http.POST(body);
    http.end();
  }
}

void checkPasswordWithDjango(String pass) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(djangoAuthUrl);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["type"] = "keypad";
    doc["value"] = pass;
    doc["device_id"] = WiFi.macAddress();
    
    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    String res = http.getString();

    Serial.println("Keypad check response: " + res);
    Serial2.println(res);
    Serial.println("Sent to Mega: " + res);

    // Parse response
    DeserializationError error = deserializeJson(doc, res);
    if (!error) {
      const char* status = doc["status"];
      if (String(status) == "GRANTED") {
        unlockDoor();
        webSocket.broadcastTXT("{\"access\":\"granted\", \"method\":\"keypad\"}");
      } else {
        webSocket.broadcastTXT("{\"access\":\"denied\", \"method\":\"keypad\"}");
      }
    }

    http.end();
  }
}

void checkRFIDWithDjango(String uid) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(djangoRfidUrl);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["type"] = "rfid";
    doc["value"] = uid;
    doc["device_id"] = WiFi.macAddress();
    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    String res = http.getString();
    Serial.println("RFID check response: " + res);

    DeserializationError error = deserializeJson(doc, res);
    if (!error) {
      const char* status = doc["status"];
      if (String(status) == "GRANTED") {
           webSocket.broadcastTXT("{\"rfid\":\"access_granted\", \"rfid_id\":\"" + uid + "\"}");
             unlockDoor();
         digitalWrite(GRANTED_LED,HIGH);
         delay(3000);
         lockDoor();
         digitalWrite(GRANTED_LED,LOW);
         sendAccessAlert("RFID", uid, true);
         
      } else {
        unlockDoor();
        digitalWrite(DENIED_LED,HIGH);
         lockDoor();
        delay(3000);
        digitalWrite(DENIED_LED,LOW);
        sendAccessAlert("RFID", uid, false);
        webSocket.broadcastTXT("{\"rfid\":\"access_denied\", \"rfid_id\":\"" + uid + "\"}");
      }
    }

    http.end();
  }
}

void handleSerialFromMega() {
  if (SerialMega.available()) {
    String line = SerialMega.readStringUntil('\n');
    line.trim();
    Serial.print("ESP32 received: ");
    if (line.startsWith("KEYPAD:")) {
      String pass = line.substring(7);
      checkPasswordWithDjango(pass);
    }
  }
}

void handleRFID() {
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String rfidUid = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      rfidUid += (mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
      rfidUid += String(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.println("RFID Scanned: " + rfidUid);
    checkRFIDWithDjango(rfidUid);
    mfrc522.PICC_HaltA();
  }
}

void unlockDoor() {
  Serial.println("Unlocking door...");
  doorServo.write(SERVO_UNLOCKED_POS);
  delay(UNLOCK_DURATION);
  
}

void lockDoor() {
  Serial.println("Locking door...");
  doorServo.write(SERVO_LOCKED_POS);
}

 