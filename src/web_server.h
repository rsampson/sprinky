#pragma once

#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;

void setUpWebServer();
void processWebCommands();  // carry out valve/run/reboot requests from the web UI (loop() only)
void updateAutoSeason();  // switch season profile at calendar season boundaries
