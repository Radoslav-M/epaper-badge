// Includes
    #include <Arduino.h>
    #include "G1_Driver.h"
    #include "Wifimgr.h"
    #include "WebUI.h"
    #include "Epaper.h"


// Definitions
    // PINS
        #define EPD_CLK    18
        #define EPD_MOSI   23
        #define EPD_CS     5
        #define EPD_ENABLE 17
        #define EPD_BUSY   16

    // E-Paper Display Dimensions
        #define EPD_WIDTH 400
        #define EPD_HEIGHT 300


void setup() {
    // Initialize serial communication and E-Paper display
        Serial.begin(115200);

    // Initialize E-Paper display
        Epaper::begin(EPD_MOSI, EPD_CLK, EPD_CS, EPD_BUSY, EPD_ENABLE);

    // Initialize WiFi manager and web UI
        WifiMgr::begin("Limes E-Paper Badge", "password");   // AP name + password (min 8 chars)
        WebUI::begin(EPD_WIDTH, EPD_HEIGHT);
        WifiMgr::startServer();
}


void loop() {
    delay(10000000);
}