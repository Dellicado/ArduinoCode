#include <Wire.h>
#include <EEPROM.h>
#include "MAX30100_PulseOximeter.h"
#include <U8g2lib.h>  // Include U8g2 for the OLED display
#include <WiFi.h>     // Include WiFi library
#include <FirebaseESP32.h>  // Include Firebase ESP32 library

#define ENABLE_MAX30100 1

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);  // Initialize U8g2 for SH1106

#if ENABLE_MAX30100
#define REPORTING_PERIOD_MS 5000

PulseOximeter pox;
#endif

// WiFi credentials
const char* ssid = "Tsando_Deco";
const char* password = "Beringuelfam_7";

// Firebase credentials
FirebaseConfig config;
FirebaseAuth auth;
FirebaseData firebaseData;

uint32_t tsLastReport = 0;
uint32_t tsLastSave = 0;

float glucose_records[3] = {0.0, 0.0, 0.0};  // Array to store the last three records

//void onBeatDetected() {
//    Serial.println("Beat!");
//}

void setup() {
    Serial.begin(115200);
    Serial.println("U8g2 SH1106 128x64 OLED TEST");

    u8g2.begin();  // Initialize the U8g2 display
    u8g2.setFont(u8g2_font_ncenB08_tr);  // Set font
    u8g2.clearBuffer();  // Clear the internal memory
    u8g2.drawStr(20, 18, "GLUCOPULSE");  // Display initial message
    u8g2.sendBuffer();  // Transfer internal memory to the display

    delay(2000);  // Pause for 2 seconds

    // Initialize WiFi
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
        yield();  // Allow background tasks to execute
    }
    Serial.println();
    Serial.println("WiFi connected");

    // Set up Firebase configuration
    config.host = "https://gp-auth-670ed-default-rtdb.firebaseio.com/";   // Firebase Host URL
    config.api_key = "AIzaSyDLuOVZiuSMyrQ33N-9Y8yTqilLafi_erk"; // Firebase API key or secret
    auth.user.email = "bladestone12@gmail.com";  // Optional: Use if authentication via email and password is needed
    auth.user.password = "bladestone12";  // Optional: Use if authentication via email and password is needed

    // Initialize Firebase
    Firebase.begin(&config, &auth);

    // Enable auto-reconnect to WiFi
    Firebase.reconnectWiFi(true);

    Serial.println("Firebase initialized");

    Serial.print("Initializing pulse oximeter..");
#if ENABLE_MAX30100
    if (!pox.begin()) {
        Serial.println("FAILED");
        for (;;);
    } else {
        Serial.println("SUCCESS");
    }

    pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
//    pox.setOnBeatDetectedCallback(onBeatDetected);
#endif

    load_glucose_records();
    print_glucose_records();  // Print loaded records to Serial
}

void loop() {
#if ENABLE_MAX30100
    pox.update();
    int bpm = 0;
    int spo2 = 0;
    float glucose_level = 0.0;

    if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
        bpm = pox.getHeartRate();
        spo2 = pox.getSpO2();

        if (bpm > 0 && spo2 > 0) {
            glucose_level = 16714.61 + 0.47 * bpm - 351.045 * spo2 + 1.85 * (spo2 * spo2);
        }

        Serial.print("Heart rate: ");
        Serial.println(bpm);
        Serial.print("SpO2: ");
        Serial.println(spo2);
        Serial.print("Glucose Level: ");
        Serial.println(glucose_level);

        tsLastReport = millis();
        display_data(bpm, spo2, glucose_level);

        // Use a flag to send data to Firebase at intervals
        static unsigned long lastFirebaseSend = 0;
        if (millis() - lastFirebaseSend > 10000) { // Send data every 10 seconds
            if (WiFi.status() == WL_CONNECTED) {
                sendDataToFirebase(bpm, spo2, glucose_level);
            } else {
                Serial.println("WiFi not connected, skipping Firebase update.");
            }
            lastFirebaseSend = millis();
        }

        yield();  // Add yield here to avoid blocking in case of long sensor readings
    }

    // Save the glucose level if it is smaller than 500 every 10 seconds
    if (glucose_level < 500.0 && glucose_level > 0 && millis() - tsLastSave > 10000) {
        save_glucose_level(glucose_level);
        tsLastSave = millis();
    }

    yield();  // Add yield to ensure smooth running of background tasks
#endif
}

void display_data(int bpm, int spo2, float glucose_level) {
    u8g2.clearBuffer();  // Clear the display buffer

    // Display BPM
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.setCursor(0, 10);
    u8g2.print("BPM: ");
    u8g2.print(bpm);

    // Display SpO2
    u8g2.setCursor(0, 20);
    u8g2.print("SpO2: ");
    u8g2.print(spo2);

    // Display Glucose Level
    u8g2.setCursor(0, 30);
    if (glucose_level > 500.0 || glucose_level < 0) {
        u8g2.print("Glucose: MEAS");
    } else {
        u8g2.print("Glucose: ");
        u8g2.print(glucose_level);
    }

    // Draw a vertical line and display glucose records
    u8g2.drawLine(88, 0, 88, 64);  // Vertical line
    for (int i = 0; i < 3; i++) {
        u8g2.setCursor(92, 10 + (i * 10));
        u8g2.print(glucose_records[i]);
    }

    u8g2.sendBuffer();  // Transfer buffer to display
}

void save_glucose_level(float glucose_level) {
    // Shift the old records
    glucose_records[2] = glucose_records[1];
    glucose_records[1] = glucose_records[0];
    glucose_records[0] = glucose_level;

    // Save the records to EEPROM
    for (int i = 0; i < 3; i++) {
        EEPROM.put(i * sizeof(float), glucose_records[i]);
        yield();  // Add yield here to avoid EEPROM operations from blocking other tasks
    }

    print_glucose_records();  // Print saved records to Serial for debugging
}

void load_glucose_records() {
    // Load the records from EEPROM
    for (int i = 0; i < 3; i++) {
        EEPROM.get(i * sizeof(float), glucose_records[i]);
        yield();  // Add yield to ensure smooth EEPROM read operations
    }
}

void print_glucose_records() {
    // Print glucose records to Serial for debugging
    Serial.println("Glucose Records:");
    for (int i = 0; i < 3; i++) {
        Serial.print(i);
        Serial.print(": ");
        Serial.println(glucose_records[i]);
        yield();  // Yield during print operations to avoid blocking
    }
}

void sendDataToFirebase(int bpm, int spo2, float glucose_level) {
    // Create JSON object to hold the data
    FirebaseJson json;
    json.set("BPM", bpm);
    json.set("SpO2", spo2);
    json.set("Glucose", glucose_level);

    // Send the data to Firebase
    if (Firebase.setJSON(firebaseData, "/health_data", json)) {
        Serial.println("Data sent successfully");
    } else {
        Serial.print("Failed to send data: ");
        Serial.println(firebaseData.errorReason());
    }

    yield();  // Add yield after sending data to Firebase
}
