#pragma once

#include <Arduino.h>
#include <FS.h>
#include "G1_Driver.h"

namespace Epaper {

constexpr uint16_t WIDTH = 400;
constexpr uint16_t HEIGHT = 300;

bool begin(int8_t epd_mosi, int8_t epd_clk, int8_t epd_cs,
           int8_t epd_busy, int8_t epd_enable);

G1& blackCanvas();
GFXcanvas1& redCanvas();

void clear();
uint8_t display();
bool displayImage(File& image);
bool displayImageData(const uint8_t* data, size_t size);

}  // namespace Epaper