#include <Wire.h>
#include <EEPROM.h>
#include "MAX30100_PulseOximeter.h"
#include "U8glib.h"  // Include U8glib for the OLED display

#define ENABLE_MAX30100 1

U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_NONE);  // Initialize U8glib for SH1106

#if ENABLE_MAX30100
#define REPORTING_PERIOD_MS 5000

PulseOximeter pox;
#endif

uint32_t tsLastReport = 0;
uint32_t tsLastSave = 0;

float glucose_records[3] = {0.0, 0.0, 0.0};  // Array to store the last three records

void onBeatDetected() {
    Serial.println("Beat!");
}

void setup() {
    Serial.begin(115200);
    Serial.println("U8glib SH1106 128x64 OLED TEST");

    u8g.setFont(u8g_font_6x10);  // Set font
    u8g.firstPage();
    do {
        u8g.drawStr(20, 18, "GLUCOPULSE");  // Display initial message
    } while (u8g.nextPage());

    delay(2000);  // Pause for 2 seconds

    Serial.print("Initializing pulse oximeter..");
#if ENABLE_MAX30100
    if (!pox.begin()) {
        Serial.println("FAILED");
        for (;;);
    } else {
        Serial.println("SUCCESS");
    }

    pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
    pox.setOnBeatDetectedCallback(onBeatDetected);
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
    }

    // Save the glucose level if it is smaller than 500 every 10 seconds
    if (glucose_level < 500.0 && glucose_level > 0 && millis() - tsLastSave > 10000) {
        save_glucose_level(glucose_level);
        tsLastSave = millis();
    }
#endif
}

void display_data(int bpm, int spo2, float glucose_level) {
    u8g.firstPage();
    do {
        // Display BPM
        u8g.setFont(u8g_font_6x10);
        u8g.setPrintPos(0, 10);
        u8g.print("BPM: ");
        u8g.print(bpm);

        // Display SpO2
        u8g.setPrintPos(0, 20);
        u8g.print("SpO2: ");
        u8g.print(spo2);

        // Display Glucose Level
        u8g.setPrintPos(0, 30);
        if (glucose_level > 500.0 || glucose_level < 0) {
            u8g.print("Glucose: MEAS");
        } else {
            u8g.print("Glucose: ");
            u8g.print(glucose_level);
        }

        // Draw a vertical line and display glucose records
        u8g.drawLine(88, 0, 88, 64);  // Vertical line
        for (int i = 0; i < 3; i++) {
            u8g.setPrintPos(92, 10 + (i * 10));
            u8g.print(glucose_records[i]);
        }

    } while (u8g.nextPage());
}

void save_glucose_level(float glucose_level) {
    // Shift the old records
    glucose_records[2] = glucose_records[1];
    glucose_records[1] = glucose_records[0];
    glucose_records[0] = glucose_level;

    // Save the records to EEPROM
    for (int i = 0; i < 3; i++) {
        EEPROM.put(i * sizeof(float), glucose_records[i]);
    }

    print_glucose_records();  // Print saved records to Serial for debugging
}

void load_glucose_records() {
    // Load the records from EEPROM
    for (int i = 0; i < 3; i++) {
        EEPROM.get(i * sizeof(float), glucose_records[i]);
    }
}

void print_glucose_records() {
    // Print glucose records to Serial for debugging
    Serial.println("Glucose Records:");
    for (int i = 0; i < 3; i++) {
        Serial.print(i);
        Serial.print(": ");
        Serial.println(glucose_records[i]);
    }
}

void heart_beat(int *x_pos) {
    // U8glib does not support the exact drawPixel method, 
    // so you'll need to manually implement the heartbeat drawing
    u8g.firstPage();
    do {
        // Draw the heart beat pattern at the specified x_pos (example below)
        u8g.drawBox(*x_pos, 8, 30, 15);  // Example of drawing a box to represent heartbeat
        *x_pos += 30;  // Increment x_pos
    } while (u8g.nextPage());
}
