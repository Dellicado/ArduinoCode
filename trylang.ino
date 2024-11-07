#include <Wire.h>
#include <EEPROM.h>
#include "MAX30100_PulseOximeter.h"
#include <U8g2lib.h>  // Include U8g2 for the OLED display
//#include <WiFi.h>     // Include WiFi library
#include <WiFiManager.h> // Include WiFiManager library
#include <FirebaseESP32.h>  // Include Firebase ESP32 library
#include <math.h>

// OLED Display Setup
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);  // Initialize U8g2 for SH1106

// Pulse Oximeter Setup
#define ENABLE_MAX30100 1
#if ENABLE_MAX30100
#define REPORTING_PERIOD_MS 5000

PulseOximeter pox;
#endif

FirebaseConfig config;
FirebaseAuth auth;
FirebaseData firebaseData;

// Variables for data collection
uint32_t tsLastReport = 0;
uint32_t tsLastSave = 0;

float glucose_records[3] = {0.0, 0.0, 0.0};  // Array to store the last three records
int entryCount = 0; // Initialize entry count
int bpm = 0;       // Heart Rate
int spo2 = 0;      // SpO2
float glucose_level = 0.0; // Glucose Level

// Task Handles
TaskHandle_t GetReadingsTask;
TaskHandle_t SendDataTask;

// Function Declarations
void GetSensorReadings(void *parameter);
void SendDataToFirebase(void *parameter);
void display_data();
void save_glucose_level(float glucose_level);
void load_glucose_records();
void print_glucose_records();
void sendDataToFirebase(int bpm, int spo2, float glucose_level);
void blinkCheckSymbol();  // Declaration for the blinking function

void setup() {
    Serial.begin(115200);
    Serial.println("U8g2 SH1106 128x64 OLED TEST");

    u8g2.begin();  // Initialize the U8g2 display
    u8g2.setFont(u8g2_font_ncenB08_tr);  // Set font
    u8g2.clearBuffer();  // Clear the internal memory
    u8g2.drawStr(20, 18, "GLUCOPULSE");  // Display initial message
      u8g2.sendBuffer();  // Transfer internal memory to the display

    delay(2000);  // Pause for 2 seconds

    // Initialize WiFi using WiFiManager
    WiFiManager wifiManager;
       Serial.println("Starting WiFiManager...");
    wifiManager.setTimeout(180);  // Set timeout to 180 seconds (default is 30)
    if (!wifiManager.autoConnect("GlucoPulse_AP")) {
        Serial.println("Failed to connect, entering AP mode...");
    } else {
        Serial.println("WiFi connected successfully!");
    }
    
    Serial.println("Connected to WiFi!");

    // Set up Firebase configuration
    config.host = "https://gp-auth-670ed-default-rtdb.firebaseio.com/";   // Firebase Host URL
    config.api_key = "AIzaSyDLuOVZiuSMyrQ33N-9Y8yTqilLafi_erk"; // Firebase API key or secret
    auth.user.email = "bladestone12@gmail.com";  // Optional: Use if authentication via email and password is needed
    auth.user.password = "bladestone12";  // Optional: Use if authentication via email and password is needed

    // Initialize Firebase
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // Retrieve the current entry count
    if (Firebase.getInt(firebaseData, "/health_data/count")) {
        entryCount = firebaseData.intData();
        Serial.print("Current Firebase entry count: ");
        Serial.println(entryCount);
    } else {
        Serial.print("Failed to retrieve entry count: ");
        Serial.println(firebaseData.errorReason());
        entryCount = 0; // Start from 0 if unable to retrieve
    }

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
#endif

    load_glucose_records();
    print_glucose_records();  // Print loaded records to Serial

    // Create FreeRTOS tasks
    xTaskCreatePinnedToCore(GetSensorReadings, "GetReadings", 4096, NULL, 1, &GetReadingsTask, 0);
    xTaskCreatePinnedToCore(SendDataToFirebase, "PostToFirebase", 6268, NULL, 1, &SendDataTask, 1);
}

void loop() {
    delay(1);
}

// Remaining code follows unchanged...

void GetSensorReadings(void *parameter) {
    for (;;) {
#if ENABLE_MAX30100
        pox.update();

        if (millis() - tsLastReport > REPORTING_PERIOD_MS) {
            bpm = pox.getHeartRate();
            spo2 = pox.getSpO2();

            // Ensure we have valid readings
            if (bpm > 0 && spo2 > 0) {
                glucose_level = 16714.61 + 0.47 * bpm - 351.045 * spo2 + 1.85 * (spo2 * spo2);
                Serial.print("Heart rate: ");
                Serial.println(bpm);
                Serial.print("SpO2: ");
                Serial.println(spo2);
                Serial.print("Glucose Level: ");
                Serial.println(glucose_level);
                
                // Save the glucose level if it is smaller than 500 every 10 seconds
                if (glucose_level < 500.0 && glucose_level > 0 && millis() - tsLastSave > 10000) {
                    save_glucose_level(glucose_level);
                    tsLastSave = millis();
                }
            } else {
                Serial.println("Invalid heart rate or SpO2 readings.");

            }

            tsLastReport = millis();
            display_data();
        }
#endif
        vTaskDelay(10 / portTICK_PERIOD_MS);  // Delay to yield control
    }
}


void SendDataToFirebase(void *parameter) {
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            // Only send data to Firebase if bpm and spo2 have valid readings
            if (bpm > 0 && spo2 > 0) {
                sendDataToFirebase(bpm, spo2, glucose_level);
            } else {
                Serial.println("Invalid bpm or SpO2 readings. Skipping Firebase update.");
            }
        } else {
            Serial.println("WiFi not connected, skipping Firebase update.");
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS);  // Delay to send data every 10 seconds
    }
}

void display_data() {
    u8g2.clearBuffer();  // Clear the display buffer

    // Display BPM
    u8g2.setFont(u8g2_font_ncenB08_tr);  // Use a medium font for BPM and SpO2
    u8g2.setCursor(0, 10);
    u8g2.print("BPM: ");
    u8g2.print(bpm);

    // Display SpO2
    u8g2.setCursor(0, 20);
    u8g2.print("SpO2: ");
    u8g2.print(spo2);

    // Check if BPM or SpO2 is zero; if so, set glucose_level to zero and display message
    float display_glucose = (bpm == 0 || spo2 == 0) ? 0 : glucose_level;

    // Display Glucose Level
    u8g2.setCursor(0, 30);
    if (display_glucose > 500.0 || display_glucose < 0) {
        u8g2.print("Glucose: MEAS");
    } else {
        u8g2.print("Glucose: ");
        u8g2.print(display_glucose);
    }

    // Display message if bpm or spo2 is zero
    if (bpm == 0 || spo2 == 0) {
        u8g2.setFont(u8g2_font_5x7_tr);  // Use a smaller font for the message
        u8g2.setCursor(0, 50);  // Position above the vertical line
        u8g2.print("Place finger to");
        u8g2.setCursor(0, 58);  // Position above the vertical line
        u8g2.print("take a reading.");
    }
        u8g2.drawLine(88, 0, 88, 64);  // Vertical line
    for (int i = 0; i < 3; i++) {
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.setCursor(92, 10 + (i * 10));
        u8g2.print(glucose_records[i]);
    }

    u8g2.sendBuffer();  // Transfer buffer to the display
}

void save_glucose_level(float glucose_level) {
    // Update the array of glucose records
    for (int i = 0; i < 2; i++) {
        glucose_records[i] = glucose_records[i + 1];
    }
    glucose_records[2] = glucose_level;

    // Print glucose records to Serial
    Serial.print("Glucose Records: ");
    for (int i = 0; i < 3; i++) {
        Serial.print(glucose_records[i]);
        Serial.print(" ");
    }
    Serial.println();

    // Save records to EEPROM
    EEPROM.put(0, glucose_records);
    EEPROM.commit();
}

void load_glucose_records() {
    // Load glucose records from EEPROM
    EEPROM.get(0, glucose_records);
}

void print_glucose_records() {
    Serial.print("Loaded Glucose Records: ");
    for (int i = 0; i < 3; i++) {
        Serial.print(glucose_records[i]);
        Serial.print(" ");
    }
    Serial.println();
}

void sendDataToFirebase(int bpm, int spo2, float glucose_level) {
    String path = "/health_data/entry" + String(entryCount + 1);
    if (Firebase.setInt(firebaseData, path + "/bpm", bpm) &&
        Firebase.setInt(firebaseData, path + "/spo2", spo2) &&
        Firebase.setFloat(firebaseData, path + "/glucose", glucose_level)) {
        
        Serial.println("Data sent successfully to Firebase");
        blinkCheckSymbol();  // Call the blink function after sending data successfully

        // Increment the entry count and update it in Firebase
        entryCount++;
        Firebase.setInt(firebaseData, "/health_data/count", entryCount);
    } else {
        Serial.print("Failed to send data to Firebase: ");
        Serial.println(firebaseData.errorReason());
    }
}

void blinkCheckSymbol() {
        display_data(); 

        u8g2.drawDisc(20, 50, 3);
        u8g2.sendBuffer();  // Show the check mark
        delay(2000);  // Wait for half a second
  
        display_data(); 
    }
