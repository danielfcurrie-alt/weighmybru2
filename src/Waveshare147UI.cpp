#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include "Waveshare147UI.h"
#include "WeighMyBruPlusLogo.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <qrcode.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr int16_t TOP_BAR_HEIGHT = 30;
constexpr int16_t BOTTOM_NAV_HEIGHT = 28;
constexpr uint8_t QR_VERSION = 4;
constexpr uint8_t QR_QUIET_ZONE = 4;
constexpr uint8_t QR_MODULE_SCALE = 4;
constexpr size_t QR_BUFFER_SIZE = 512;

float boundedAbs(float value) {
    return isfinite(value) ? fabsf(value) : 0.0f;
}
}

Waveshare147UI::Waveshare147UI(JD9853DisplayDriver& display)
    : Adafruit_GFX(WIDTH, HEIGHT), display(display), frameBuffer(nullptr),
      usingPsram(false), dirty(false), flowHistory{}, flowWriteIndex(0),
      flowSampleCount(0) {}

Waveshare147UI::~Waveshare147UI() {
    free(frameBuffer);
}

bool Waveshare147UI::begin(const char* firmwareVersion) {
    if (!display.isInitialized() || display.width() != WIDTH ||
        display.height() != HEIGHT) {
        return false;
    }
    if (frameBuffer == nullptr) {
        const size_t bytes = static_cast<size_t>(WIDTH) * HEIGHT * sizeof(uint16_t);
        frameBuffer = static_cast<uint16_t*>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        usingPsram = frameBuffer != nullptr;
        if (frameBuffer == nullptr) {
            frameBuffer = static_cast<uint16_t*>(malloc(bytes));
        }
    }
    if (frameBuffer == nullptr) {
        return false;
    }
    setRotation(0);
    setTextWrap(false);
    renderSplash(firmwareVersion);
    return present();
}

bool Waveshare147UI::present() {
    if (frameBuffer == nullptr || !dirty) {
        return frameBuffer != nullptr;
    }
    const bool written = display.drawRGB565(
        0, 0, WIDTH, HEIGHT, frameBuffer, static_cast<size_t>(WIDTH) * HEIGHT);
    if (written) {
        dirty = false;
    }
    return written;
}

uint32_t Waveshare147UI::frameChecksum() const {
    if (frameBuffer == nullptr) {
        return 0;
    }
    uint32_t hash = 2166136261UL;
    const size_t count = static_cast<size_t>(WIDTH) * HEIGHT;
    for (size_t i = 0; i < count; i++) {
        const uint16_t pixel = frameBuffer[i];
        hash ^= static_cast<uint8_t>(pixel >> 8);
        hash *= 16777619UL;
        hash ^= static_cast<uint8_t>(pixel & 0xff);
        hash *= 16777619UL;
    }
    return hash;
}

void Waveshare147UI::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (frameBuffer == nullptr || x < 0 || y < 0 || x >= width() || y >= height()) {
        return;
    }
    int16_t physicalX = x;
    int16_t physicalY = y;
    switch (getRotation()) {
        case 1:
            physicalX = WIDTH - 1 - y;
            physicalY = x;
            break;
        case 2:
            physicalX = WIDTH - 1 - x;
            physicalY = HEIGHT - 1 - y;
            break;
        case 3:
            physicalX = y;
            physicalY = HEIGHT - 1 - x;
            break;
        default:
            break;
    }
    frameBuffer[static_cast<size_t>(physicalY) * WIDTH + physicalX] = color;
    dirty = true;
}

void Waveshare147UI::drawFastHLine(int16_t x, int16_t y, int16_t lineWidth,
                                   uint16_t color) {
    if (getRotation() != 0) {
        Adafruit_GFX::drawFastHLine(x, y, lineWidth, color);
        return;
    }
    fillRect(x, y, lineWidth, 1, color);
}

void Waveshare147UI::drawFastVLine(int16_t x, int16_t y, int16_t lineHeight,
                                   uint16_t color) {
    if (getRotation() != 0) {
        Adafruit_GFX::drawFastVLine(x, y, lineHeight, color);
        return;
    }
    fillRect(x, y, 1, lineHeight, color);
}

void Waveshare147UI::fillRect(int16_t x, int16_t y, int16_t rectWidth,
                              int16_t rectHeight, uint16_t color) {
    if (frameBuffer == nullptr || rectWidth <= 0 || rectHeight <= 0) {
        return;
    }
    if (getRotation() != 0) {
        Adafruit_GFX::fillRect(x, y, rectWidth, rectHeight, color);
        return;
    }
    if (x < 0) {
        rectWidth += x;
        x = 0;
    }
    if (y < 0) {
        rectHeight += y;
        y = 0;
    }
    if (x >= WIDTH || y >= HEIGHT) {
        return;
    }
    rectWidth = min(rectWidth, static_cast<int16_t>(WIDTH - x));
    rectHeight = min(rectHeight, static_cast<int16_t>(HEIGHT - y));
    if (rectWidth <= 0 || rectHeight <= 0) {
        return;
    }
    for (int16_t row = 0; row < rectHeight; row++) {
        uint16_t* destination = frameBuffer + static_cast<size_t>(y + row) * WIDTH + x;
        for (int16_t column = 0; column < rectWidth; column++) {
            destination[column] = color;
        }
    }
    dirty = true;
}

void Waveshare147UI::fillScreen(uint16_t color) {
    if (frameBuffer == nullptr) {
        return;
    }
    const size_t count = static_cast<size_t>(WIDTH) * HEIGHT;
    for (size_t i = 0; i < count; i++) {
        frameBuffer[i] = color;
    }
    dirty = true;
}

void Waveshare147UI::pushFlowSample(float gramsPerSecond) {
    flowHistory[flowWriteIndex] = isfinite(gramsPerSecond) ? gramsPerSecond : 0.0f;
    flowWriteIndex = (flowWriteIndex + 1) % FLOW_HISTORY_SIZE;
    flowSampleCount = min(flowSampleCount + 1, FLOW_HISTORY_SIZE);
}

void Waveshare147UI::clearFlowHistory() {
    memset(flowHistory, 0, sizeof(flowHistory));
    flowWriteIndex = 0;
    flowSampleCount = 0;
}

void Waveshare147UI::renderSplash(const char* firmwareVersion) {
    fillScreen(COLOR_BACKGROUND);
    drawOfficialLogo((WIDTH - WeighMyBruPlusLogo::WIDTH) / 2, 34);
    drawCenteredText("STARTING", 226, 1, COLOR_TEAL);

    if (firmwareVersion != nullptr && firmwareVersion[0] != '\0') {
        char footer[25];
        snprintf(footer, sizeof(footer), "%.24s", firmwareVersion);
        drawCenteredText(footer, 250, 1, COLOR_MUTED);
    }
    drawFastHLine(39, 286, 94, COLOR_GRID);
    drawFastHLine(39, 286, 31, COLOR_TEAL);
}

void Waveshare147UI::render(Page page, const DashboardData& data, const char* appUrl) {
    switch (page) {
        case Page::Flow:
            renderFlow(data);
            break;
        case Page::Connect:
            renderConnect(appUrl != nullptr ? appUrl : data.ipAddress);
            break;
        case Page::Status:
            renderStatus(data);
            break;
        case Page::Weight:
        default:
            renderWeight(data);
            break;
    }
}

void Waveshare147UI::renderWeight(const DashboardData& data) {
    fillScreen(COLOR_BACKGROUND);
    drawTopBar(data, "WEIGHT");

    char weight[20];
    snprintf(weight, sizeof(weight), "%+.1f", data.weightGrams);
    drawCenteredText(weight, 48, 4, COLOR_TEXT);
    drawCenteredText("grams", 84, 1, COLOR_MUTED);

    drawFastHLine(12, 108, WIDTH - 24, COLOR_GRID);
    char flow[16];
    snprintf(flow, sizeof(flow), "%.1f g/s", data.flowGramsPerSecond);
    drawMetric("FLOW", flow, 8, 122, 76, COLOR_GREEN);
    char timer[16];
    formatTimer(data.timerMillis, timer, sizeof(timer));
    drawMetric(data.timerRunning ? "TIME LIVE" : "TIME", timer, 88, 122, 76,
               data.timerRunning ? COLOR_AMBER : COLOR_TEXT);

    drawFlowGraph(8, 176, WIDTH - 16, 103, false);
    drawBottomNav(Page::Weight);
}

void Waveshare147UI::renderFlow(const DashboardData& data) {
    fillScreen(COLOR_BACKGROUND);
    drawTopBar(data, "FLOW CURVE");

    char flow[20];
    snprintf(flow, sizeof(flow), "%+.2f g/s", data.flowGramsPerSecond);
    drawCenteredText(flow, 42, 2, COLOR_GREEN);
    drawFlowGraph(8, 76, WIDTH - 16, 186, true);

    char weight[20];
    snprintf(weight, sizeof(weight), "%.1f g", data.weightGrams);
    setTextSize(1);
    setTextColor(COLOR_MUTED);
    setCursor(9, 272);
    print(weight);
    char timer[16];
    formatTimer(data.timerMillis, timer, sizeof(timer));
    setCursor(117, 272);
    print(timer);
    drawBottomNav(Page::Flow);
}

bool Waveshare147UI::renderConnect(const char* appUrl) {
    fillScreen(COLOR_BACKGROUND);
    DashboardData header;
    header.wifiConnected = appUrl != nullptr && appUrl[0] != '\0';
    drawTopBar(header, "WEB APP");

    if (appUrl == nullptr || appUrl[0] == '\0') {
        drawCenteredText("NO NETWORK", 130, 2, COLOR_RED);
        drawBottomNav(Page::Connect);
        return false;
    }

    uint8_t qrData[QR_BUFFER_SIZE];
    QRCode qrCode;
    const int8_t result = qrcode_initText(&qrCode, qrData, QR_VERSION, ECC_LOW, appUrl);
    if (result != 0) {
        drawCenteredText("QR ERROR", 130, 2, COLOR_RED);
        drawBottomNav(Page::Connect);
        return false;
    }

    const int16_t qrPixels = (qrCode.size + QR_QUIET_ZONE * 2) * QR_MODULE_SCALE;
    const int16_t originX = (WIDTH - qrPixels) / 2;
    const int16_t originY = 39;
    fillRect(originX, originY, qrPixels, qrPixels, COLOR_TEXT);
    for (uint8_t y = 0; y < qrCode.size; y++) {
        for (uint8_t x = 0; x < qrCode.size; x++) {
            if (qrcode_getModule(&qrCode, x, y)) {
                fillRect(originX + (x + QR_QUIET_ZONE) * QR_MODULE_SCALE,
                         originY + (y + QR_QUIET_ZONE) * QR_MODULE_SCALE,
                         QR_MODULE_SCALE, QR_MODULE_SCALE, 0x0000);
            }
        }
    }

    drawCenteredText("OPEN WMB+", 212, 1, COLOR_TEAL);
    setTextSize(1);
    setTextColor(COLOR_MUTED);
    int16_t boundsX = 0;
    int16_t boundsY = 0;
    uint16_t boundsWidth = 0;
    uint16_t boundsHeight = 0;
    getTextBounds(appUrl, 0, 0, &boundsX, &boundsY, &boundsWidth, &boundsHeight);
    const char* displayedUrl = appUrl;
    if (boundsWidth > WIDTH - 8) {
        const char* scheme = strstr(appUrl, "://");
        displayedUrl = scheme != nullptr ? scheme + 3 : appUrl;
        getTextBounds(displayedUrl, 0, 0, &boundsX, &boundsY, &boundsWidth, &boundsHeight);
    }
    setCursor(max(4, (WIDTH - static_cast<int16_t>(boundsWidth)) / 2), 230);
    print(displayedUrl);
    drawBottomNav(Page::Connect);
    return true;
}

void Waveshare147UI::renderStatus(const DashboardData& data) {
    fillScreen(COLOR_BACKGROUND);
    drawTopBar(data, "SYSTEM");

    setTextSize(1);
    const char* labels[] = {"SCALE", "WIFI", "BLUETOOTH", "BATTERY"};
    const bool states[] = {data.scaleConnected, data.wifiConnected,
                           data.bluetoothConnected, data.batteryPercent >= 0};
    for (size_t i = 0; i < 4; i++) {
        const int16_t y = 48 + static_cast<int16_t>(i) * 37;
        setTextColor(COLOR_MUTED);
        setCursor(12, y);
        print(labels[i]);
        setTextColor(states[i] ? COLOR_GREEN : COLOR_RED);
        setCursor(112, y);
        print(states[i] ? "READY" : "OFF");
        drawFastHLine(12, y + 18, WIDTH - 24, COLOR_GRID);
    }

    char gaugeRate[24];
    snprintf(gaugeRate, sizeof(gaugeRate), "%+.1f %%/hr",
             data.fuelGaugeRatePercentPerHour);
    drawMetric("BATTERY TREND", gaugeRate, 12, 202, 148,
               data.fuelGaugeAlert ? COLOR_RED : COLOR_AMBER);
    if (data.statusText != nullptr && data.statusText[0] != '\0') {
        drawCenteredText(data.statusText, 258, 1,
                         data.fuelGaugeAlert ? COLOR_RED : COLOR_MUTED);
    }
    drawBottomNav(Page::Status);
}

void Waveshare147UI::renderPourOver(const PourOverSession& session,
                                    const DashboardData& data) {
    fillScreen(COLOR_BACKGROUND);
    drawTopBar(data, "POUR OVER");

    const PourOverStage* stage = session.currentStage();
    if (stage == nullptr) {
        drawCenteredText("OPEN WEB APP", 118, 2, COLOR_TEAL);
        drawCenteredText("CHOOSE A RECIPE", 151, 1, COLOR_MUTED);
        char total[16];
        formatTimer(session.totalElapsedMs(), total, sizeof(total));
        drawCenteredText(total, 197, 2, COLOR_TEXT);
        drawBottomNav(Page::Connect);
        return;
    }

    uint16_t stageColor = COLOR_AMBER;
    switch (stage->type) {
        case PourOverStageType::Pour: stageColor = COLOR_TEAL; break;
        case PourOverStageType::Agitate: stageColor = COLOR_RED; break;
        case PourOverStageType::Drawdown: stageColor = COLOR_GREEN; break;
        case PourOverStageType::Pause: default: stageColor = COLOR_AMBER; break;
    }

    char position[20];
    snprintf(position, sizeof(position), "%u OF %u",
             static_cast<unsigned>(session.currentStageIndex() + 1),
             static_cast<unsigned>(session.recipe().stageCount));
    setTextSize(1);
    setTextColor(stageColor);
    setCursor(10, 43);
    print(PourOverSession::stageTypeName(stage->type));
    setTextColor(COLOR_MUTED);
    setCursor(122, 43);
    print(position);

    char stageName[25];
    snprintf(stageName, sizeof(stageName), "%.24s", stage->name);
    drawCenteredText(stageName, 61, 2, COLOR_TEXT);

    char stageTimer[16];
    formatTimer(session.stageElapsedMs(), stageTimer, sizeof(stageTimer));
    drawCenteredText(stageTimer, 99, 4,
                     session.status() == PourOverStatus::Paused ? COLOR_AMBER : stageColor);
    drawCenteredText(session.status() == PourOverStatus::Paused ? "PAUSED" : "STAGE TIME",
                     137, 1, COLOR_MUTED);

    float progress = 0.0f;
    if (stage->type == PourOverStageType::Pour && stage->targetGrams > 0.0f) {
        progress = session.currentAddedGrams() / stage->targetGrams;
    } else if (stage->durationMs > 0) {
        progress = static_cast<float>(session.stageElapsedMs()) /
                   static_cast<float>(stage->durationMs);
    }
    progress = constrain(progress, 0.0f, 1.0f);
    fillRect(10, 158, WIDTH - 20, 8, COLOR_GRID);
    fillRect(10, 158, static_cast<int16_t>((WIDTH - 20) * progress), 8, stageColor);

    char weight[20];
    if (stage->type == PourOverStageType::Pour) {
        snprintf(weight, sizeof(weight), "%.0f / %.0f g",
                 session.currentAddedGrams(), stage->targetGrams);
    } else {
        snprintf(weight, sizeof(weight), "%.1f g", data.weightGrams);
    }
    drawMetric(stage->type == PourOverStageType::Pour ? "STAGE WEIGHT" : "WEIGHT",
               weight, 10, 183, 76, stageColor);

    char flow[18];
    snprintf(flow, sizeof(flow), "%.1f g/s", data.flowGramsPerSecond);
    drawMetric("FLOW", flow, 94, 183, 68, COLOR_GREEN);

    char total[16];
    formatTimer(session.totalElapsedMs(), total, sizeof(total));
    setTextSize(1);
    setTextColor(COLOR_MUTED);
    setCursor(10, 241);
    print("TOTAL");
    setTextColor(COLOR_TEXT);
    setCursor(50, 241);
    print(total);
    setTextColor(COLOR_MUTED);
    setCursor(105, 241);
    print(session.status() == PourOverStatus::Finished ? "DONE" : "PHONE CTRL");

    drawFlowGraph(10, 258, WIDTH - 20, 31, false);
    drawBottomNav(Page::Flow);
}

void Waveshare147UI::drawTopBar(const DashboardData& data, const char* title) {
    fillRect(0, 0, WIDTH, TOP_BAR_HEIGHT, COLOR_PANEL);
    setTextSize(1);
    setTextColor(COLOR_TEXT);
    setCursor(8, 11);
    print(title);

    const uint16_t wifiColor = data.wifiConnected ? COLOR_TEAL : COLOR_GRID;
    const uint16_t bluetoothColor = data.bluetoothConnected ? COLOR_GREEN : COLOR_GRID;
    fillCircle(118, 15, 4, wifiColor);
    fillCircle(132, 15, 4, bluetoothColor);
    drawBattery(145, 9, data.batteryPercent, data.fuelGaugeAlert);
}

void Waveshare147UI::drawBottomNav(Page activePage) {
    const int16_t y = HEIGHT - BOTTOM_NAV_HEIGHT;
    fillRect(0, y, WIDTH, BOTTOM_NAV_HEIGHT, COLOR_PANEL);
    const char* labels[] = {"WT", "FLOW", "QR", "SYS"};
    const int16_t centers[] = {22, 66, 108, 151};
    setTextSize(1);
    for (uint8_t i = 0; i < 4; i++) {
        const bool active = static_cast<uint8_t>(activePage) == i;
        setTextColor(active ? COLOR_TEAL : COLOR_MUTED);
        int16_t x = centers[i] - static_cast<int16_t>(strlen(labels[i]) * 3);
        setCursor(x, y + 10);
        print(labels[i]);
        if (active) {
            drawFastHLine(centers[i] - 9, y + 23, 18, COLOR_TEAL);
        }
    }
}

void Waveshare147UI::drawFlowGraph(int16_t x, int16_t y, int16_t graphWidth,
                                   int16_t graphHeight, bool showScale) {
    fillRect(x, y, graphWidth, graphHeight, COLOR_PANEL);
    for (uint8_t division = 1; division < 4; division++) {
        const int16_t gridY = y + division * graphHeight / 4;
        drawFastHLine(x, gridY, graphWidth, COLOR_GRID);
    }
    for (uint8_t division = 1; division < 4; division++) {
        const int16_t gridX = x + division * graphWidth / 4;
        drawFastVLine(gridX, y, graphHeight, COLOR_GRID);
    }
    if (flowSampleCount < 2) {
        drawFastHLine(x, y + graphHeight - 8, graphWidth, COLOR_MUTED);
        return;
    }

    float maximum = 1.0f;
    for (size_t i = 0; i < flowSampleCount; i++) {
        const size_t index = (flowWriteIndex + FLOW_HISTORY_SIZE - flowSampleCount + i) %
                             FLOW_HISTORY_SIZE;
        maximum = max(maximum, boundedAbs(flowHistory[index]));
    }
    maximum = ceilf(maximum * 2.0f) / 2.0f;
    const int16_t centerY = y + graphHeight / 2;
    drawFastHLine(x, centerY, graphWidth, COLOR_MUTED);

    int16_t previousX = x;
    int16_t previousY = centerY;
    for (size_t i = 0; i < flowSampleCount; i++) {
        const size_t index = (flowWriteIndex + FLOW_HISTORY_SIZE - flowSampleCount + i) %
                             FLOW_HISTORY_SIZE;
        const float normalized = constrain(flowHistory[index] / maximum, -1.0f, 1.0f);
        const int16_t pointX = x + static_cast<int16_t>(
            i * static_cast<size_t>(graphWidth - 1) / (flowSampleCount - 1));
        const int16_t pointY = centerY - static_cast<int16_t>(
            normalized * (graphHeight / 2 - 5));
        if (i > 0) {
            drawLine(previousX, previousY, pointX, pointY, COLOR_GREEN);
        }
        previousX = pointX;
        previousY = pointY;
    }

    if (showScale) {
        char scale[12];
        snprintf(scale, sizeof(scale), "%.1f", maximum);
        setTextSize(1);
        setTextColor(COLOR_MUTED);
        setCursor(x + 4, y + 4);
        print(scale);
        setCursor(x + 4, y + graphHeight - 11);
        print('-');
        print(scale);
    }
}

void Waveshare147UI::drawCenteredText(const char* text, int16_t y, uint8_t textSize,
                                      uint16_t color) {
    if (text == nullptr) {
        return;
    }
    setTextSize(textSize);
    setTextColor(color);
    int16_t boundsX = 0;
    int16_t boundsY = 0;
    uint16_t boundsWidth = 0;
    uint16_t boundsHeight = 0;
    getTextBounds(text, 0, y, &boundsX, &boundsY, &boundsWidth, &boundsHeight);
    setCursor(max(0, (WIDTH - static_cast<int16_t>(boundsWidth)) / 2), y);
    print(text);
}

void Waveshare147UI::drawMetric(const char* label, const char* value, int16_t x,
                                int16_t y, int16_t metricWidth, uint16_t color) {
    setTextSize(1);
    setTextColor(COLOR_MUTED);
    setCursor(x, y);
    print(label);
    setTextColor(color);
    setCursor(x, y + 17);
    print(value);
    drawFastHLine(x, y + 31, metricWidth, COLOR_GRID);
}

void Waveshare147UI::drawBattery(int16_t x, int16_t y, int16_t percentage, bool alert) {
    const uint16_t color = alert ? COLOR_RED : percentage >= 0 ? COLOR_TEXT : COLOR_GRID;
    drawRect(x, y, 18, 11, color);
    fillRect(x + 18, y + 3, 2, 5, color);
    if (percentage >= 0) {
        const int16_t fillWidth = constrain((percentage * 14) / 100, 0, 14);
        fillRect(x + 2, y + 2, fillWidth, 7,
                 percentage <= 15 ? COLOR_RED : percentage <= 35 ? COLOR_AMBER : COLOR_GREEN);
    }
}

void Waveshare147UI::drawOfficialLogo(int16_t x, int16_t y) {
    if (frameBuffer == nullptr || x < 0 || y < 0 ||
        x + WeighMyBruPlusLogo::WIDTH > WIDTH ||
        y + WeighMyBruPlusLogo::HEIGHT > HEIGHT) {
        return;
    }
    size_t pixelIndex = 0;
    for (uint16_t row = 0; row < WeighMyBruPlusLogo::HEIGHT; row++) {
        uint16_t* destination = frameBuffer +
            static_cast<size_t>(y + row) * WIDTH + x;
        for (uint16_t column = 0; column < WeighMyBruPlusLogo::WIDTH; column++) {
            const uint8_t packed = pgm_read_byte(
                &WeighMyBruPlusLogo::PIXELS[pixelIndex / 2]);
            const uint8_t paletteIndex = (pixelIndex & 1) == 0
                ? static_cast<uint8_t>(packed >> 4)
                : static_cast<uint8_t>(packed & 0x0f);
            destination[column] = pgm_read_word(
                &WeighMyBruPlusLogo::PALETTE[paletteIndex]);
            pixelIndex++;
        }
    }
    dirty = true;
}

void Waveshare147UI::formatTimer(uint32_t milliseconds, char* output,
                                 size_t outputLength) const {
    const uint32_t seconds = milliseconds / 1000;
    snprintf(output, outputLength, "%lu:%02lu",
             static_cast<unsigned long>(seconds / 60),
             static_cast<unsigned long>(seconds % 60));
}

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
