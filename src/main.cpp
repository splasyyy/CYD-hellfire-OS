#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include <cmath>

#include "board_config.h"

namespace {
constexpr uint8_t calibrationVersion = 1;
constexpr int calibrationInset = 22;
constexpr int touchSamples = 20;

TFT_eSPI display;
XPT2046_Touchscreen touch(board::touchCsPin, board::touchIrqPin);
Preferences preferences;

struct TouchCalibration {
  uint8_t version;
  float xRaw;
  float xOther;
  float xOffset;
  float yRaw;
  float yOther;
  float yOffset;
};

struct CalibrationPoint {
  TS_Point raw;
  int screenX;
  int screenY;
};

TouchCalibration calibration{};
bool calibrationValid = false;
bool preferencesReady = false;
bool lastTouchDown = false;
const char *sdStatus = "Not checked";

bool solveThreeByThree(double matrix[3][4], double result[3]) {
  for (int column = 0; column < 3; ++column) {
    int pivot = column;
    for (int row = column + 1; row < 3; ++row) {
      if (fabs(matrix[row][column]) > fabs(matrix[pivot][column])) {
        pivot = row;
      }
    }

    if (fabs(matrix[pivot][column]) < 1e-8) {
      return false;
    }

    if (pivot != column) {
      for (int item = column; item < 4; ++item) {
        const double value = matrix[column][item];
        matrix[column][item] = matrix[pivot][item];
        matrix[pivot][item] = value;
      }
    }

    const double divisor = matrix[column][column];
    for (int item = column; item < 4; ++item) {
      matrix[column][item] /= divisor;
    }

    for (int row = 0; row < 3; ++row) {
      if (row == column) {
        continue;
      }
      const double factor = matrix[row][column];
      for (int item = column; item < 4; ++item) {
        matrix[row][item] -= factor * matrix[column][item];
      }
    }
  }

  for (int row = 0; row < 3; ++row) {
    result[row] = matrix[row][3];
  }
  return true;
}

bool fitCalibration(const CalibrationPoint points[4],
                    TouchCalibration &fitted) {
  double sums[3][3]{};
  double xTargets[3]{};
  double yTargets[3]{};

  for (int index = 0; index < 4; ++index) {
    const double values[3] = {static_cast<double>(points[index].raw.x),
                              static_cast<double>(points[index].raw.y), 1.0};
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 3; ++column) {
        sums[row][column] += values[row] * values[column];
      }
      xTargets[row] += values[row] * points[index].screenX;
      yTargets[row] += values[row] * points[index].screenY;
    }
  }

  double xMatrix[3][4]{};
  double yMatrix[3][4]{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      xMatrix[row][column] = sums[row][column];
      yMatrix[row][column] = sums[row][column];
    }
    xMatrix[row][3] = xTargets[row];
    yMatrix[row][3] = yTargets[row];
  }

  double xResult[3]{};
  double yResult[3]{};
  if (!solveThreeByThree(xMatrix, xResult) ||
      !solveThreeByThree(yMatrix, yResult)) {
    return false;
  }

  fitted.version = calibrationVersion;
  fitted.xRaw = static_cast<float>(xResult[0]);
  fitted.xOther = static_cast<float>(xResult[1]);
  fitted.xOffset = static_cast<float>(xResult[2]);
  fitted.yRaw = static_cast<float>(yResult[0]);
  fitted.yOther = static_cast<float>(yResult[1]);
  fitted.yOffset = static_cast<float>(yResult[2]);

  for (int index = 0; index < 4; ++index) {
    const float screenX = fitted.xRaw * points[index].raw.x +
                          fitted.xOther * points[index].raw.y +
                          fitted.xOffset;
    const float screenY = fitted.yRaw * points[index].raw.x +
                          fitted.yOther * points[index].raw.y +
                          fitted.yOffset;
    if (fabsf(screenX - points[index].screenX) > 35.0f ||
        fabsf(screenY - points[index].screenY) > 35.0f) {
      return false;
    }
  }
  return true;
}

bool loadCalibration() {
  if (!preferencesReady ||
      preferences.getBytesLength("cal") != sizeof(TouchCalibration)) {
    return false;
  }

  TouchCalibration saved{};
  if (preferences.getBytes("cal", &saved, sizeof(saved)) != sizeof(saved) ||
      saved.version != calibrationVersion ||
      !isfinite(saved.xRaw) || !isfinite(saved.xOther) ||
      !isfinite(saved.xOffset) || !isfinite(saved.yRaw) ||
      !isfinite(saved.yOther) || !isfinite(saved.yOffset)) {
    return false;
  }

  calibration = saved;
  return true;
}

void drawTarget(int x, int y, const char *label) {
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextDatum(MC_DATUM);
  display.drawString("Touch calibration", board::screenWidth / 2, 20, 2);
  display.drawString(label, board::screenWidth / 2, 42, 2);
  display.drawCircle(x, y, 12, TFT_YELLOW);
  display.drawFastHLine(x - 18, y, 37, TFT_YELLOW);
  display.drawFastVLine(x, y - 18, 37, TFT_YELLOW);
}

bool captureCalibrationPoint(CalibrationPoint &point, int x, int y,
                            const char *label) {
  drawTarget(x, y, label);
  while (touch.touched()) {
    delay(10);
  }

  while (!touch.touched()) {
    delay(10);
  }

  int32_t sumX = 0;
  int32_t sumY = 0;
  int samples = 0;
  for (int index = 0; index < touchSamples; ++index) {
    if (!touch.touched()) {
      break;
    }
    const TS_Point sample = touch.getPoint();
    sumX += sample.x;
    sumY += sample.y;
    ++samples;
    delay(5);
  }
  while (touch.touched()) {
    delay(10);
  }

  if (samples == 0) {
    return false;
  }

  point.raw = TS_Point(sumX / samples, sumY / samples, 0);
  point.screenX = x;
  point.screenY = y;
  return true;
}

bool runCalibration() {
  const int left = calibrationInset;
  const int right = board::screenWidth - calibrationInset;
  const int top = calibrationInset + 35;
  const int bottom = board::screenHeight - calibrationInset;
  const int targetX[4] = {left, right, right, left};
  const int targetY[4] = {top, top, bottom, bottom};
  const char *labels[4] = {"1 of 4", "2 of 4", "3 of 4", "4 of 4"};
  CalibrationPoint points[4]{};

  for (int index = 0; index < 4; ++index) {
    if (!captureCalibrationPoint(points[index], targetX[index], targetY[index],
                                 labels[index])) {
      return false;
    }
  }

  TouchCalibration fitted{};
  if (!fitCalibration(points, fitted)) {
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_RED, TFT_BLACK);
    display.drawCentreString("Calibration failed", board::screenWidth / 2,
                             90, 2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawCentreString("Tap to retry", board::screenWidth / 2, 125, 2);
    delay(1200);
    return false;
  }

  calibration = fitted;
  calibrationValid = true;
  if (preferencesReady) {
    if (preferences.putBytes("cal", &calibration, sizeof(calibration)) !=
        sizeof(calibration)) {
      Serial.println("Calibration works but could not be saved to NVS");
    } else {
      Serial.println("Touch calibration saved to NVS");
    }
  } else {
    Serial.println("Touch calibration works for this session only");
  }
  return true;
}

bool mapTouch(const TS_Point &raw, int &x, int &y) {
  if (!calibrationValid) {
    return false;
  }

  const float mappedX = calibration.xRaw * raw.x +
                        calibration.xOther * raw.y + calibration.xOffset;
  const float mappedY = calibration.yRaw * raw.x +
                        calibration.yOther * raw.y + calibration.yOffset;
  if (!isfinite(mappedX) || !isfinite(mappedY)) {
    return false;
  }

  x = constrain(static_cast<int>(lroundf(mappedX)), 0,
                board::screenWidth - 1);
  y = constrain(static_cast<int>(lroundf(mappedY)), 0,
                board::screenHeight - 1);
  return true;
}

void drawButton(int x, int width, const char *label, uint16_t color) {
  display.fillRoundRect(x, 100, width - 8, 78, 8, color);
  display.drawRoundRect(x, 100, width - 8, 78, 8, TFT_WHITE);
  display.setTextColor(TFT_WHITE, color);
  display.setTextDatum(MC_DATUM);
  display.drawString(label, x + (width - 8) / 2, 139, 2);
}

void drawHome() {
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(TL_DATUM);
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.drawString("CYD Hellfire OS", 10, 8, 2);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("ESP32-2432S028 diagnostics", 10, 31, 1);
  display.drawString("Touch: calibrated", 10, 51, 2);
  display.drawString("SD: ", 10, 78, 2);
  display.drawString(sdStatus, 48, 78, 2);
  drawButton(8, 152, "Calibrate", TFT_BLUE);
  drawButton(160, 152, "Check SD", TFT_DARKGREEN);
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawCentreString("Tap a button to run a check", 160, 202, 1);
}

void appendSdHealthRecord() {
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawCentreString("Checking microSD...", 160, 80, 2);

  SD.end();
  SPI.end();
  SPI.begin(board::sdSckPin, board::sdMisoPin, board::sdMosiPin,
            board::sdCsPin);

  if (!SD.begin(board::sdCsPin, SPI, board::spiFrequency)) {
    sdStatus = "Mount failed";
    Serial.println("SD check failed: card mount failed");
  } else {
    File log = SD.open("/cyd-health.log", FILE_APPEND);
    if (!log) {
      sdStatus = "Write failed";
      Serial.println("SD check failed: cannot open /cyd-health.log");
    } else {
      log.printf("boot_ms=%lu,free_heap=%lu\n",
                 static_cast<unsigned long>(millis()),
                 static_cast<unsigned long>(ESP.getFreeHeap()));
      log.close();
      sdStatus = "Ready; log updated";
      Serial.printf("SD check passed: %llu bytes; health record appended\n",
                    static_cast<unsigned long long>(SD.cardSize()));
    }
    SD.end();
  }

  SPI.end();
  SPI.begin(board::touchSckPin, board::touchMisoPin, board::touchMosiPin,
            board::touchCsPin);
  touch.begin();
}

void handleHomeTouch(int x, int y) {
  if (y < 100 || y > 178) {
    return;
  }

  if (x < 160) {
    runCalibration();
  } else {
    appendSdHealthRecord();
  }
  drawHome();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.printf("Starting CYD diagnostics on %s\n", ESP.getChipModel());

  pinMode(board::backlightPin, OUTPUT);
  digitalWrite(board::backlightPin, HIGH);

  display.init();
  display.setRotation(1);
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawCentreString("Starting hardware checks...", 160, 110, 2);

  SPI.begin(board::touchSckPin, board::touchMisoPin, board::touchMosiPin,
            board::touchCsPin);
  if (!touch.begin()) {
    Serial.println("Touch controller initialization failed");
  }
  touch.setRotation(1);

  preferencesReady = preferences.begin("cyd-health", false);
  if (!preferencesReady) {
    Serial.println("NVS unavailable; calibration will not persist");
  }
  calibrationValid = loadCalibration();
}

void loop() {
  if (!calibrationValid) {
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawCentreString("Calibrate touch", 160, 100, 2);
    display.drawCentreString("Tap the four crosshairs", 160, 130, 2);
    delay(700);
    if (!runCalibration()) {
      delay(250);
      return;
    }
    drawHome();
  }

  const bool isTouchDown = touch.touched();
  if (isTouchDown && !lastTouchDown) {
    int x = 0;
    int y = 0;
    if (mapTouch(touch.getPoint(), x, y)) {
      handleHomeTouch(x, y);
    }
  }
  lastTouchDown = touch.touched();
  delay(20);
}
