#include <Arduino.h>
#include "Epaper.h"

namespace {

G1 driver(Epaper::WIDTH, Epaper::HEIGHT);
GFXcanvas1 red(Epaper::WIDTH, Epaper::HEIGHT);
bool initialized = false;

constexpr size_t IMAGE_PLANE_BYTES =
    ((Epaper::WIDTH + 7) / 8) * Epaper::HEIGHT;

}  // namespace

namespace Epaper {

bool begin(int8_t epd_mosi, int8_t epd_clk, int8_t epd_cs,
           int8_t epd_busy, int8_t epd_enable) {
    driver.begin(epd_mosi, epd_clk, epd_cs, epd_busy, epd_enable);
    initialized = true;

    return true;
}

G1& blackCanvas() {
    return driver;
}

GFXcanvas1& redCanvas() {
    return red;
}

void clear() {
    driver.fillScreen(0);
    red.fillScreen(0);
}

uint8_t display() {
    if (!initialized) {
        Serial.println("[epaper] Display has not been initialized");
        return 255;
    }

    return driver.display(red.getBuffer());
}

bool displayImage(File& image) {
    Serial.println("[epaper] displayImage started");
    if (!initialized) {
        Serial.println("[epaper] Cannot display image before initialization");
        return false;
    }

    if (!image) {
        Serial.println("[epaper] Image file is not open");
        return false;
    }

    const size_t fileSize = image.size();
    Serial.printf("[epaper] Image file size: %u bytes\n",
                  static_cast<unsigned>(fileSize));
    if (fileSize != IMAGE_PLANE_BYTES &&
        fileSize != 2 * IMAGE_PLANE_BYTES) {
        Serial.printf("[epaper] Invalid image size: %u bytes\n",
                      static_cast<unsigned>(fileSize));
        return false;
    }

    if (!image.seek(0)) {
        Serial.println("[epaper] Could not rewind image file");
        return false;
    }
    Serial.println("[epaper] Reading black plane");

    if (image.read(driver.getBuffer(), IMAGE_PLANE_BYTES) != IMAGE_PLANE_BYTES) {
        Serial.println("[epaper] Could not read black image plane");
        return false;
    }
    Serial.println("[epaper] Black plane loaded");

    red.fillScreen(0);
    if (fileSize == 2 * IMAGE_PLANE_BYTES &&
        image.read(red.getBuffer(), IMAGE_PLANE_BYTES) != IMAGE_PLANE_BYTES) {
        Serial.println("[epaper] Could not read red image plane");
        return false;
    }
    Serial.println("[epaper] Red plane loaded");

    Serial.println("[epaper] Starting panel update");
    const uint8_t status = display();
    if (status != 0) {
        Serial.printf("[epaper] Image display failed (status %u)\n", status);
        return false;
    }
    printf("[epaper] Image display successful\n");

    return true;
}

bool displayImageData(const uint8_t* data, size_t size) {
    if (!initialized) {
        Serial.println("[epaper] Cannot display image before initialization");
        return false;
    }

    if (!data || (size != IMAGE_PLANE_BYTES && size != 2 * IMAGE_PLANE_BYTES)) {
        Serial.printf("[epaper] Invalid image data size: %u bytes\n",
                      static_cast<unsigned>(size));
        return false;
    }

    memcpy(driver.getBuffer(), data, IMAGE_PLANE_BYTES);
    red.fillScreen(0);
    if (size == 2 * IMAGE_PLANE_BYTES) {
        memcpy(red.getBuffer(), data + IMAGE_PLANE_BYTES, IMAGE_PLANE_BYTES);
    }

    const uint8_t status = display();
    if (status != 0) {
        Serial.printf("[epaper] Preview display failed (status %u)\n", status);
        return false;
    }

    return true;
}

}  // namespace Epaper