#include <LiquidCrystal.h>
#include <Keypad.h>
#include <ArduinoJson.h>

// LCD pin setup: RS=2, E=3, D4=13, D5=12, D6=11, D7=10
LiquidCrystal lcd(2, 3, 13, 12, 11, 10);

// Keypad setup
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {22, 23, 24, 25};
byte colPins[COLS] = {26, 27, 28, 29};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Buzzer setup
const int buzzerPin = 8; // Change to your buzzer pin
const int keyPressDuration = 50; // ms for key press beep
const int grantedBeepDuration = 300; // ms for access granted
const int deniedBeepDuration = 1000; // ms for access denied

String enteredPassword = "";
String lastTemp = "";
String lastHum = "";
bool isEnteringPassword = false;
bool waitingForAccessResponse = false;

  void handleSerialFromESP32();
  void handleKeypadInput();
  void displayTempAndHum();

void setup() {
  Serial.begin(9600);    // Debug
  Serial1.begin(9600);
  Serial2.begin(9600);   // For ESP32
  lcd.begin(16, 2);
  lcd.print("Waiting for ESP");
  
  // Initialize buzzer pin
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
}

void loop() {
  handleSerialFromESP32();
  handleKeypadInput();
}

// Display temperature and humidity
void displayTempAndHum() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp: " + lastTemp + "C");
  lcd.setCursor(0, 1);
  lcd.print("Hum: " + lastHum + "%");
}

// Handle serial input from ESP32
void handleSerialFromESP32() {
  if (Serial1.available()) {
    String input = Serial1.readStringUntil('\n');
    input.trim();
    Serial.println("Received from ESP32: " + input);  

    // Handle access response
    if (waitingForAccessResponse && input.startsWith("{")) {
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, input);

      if (!error) {
        String statusValue = doc["status"].as<String>();

        lcd.clear();
        if (statusValue == "GRANTED") {
          lcd.print("Access Granted");
          // Access granted beep (short high tone)
          tone(buzzerPin, 1500, grantedBeepDuration);
        } else if (statusValue == "DENIED") {
          lcd.print("Access Denied");
          // Access denied beep (long low tone)
          tone(buzzerPin, 800, deniedBeepDuration);
        } else {
          lcd.print("Unknown Status");
        }

        enteredPassword = "";
        waitingForAccessResponse = false;
        delay(2000);
        displayTempAndHum();
        return;
      } else {
        Serial.println("❌ JSON parse error: " + String(error.c_str()));
      }
    }

    // Handle TEMP and HUM update if not entering password
    if (!isEnteringPassword && input.startsWith("TEMP:")) {
      int commaIndex = input.indexOf(',');
      if (commaIndex != -1) {
        lastTemp = input.substring(5, commaIndex);
        int humIndex = input.indexOf("HUM:");
        if (humIndex != -1) {
          lastHum = input.substring(humIndex + 4);
        }
      }
      displayTempAndHum();
    }
  }
}

// Handle Keypad Logic
void handleKeypadInput() {
  char key = keypad.getKey();
  if (key) {
    // Short beep for any key press
    tone(buzzerPin, 1000, keyPressDuration);
    
    if (!isEnteringPassword) {
      isEnteringPassword = true;
      lcd.clear();
      lcd.print("Enter Password:");
      lcd.setCursor(0, 1);
      lcd.print("PWD: ");
    }

    if (key == '#') {
      if (enteredPassword.length() > 0) {
        // Send password to ESP32
        Serial1.print("KEYPAD:");
        Serial1.println(enteredPassword);
        Serial.print("Sent password: ");
        Serial.println(enteredPassword);

        lcd.clear();
        lcd.print("Checking...");
        waitingForAccessResponse = true;
        isEnteringPassword = false;
      }
    } else if (key == '*') {
      enteredPassword = "";
      lcd.clear();
      lcd.print("Cleared");
      delay(1000);
      isEnteringPassword = false;
      displayTempAndHum();
    } else {
      enteredPassword += key;
      lcd.setCursor(0, 1);
      lcd.print("PWD: ");
      for (int i = 0; i < (int)enteredPassword.length(); i++) {
        lcd.print("*");
      }
    }
  }
}
