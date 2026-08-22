#ifndef BATTERY_TIME_ESTIMATOR_H
#define BATTERY_TIME_ESTIMATOR_H

namespace BatteryTimeEstimator {

constexpr float MIN_RATE_PERCENT_PER_HOUR = 0.5f;
constexpr float TINYS3D_ACTIVE_BASE_MA = 36.0f;
constexpr float TINYS3D_HX711_80SPS_DELTA_MA = 3.0f;
constexpr float TINYS3D_WIFI_DELTA_MA = 42.0f;
constexpr float TINYS3D_CHARGER_MA = 300.0f;
constexpr float TINYS3D_CHARGE_EFFICIENCY = 0.85f;
constexpr const char* TINYS3D_MODEL_NAME = "tinys3d-v1-linear";

inline float clampSoc(float stateOfChargePercent) {
    if (stateOfChargePercent < 0.0f) {
        return 0.0f;
    }
    if (stateOfChargePercent > 100.0f) {
        return 100.0f;
    }
    return stateOfChargePercent;
}

inline float activeLoadMa(bool wifiOn) {
    return TINYS3D_ACTIVE_BASE_MA + TINYS3D_HX711_80SPS_DELTA_MA +
           (wifiOn ? TINYS3D_WIFI_DELTA_MA : 0.0f);
}

inline float netChargeCurrentMa(bool wifiOn) {
    return (TINYS3D_CHARGER_MA * TINYS3D_CHARGE_EFFICIENCY) - activeLoadMa(wifiOn);
}

inline float runtimeMinutesFromCurrent(float capacityMah,
                                       float stateOfChargePercent,
                                       float loadMa) {
    if (capacityMah <= 0.0f || stateOfChargePercent < 0.0f || loadMa <= 0.0f) {
        return -1.0f;
    }
    const float remainingMah = capacityMah * (clampSoc(stateOfChargePercent) / 100.0f);
    return (remainingMah / loadMa) * 60.0f;
}

inline float minutesToTargetFromCurrent(float capacityMah,
                                        float stateOfChargePercent,
                                        float targetPercent,
                                        float netChargeMa) {
    if (capacityMah <= 0.0f || stateOfChargePercent < 0.0f ||
        targetPercent <= 0.0f || netChargeMa <= 0.0f) {
        return -1.0f;
    }
    const float current = clampSoc(stateOfChargePercent);
    const float target = clampSoc(targetPercent);
    if (current >= target) {
        return 0.0f;
    }
    const float requiredMah = capacityMah * ((target - current) / 100.0f);
    return (requiredMah / netChargeMa) * 60.0f;
}

inline float runtimeMinutesFromGaugeRate(float stateOfChargePercent,
                                         float signedRatePercentPerHour) {
    if (stateOfChargePercent < 0.0f ||
        signedRatePercentPerHour > -MIN_RATE_PERCENT_PER_HOUR) {
        return -1.0f;
    }
    return (clampSoc(stateOfChargePercent) / -signedRatePercentPerHour) * 60.0f;
}

inline float minutesToTargetFromGaugeRate(float stateOfChargePercent,
                                          float targetPercent,
                                          float signedRatePercentPerHour) {
    if (stateOfChargePercent < 0.0f || targetPercent <= 0.0f) {
        return -1.0f;
    }
    const float current = clampSoc(stateOfChargePercent);
    const float target = clampSoc(targetPercent);
    if (current >= target) {
        return 0.0f;
    }
    if (signedRatePercentPerHour < MIN_RATE_PERCENT_PER_HOUR) {
        return -1.0f;
    }
    return ((target - current) / signedRatePercentPerHour) * 60.0f;
}

}  // namespace BatteryTimeEstimator

#endif
