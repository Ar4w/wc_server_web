#pragma once
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h> 

// Универсальный обработчик для перехвата запросов ТОЛЬКО в режиме AP
class CaptiveRequestHandler : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        // Перехватываем запросы только когда устройство работает как точка доступа
        return (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
    }
    
    void handleRequest(AsyncWebServerRequest* request) override;
};

class CaptivePortal {
    DNSServer* _dns;
    AsyncWebServer* _srv;
    bool _active = false;
    void _setupRoutes();
    void _handleSubmit(AsyncWebServerRequest*);
public:
    CaptivePortal(AsyncWebServer* s, DNSServer* d) : _srv(s), _dns(d) {}
    bool begin(const char* ssid, const char* pass = nullptr);
    void loop() { if (_active && _dns) _dns->processNextRequest(); }
    bool isActive() const { return _active; }
};

/*
#pragma once
#include <DNSServer.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Универсальный обработчик для перехвата всех запросов в режиме AP
class CaptiveRequestHandler : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override { return true; }
    void handleRequest(AsyncWebServerRequest* request) override;
};

class CaptivePortal {
    DNSServer* _dns;
    AsyncWebServer* _srv;
    bool _active = false;
    void _setupRoutes();
    void _handleSubmit(AsyncWebServerRequest*);
public:
    CaptivePortal(AsyncWebServer* s, DNSServer* d) : _srv(s), _dns(d) {}
    bool begin(const char* ssid, const char* pass = nullptr);
    void loop() { if (_active && _dns) _dns->processNextRequest(); }
    bool isActive() const { return _active; }
};
*/