#include <SPI.h>
#include "G1_Driver.h"
#include <Arduino.h>

namespace {
    constexpr uint32_t EPD_BUSY_TIMEOUT_MS = 20000;
}

G1::G1(uint16_t w, uint16_t h): GFXcanvas1(w, h) {}

void G1::begin(int8_t epd_mosi, int8_t epd_clk, int8_t epd_cs, int8_t epd_busy, int8_t epd_enable) {
  _epd_mosi = epd_mosi;
  _epd_clk = epd_clk;
  _epd_cs = epd_cs;
  _epd_busy = epd_busy;
  _epd_enable = epd_enable;

  vspi = new SPIClass(VSPI);
  vspi->begin(_epd_clk, -1, _epd_mosi, -1); //SCLK, MISO, MOSI, SS
  pinMode(_epd_cs, OUTPUT);
  pinMode(_epd_enable, OUTPUT);
  pinMode(_epd_busy, INPUT);
  digitalWrite(_epd_enable, LOW);
  digitalWrite(_epd_cs, LOW);
  this->temperature(0x50);
}

uint8_t G1::display(uint8_t*red_buffer) {
    Serial.println("[G1] Display called");
    int retry = 10;
    uint8_t status = 0;
    while ((status = display_internal(red_buffer)) != 0) {
        Serial.printf("[G1] Display attempt failed (status %u)\n", status);
        digitalWrite(_epd_enable, LOW);
        digitalWrite(_epd_cs, LOW);
        delay(500);
        if (retry-- <= 0) {
        return 1;
        }
    }
    return 0;
}

uint8_t G1::display_internal(uint8_t*red_buffer) {
  Serial.println("[G1] Starting panel communication");
  const uint8_t beginStatus = epd_begin(_temperature);
  if (beginStatus != 0) {
    Serial.printf("[G1] Panel startup failed (status %u, BUSY pin %d)\n",
                  beginStatus, digitalRead(_epd_busy));
    return 3;
  }
  Serial.println("[G1] Panel startup complete; sending image");

  int i, x;
  uint32_t buffer_position = 0;
  for (x = 0; x < 300; x++) {
    for (i = 0; i < 50; i++) {
      if (red_buffer != 0)
        epd_send_spi(red_buffer[buffer_position++]);//color RED
      else
        epd_send_spi(0x00);//color RED
    }
    delay(1);
    if (!waitWhileBusy(EPD_BUSY_TIMEOUT_MS)) {
      Serial.printf("[G1] Timeout after red row %d (BUSY pin %d)\n",
                    x, digitalRead(_epd_busy));
      return 2;
    }
  }
  buffer_position = 0;
  for (x = 0; x < 300; x++) {
    for (i = 0; i < 50; i++) {
      epd_send_spi(this->getBuffer()[buffer_position++]);//color BLACK
    }
    delay(1);
    if (x == 299)
      break;
    if (!waitWhileBusy(EPD_BUSY_TIMEOUT_MS)) {
      Serial.printf("[G1] Timeout after black row %d (BUSY pin %d)\n",
                    x, digitalRead(_epd_busy));
      return 1;
    }
  }
  delay(50);

  digitalWrite(_epd_cs, HIGH);
  if (!waitWhileBusy(EPD_BUSY_TIMEOUT_MS)) {
    Serial.printf("[G1] Final BUSY timeout (BUSY pin %d)\n",
                  digitalRead(_epd_busy));
    return 1;
  }

  digitalWrite(_epd_enable, LOW);
  digitalWrite(_epd_cs, LOW);
  Serial.println("[G1] Panel update complete");
  return 0;
}

void G1::epd_send_spi(uint8_t data) {
  vspi->beginTransaction(SPISettings(3000000, MSBFIRST, SPI_MODE0));
  vspi->transfer(data);
  vspi->endTransaction();
  delayMicroseconds(10);
}

uint8_t G1::epd_begin(uint8_t temperature) {
  digitalWrite(_epd_enable, HIGH);
  delay(300);
  digitalWrite(_epd_cs, HIGH);
  delay(300);
  digitalWrite(_epd_cs, LOW);
  delay(1);
  epd_send_spi(0x88);
  epd_send_spi(temperature);
  delay(1);

  if (digitalRead(_epd_busy) == LOW) {
    return 2;  // BUSY did not go high, indicating a startup/wiring issue.
  }

  return waitWhileBusy(EPD_BUSY_TIMEOUT_MS) ? 0 : 1;
}

bool G1::waitWhileBusy(uint32_t timeoutMs) {
  const uint32_t start = millis();

  while (digitalRead(_epd_busy) == HIGH) {
    if (millis() - start >= timeoutMs) {
      return false;
    }
    delay(1);  // Yield so the ESP32 scheduler and watchdog can run.
  }

  return true;
}

void G1::temperature(uint8_t temp){
  _temperature = temp;
}