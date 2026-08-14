#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "Scale.h"
#include "FlowRate.h"
#include "BluetoothScale.h"
#include "Display.h"
#include "BatteryMonitor.h"
#include "SmbComms.h"
#include "PowerManager.h"
#include "DiagnosticEventLog.h"
#include "BoardHardware.h"
#include "BatteryDrainSession.h"

extern float calibrationFactor;

void setupWebServer(Scale &scale, FlowRate &flowRate, BluetoothScale &bluetoothScale, Display &display, BatteryMonitor &battery, SmbComms &smb, PowerManager &powerManager, DiagnosticEventLog &diagnosticEvents, BoardHardware &boardHardware, BatteryDrainSession &batteryDrainSession);
void startWebServer();
void stopWebServer();

#endif
