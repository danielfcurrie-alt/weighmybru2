#ifndef SIMULATION_PROFILES_H
#define SIMULATION_PROFILES_H

#include <stdint.h>
#include <math.h>

#define WMBP_SIM_SCENARIO_IDLE 1
#define WMBP_SIM_SCENARIO_SHOT 2
#define WMBP_SIM_SCENARIO_DRIFT 3
#define WMBP_SIM_SCENARIO_GLITCH 4
#define WMBP_SIM_SCENARIO_BUMP 5

#define WMBP_SIM_BATTERY_PROFILE_BASELINE 1
#define WMBP_SIM_BATTERY_PROFILE_WIFI_AP 2
#define WMBP_SIM_BATTERY_PROFILE_CHARGING 3
#define WMBP_SIM_BATTERY_PROFILE_SLOW_CHARGE 4
#define WMBP_SIM_BATTERY_PROFILE_FLAT 5

#ifndef WMBP_SIM_HX711_HZ
  #define WMBP_SIM_HX711_HZ 80
#endif

#ifndef WMBP_SIM_SCENARIO
  #define WMBP_SIM_SCENARIO WMBP_SIM_SCENARIO_SHOT
#endif

#ifndef WMBP_SIM_BATTERY_PROFILE
  #define WMBP_SIM_BATTERY_PROFILE WMBP_SIM_BATTERY_PROFILE_BASELINE
#endif

class SimulationProfiles {
public:
    static uint32_t sampleIntervalMicros(int hz) {
        if (hz <= 0) {
            hz = 80;
        }
        return static_cast<uint32_t>(1000000UL / static_cast<uint32_t>(hz));
    }

    static float weightForScenario(uint32_t elapsedMs, int scenario, int hx711Hz) {
        const float t = static_cast<float>(elapsedMs) / 1000.0f;
        float grams = 0.0f;

        switch (scenario) {
            case WMBP_SIM_SCENARIO_IDLE:
                grams = 0.0f;
                break;

            case WMBP_SIM_SCENARIO_DRIFT:
                grams = t * 0.015f;
                break;

            case WMBP_SIM_SCENARIO_GLITCH:
                grams = shotWeight(t);
                if (fabsf(t - 12.68f) <= sampleWindowSeconds(hx711Hz)) {
                    grams = -591.13f;
                }
                break;

            case WMBP_SIM_SCENARIO_BUMP:
                grams = shotWeight(t);
                if (t >= 12.00f && t <= 12.00f + (4.0f * sampleWindowSeconds(hx711Hz))) {
                    grams += 8.0f;
                }
                break;

            case WMBP_SIM_SCENARIO_SHOT:
            default:
                grams = shotWeight(t);
                break;
        }

        return grams + ripple(t);
    }

    static float batteryVoltage(uint32_t elapsedMs, int profile) {
        const float hours = static_cast<float>(elapsedMs) / 3600000.0f;
        const float startVoltage = 3.950f;

        switch (profile) {
            case WMBP_SIM_BATTERY_PROFILE_WIFI_AP:
                return clampVoltage(startVoltage - (0.100f * hours));
            case WMBP_SIM_BATTERY_PROFILE_CHARGING:
                return clampVoltage(startVoltage + (0.180f * hours));
            case WMBP_SIM_BATTERY_PROFILE_SLOW_CHARGE:
                return clampVoltage(startVoltage + (0.035f * hours));
            case WMBP_SIM_BATTERY_PROFILE_FLAT:
                return startVoltage;
            case WMBP_SIM_BATTERY_PROFILE_BASELINE:
            default:
                return clampVoltage(startVoltage - (0.040f * hours));
        }
    }

    static bool simulatedUsbPowerPresent(int profile) {
        return profile == WMBP_SIM_BATTERY_PROFILE_CHARGING ||
               profile == WMBP_SIM_BATTERY_PROFILE_SLOW_CHARGE;
    }

    static const char* scenarioName(int scenario) {
        switch (scenario) {
            case WMBP_SIM_SCENARIO_IDLE: return "idle";
            case WMBP_SIM_SCENARIO_SHOT: return "shot";
            case WMBP_SIM_SCENARIO_DRIFT: return "drift";
            case WMBP_SIM_SCENARIO_GLITCH: return "glitch";
            case WMBP_SIM_SCENARIO_BUMP: return "bump";
            default: return "unknown";
        }
    }

    static const char* batteryProfileName(int profile) {
        switch (profile) {
            case WMBP_SIM_BATTERY_PROFILE_BASELINE: return "baseline";
            case WMBP_SIM_BATTERY_PROFILE_WIFI_AP: return "wifi_ap";
            case WMBP_SIM_BATTERY_PROFILE_CHARGING: return "charging";
            case WMBP_SIM_BATTERY_PROFILE_SLOW_CHARGE: return "slow_charge";
            case WMBP_SIM_BATTERY_PROFILE_FLAT: return "flat";
            default: return "unknown";
        }
    }

private:
    static float shotWeight(float t) {
        if (t < 3.0f) {
            return 0.0f;
        }
        if (t < 21.0f) {
            return (t - 3.0f) * 2.1f;
        }
        if (t < 26.0f) {
            return 37.8f;
        }
        return 37.8f - fminf(3.0f, (t - 26.0f) * 0.15f);
    }

    static float ripple(float t) {
        return 0.025f * sinf(t * 7.0f) + 0.010f * sinf(t * 19.0f);
    }

    static float sampleWindowSeconds(int hx711Hz) {
        if (hx711Hz <= 0) {
            hx711Hz = 80;
        }
        return 0.55f / static_cast<float>(hx711Hz);
    }

    static float clampVoltage(float voltage) {
        if (voltage < 3.2f) {
            return 3.2f;
        }
        if (voltage > 4.2f) {
            return 4.2f;
        }
        return voltage;
    }
};

#endif
