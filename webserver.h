#pragma once
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class WebInterface {
    AsyncWebServer* _srv;
    void _setupRoutes();
    void _setupApi();
    void _handleReadings(AsyncWebServerRequest*);
    void _handleSet(AsyncWebServerRequest*);
    void _handlePush(AsyncWebServerRequest*);
    void _handleConfig(AsyncWebServerRequest*);
    void _handleStatus(AsyncWebServerRequest*);
    void _handleExport(AsyncWebServerRequest*);
    void _handleImport(AsyncWebServerRequest*);
    void _handleForceFetch(AsyncWebServerRequest*);
    
public:
    WebInterface(AsyncWebServer* s) : _srv(s) {}
    bool begin();
    void loop() {}
    void notifyValuesChanged() {}
    void notifyEircFetched() {}
};

void fetchEircTask(void* parameter);