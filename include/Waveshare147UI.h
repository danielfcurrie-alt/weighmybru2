#ifndef WAVESHARE_147_UI_H
#define WAVESHARE_147_UI_H

#if defined(BOARD_TINYS3D) || defined(WMBP_HOST_TEST)

#include <Adafruit_GFX.h>

#include "Waveshare147Touch.h"
#include "PourOverSession.h"

class Waveshare147UI : public Adafruit_GFX {
public:
    enum class Page : uint8_t {
        Weight,
        Flow,
        Connect,
        Status,
    };

    struct DashboardData {
        float weightGrams = 0.0f;
        float flowGramsPerSecond = 0.0f;
        uint32_t timerMillis = 0;
        int16_t batteryPercent = -1;
        bool timerRunning = false;
        bool wifiConnected = false;
        bool bluetoothConnected = false;
        bool scaleConnected = false;
        bool fuelGaugeAlert = false;
        float fuelGaugeRatePercentPerHour = 0.0f;
        const char* ipAddress = "";
        const char* statusText = "";
    };

    static constexpr uint16_t WIDTH = JD9853DisplayDriver::PORTRAIT_WIDTH;
    static constexpr uint16_t HEIGHT = JD9853DisplayDriver::PORTRAIT_HEIGHT;
    static constexpr size_t FLOW_HISTORY_SIZE = 96;

    explicit Waveshare147UI(JD9853DisplayDriver& display);
    ~Waveshare147UI();

    bool begin(const char* firmwareVersion = nullptr);
    bool isReady() const { return frameBuffer != nullptr; }
    bool isUsingPsram() const { return usingPsram; }
    bool present();
    uint32_t frameChecksum() const;

    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t width, int16_t height,
                  uint16_t color) override;
    void fillScreen(uint16_t color) override;

    void pushFlowSample(float gramsPerSecond);
    void clearFlowHistory();
    void renderSplash(const char* firmwareVersion = nullptr);
    void render(Page page, const DashboardData& data, const char* appUrl = nullptr);
    void renderWeight(const DashboardData& data);
    void renderFlow(const DashboardData& data);
    bool renderConnect(const char* appUrl);
    void renderStatus(const DashboardData& data);
    void renderPourOver(const PourOverSession& session, const DashboardData& data);

private:
    static constexpr uint16_t COLOR_BACKGROUND = 0x1082;
    static constexpr uint16_t COLOR_PANEL = 0x2104;
    static constexpr uint16_t COLOR_TEXT = 0xffff;
    static constexpr uint16_t COLOR_MUTED = 0xad55;
    static constexpr uint16_t COLOR_TEAL = 0x2e79;
    static constexpr uint16_t COLOR_GREEN = 0x5e88;
    static constexpr uint16_t COLOR_AMBER = 0xfcc0;
    static constexpr uint16_t COLOR_RED = 0xf2a6;
    static constexpr uint16_t COLOR_GRID = 0x4208;

    JD9853DisplayDriver& display;
    uint16_t* frameBuffer;
    bool usingPsram;
    bool dirty;
    float flowHistory[FLOW_HISTORY_SIZE];
    size_t flowWriteIndex;
    size_t flowSampleCount;

    void drawTopBar(const DashboardData& data, const char* title);
    void drawBottomNav(Page activePage);
    void drawFlowGraph(int16_t x, int16_t y, int16_t width, int16_t height,
                       bool showScale);
    void drawCenteredText(const char* text, int16_t y, uint8_t textSize,
                          uint16_t color);
    void drawMetric(const char* label, const char* value, int16_t x, int16_t y,
                    int16_t width, uint16_t color);
    void drawBattery(int16_t x, int16_t y, int16_t percentage, bool alert);
    void drawOfficialLogo(int16_t x, int16_t y);
    void formatTimer(uint32_t milliseconds, char* output, size_t outputLength) const;
};

#endif  // BOARD_TINYS3D || WMBP_HOST_TEST
#endif
