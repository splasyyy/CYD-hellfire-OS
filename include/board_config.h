#pragma once

#include <Arduino.h>

namespace board {
constexpr int screenWidth = 320;
constexpr int screenHeight = 240;

constexpr int backlightPin = 21;

constexpr int touchSckPin = 25;
constexpr int touchMisoPin = 39;
constexpr int touchMosiPin = 32;
constexpr int touchCsPin = 33;
constexpr int touchIrqPin = 36;

constexpr int sdSckPin = 18;
constexpr int sdMisoPin = 19;
constexpr int sdMosiPin = 23;
constexpr int sdCsPin = 5;

constexpr uint32_t spiFrequency = 20000000;
}  // namespace board
