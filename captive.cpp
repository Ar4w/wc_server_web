#include "captive.h"
#include "config.h"
#include "storage.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_log.h>

bool CaptivePortal::begin(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, pass);
    IPAddress apIP = WiFi.softAPIP();
    ESP_LOGI(LOG_TAG, "AP started. IP: %s | SSID: %s", apIP.toString().c_str(), ssid);

    if (_dns) {
        _dns->start(53, "*", apIP);
        ESP_LOGI(LOG_TAG, "DNS server started (wildcard redirect to %s)", apIP.toString().c_str());
    }

    _setupRoutes();
    _srv->begin();
    ESP_LOGI(LOG_TAG, "AsyncWebServer started and listening on port %d", WEB_SERVER_PORT);
    
    _active = true;
    return true;
}

/*
bool CaptivePortal::begin(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_AP);
    //WiFi.setTxPower(WIFI_POWER_19_5dBm); // Фиксированная мощность
    //esp_wifi_set_ant(WIFI_ANT_MODE_ANT0, WIFI_ANT_MODE_ANT0); // Принудительно основная антенна
    WiFi.softAP(ssid, pass);
    IPAddress apIP = WiFi.softAPIP();
    ESP_LOGI(LOG_TAG, "AP started. IP: %s | SSID: %s", apIP.toString().c_str(), ssid);

    if (_dns) {
        _dns->start(53, "*", apIP);
        ESP_LOGI(LOG_TAG, "DNS server started (wildcard redirect to %s)", apIP.toString().c_str());
    }

    _setupRoutes();
    
    // ⚠️ КРИТИЧЕСКИ ВАЖНО: Запуск TCP-прослушивателя на порту 80
    _srv->begin();
    ESP_LOGI(LOG_TAG, "AsyncWebServer started and listening on port %d", WEB_SERVER_PORT);
    
    _active = true;
    return true;
}
*/
void CaptivePortal::_setupRoutes() {
    if (!_srv) return;

    // Главная страница конфигурации
    _srv->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (LittleFS.exists("/config.html")) {
            r->send(LittleFS, "/config.html", "text/html");
        } else {
            r->send(200, "text/html", 
                "<!DOCTYPE html>"
                "<html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no'>"
                "<title>Настройка WC Server</title>"
                "<style>"
                "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #f0f2f5; color: #333; margin: 0; padding: 20px; display: flex; justify-content: center; }"
                ".container { background: #fff; padding: 30px 25px; border-radius: 16px; box-shadow: 0 4px 20px rgba(0,0,0,0.08); width: 100%; max-width: 600px; }"
                "h1 { text-align: center; color: #1976D2; margin-top: 0; font-size: 26px; margin-bottom: 25px; }"
                ".form-group { margin-bottom: 20px; }"
                "label { display: block; font-weight: 600; margin-bottom: 8px; font-size: 16px; color: #444; }"
                "input { width: 100%; padding: 14px; font-size: 16px; border: 2px solid #e0e0e0; border-radius: 10px; box-sizing: border-box; transition: border-color 0.2s; background: #fafafa; }"
                "input:focus { border-color: #1976D2; outline: none; background: #fff; }"
                "button { width: 100%; padding: 16px; font-size: 18px; font-weight: bold; color: #fff; background: #1976D2; border: none; border-radius: 10px; cursor: pointer; margin-top: 15px; transition: background 0.2s; }"
                "button:active { background: #1565C0; transform: scale(0.98); }"
                ".hint { font-size: 13px; color: #777; margin-top: 6px; line-height: 1.4; }"
                "</style></head><body>"
                "<div class='container'>"
                "<h1>💧 Настройка WC Server</h1>"
                "<form method='POST' action='/'>"
                "<div class='form-group'><label>📶 Имя сети Wi-Fi (SSID)</label><input name='ssid' placeholder='Например: Home_WiFi' required></div>"
                "<div class='form-group'><label>🔒 Пароль Wi-Fi</label><input name='pass' type='password' placeholder='Пароль от Wi-Fi'></div>"
                "<div class='form-group'><label>👤 Логин МОСОБЛЕИРЦ (телефон)</label><input name='eirc_login' placeholder='+7...' required></div>"
                "<div class='form-group'><label>🔑 Пароль МОСОБЛЕИРЦ</label><input name='eirc_pass' type='password' required></div>"
                "<div class='form-group'><label>🔥 ID счётчика горячей воды</label><input name='hot_id' type='number' placeholder='Числовой ID из кабинета' required></div>"
                "<div class='form-group'><label>❄️ ID счётчика холодной воды</label><input name='cold_id' type='number' placeholder='Числовой ID из кабинета' required></div>"
                "<p class='hint'>💡 Чтобы узнать ID счётчиков, используйте скрипт <code>get_data_from_eirc.py</code> на компьютере.</p>"
                "<button type='submit'>💾 Сохранить и перезагрузить</button>"
                "</form></div></body></html>");
        }
    });

    // Обработка формы
    _srv->on("/", HTTP_POST, [this](AsyncWebServerRequest* r) { _handleSubmit(r); });

    // API сканирования сетей
    _srv->on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* r) {
        int n = WiFi.scanNetworks();
        String j = "[";
        for (int i = 0; i < n; ++i) {
            if (i > 0) j += ",";
            j += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        }
        j += "]";
        WiFi.scanDelete();
        r->send(200, "application/json", j);
    });

    // ВАЖНО: Регистрируем catch-all обработчик ПОСЛЕ всех конкретных маршрутов
    _srv->addHandler(new CaptiveRequestHandler());
    ESP_LOGI(LOG_TAG, "Captive portal catch-all handler registered");
    
}

void CaptivePortal::_handleSubmit(AsyncWebServerRequest* r) {
    Storage s;
    if (!s.begin(NVS_NAMESPACE)) { r->send(500, "text/plain", "NVS error"); return; }
    
    String ssid = r->arg("ssid"), pass = r->arg("pass");
    String el = r->arg("eirc_login"), ep = r->arg("eirc_pass");
    String hid = r->arg("hot_id"), cid = r->arg("cold_id");

    if (ssid.length() < 2 || el.length() < 3) { 
        r->send(400, "text/html", "<h1>⚠️ Ошибка</h1><p>Заполните SSID и данные ЕИРЦ</p><a href='/'>Назад</a>"); 
        return; 
    }

    s.saveWiFi(ssid.c_str(), pass.c_str());
    s.saveEircCredentials(el.c_str(), ep.c_str());
    s.saveMeterIds(hid.toInt(), cid.toInt());
    s.setConfigured(true);
    s.end();

    r->send(200, "text/html", 
        "<!DOCTYPE html><html><head><meta charset='utf-8'><meta http-equiv='refresh' content='3;url=http://192.168.4.1'>"
        "<title>Сохранено</title></head><body style='text-align:center;padding-top:20%'>"
        "<h1>✅ Настройки сохранены</h1><p>Устройство перезагружается и подключается к Wi-Fi...</p>"
        "<p>Если переадресация не сработала, перейдите по адресу вашей основной сети после подключения.</p></body></html>");
    
    delay(1500);
    ESP.restart();
}

// Реализация перенаправления для captive portal
void CaptiveRequestHandler::handleRequest(AsyncWebServerRequest* r) {
    // Перенаправляем ВСЕ неизвестные запросы на главную страницу
    // Это стандартный способ активации captive portal на iOS/Android
    IPAddress apIP = WiFi.softAPIP();
    r->redirect("http://" + apIP.toString());
    //r->redirect("http://192.168.4.1");
}