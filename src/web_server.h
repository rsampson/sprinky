#pragma once

#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;

void setUpWebServer();
void updateAutoSeason();  // switch season profile at calendar season boundaries
