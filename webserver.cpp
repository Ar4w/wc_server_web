#include "webserver.h"
#include "config.h"
#include "storage.h"
#include "mosobleirc.h"
#include <LittleFS.h>
#include <time.h>
#include <WiFi.h> // Добавлено для доступа к WiFi.*
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


bool WebInterface::begin() {
    if (!_srv) return false;
    _srv->serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    _setupRoutes();
    _setupApi();
    _srv->begin();
    return true;
}

void WebInterface::_setupRoutes() {
    _srv->on("/restart", HTTP_POST, [](AsyncWebServerRequest* r){
        r->send(200, "text/plain", "OK");
        delay(500);
        ESP.restart();
    });
    _srv->on("/reset", HTTP_POST, [](AsyncWebServerRequest* r){
        Storage s;
        if (s.begin(NVS_NAMESPACE)) s.clearAll();
        s.end();
        r->send(200, "text/plain", "OK");
        delay(500);
        ESP.restart();
    });
}

void WebInterface::_setupApi() {
    _srv->on("/api/readings", HTTP_GET, [this](AsyncWebServerRequest* r){ _handleReadings(r); });
    _srv->on("/api/set", HTTP_POST, [this](AsyncWebServerRequest* r){ _handleSet(r); });
    _srv->on("/api/push", HTTP_POST, [this](AsyncWebServerRequest* r){ _handlePush(r); });
    _srv->on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* r){ _handleConfig(r); });
    _srv->on("/api/config", HTTP_POST, [this](AsyncWebServerRequest* r){ _handleConfig(r); });
    _srv->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* r){ _handleStatus(r); }); 
    _srv->on("/api/fetch_eirc", HTTP_GET, [this](AsyncWebServerRequest* r){ _handleForceFetch(r); });
    _srv->on("/api/export", HTTP_GET, [this](AsyncWebServerRequest* r){ _handleExport(r); });
    _srv->on("/api/import", HTTP_POST, [this](AsyncWebServerRequest* r){ _handleImport(r); });
}

void WebInterface::_handleReadings(AsyncWebServerRequest* r) {
    StaticJsonDocument<256> d;
    d["hot"] = g_hot_value;
    d["cold"] = g_cold_value;
    d["last_sent"] = g_last_sent_date;
    d["changed"] = g_values_changed;
    String js; serializeJson(d, js);
    r->send(200, "application/json", js);
}

void WebInterface::_handleSet(AsyncWebServerRequest* r) {
    if (!r->hasParam("hot", true) || !r->hasParam("cold", true)) {
        r->send(400, "text/plain", "Missing"); return;
    }
    g_hot_value = r->getParam("hot", true)->value().toInt();
    g_cold_value = r->getParam("cold", true)->value().toInt();
    g_values_changed = true;
    Storage s;
    if (s.begin(NVS_NAMESPACE)) {
        s.saveReadings(g_hot_value, g_cold_value, g_last_sent_date);
        s.end();
    }
    r->send(200, "text/plain", "OK");
}

void WebInterface::_handlePush(AsyncWebServerRequest* r) {
    if (!eirc) { r->send(500, "text/plain", "EIRC null"); return; }
    struct tm ti;
    if (!getLocalTime(&ti)) { r->send(500, "text/plain", "Time err"); return; }
    uint32_t cd = Mosobleirc::convertToInternalDate(ti.tm_year + 1900, ti.tm_mon + 1);
    if (cd <= g_last_sent_date) { r->send(400, "text/plain", "Already sent"); return; }
    if (ti.tm_mday < eirc->global_date_from || ti.tm_mday > eirc->global_date_to) {
        r->send(400, "text/plain", "Not in window"); return;
    }
    int h = eirc->submit(eirc->getHotId(), g_hot_value);
    int c = eirc->submit(eirc->getColdId(), g_cold_value);
    if (h == 3 && c == 3) {
        r->send(200, "text/plain", "DRY_RUN_OK");
    } else if (h == 1 && c == 1) {
        g_last_sent_date = cd; 
        g_values_changed = false;
        Storage s; 
        if (s.begin(NVS_NAMESPACE)) { 
            s.saveReadings(g_hot_value, g_cold_value, g_last_sent_date); 
            s.end(); 
        }
        r->send(200, "text/plain", "OK_SENT");
    } else if (h == 2 || c == 2) {
        r->send(400, "text/plain", "LIMIT");
    } else {
        r->send(500, "text/plain", "EIRC_ERR");
    }
}

void WebInterface::_handleConfig(AsyncWebServerRequest* r) {
    Storage s;
    if (!s.begin(NVS_NAMESPACE)) { 
        r->send(500, "text/plain", "NVS"); 
        return; 
    }
    
    if (r->method() == HTTP_GET) {
        // Возврат текущей конфигурации
        StaticJsonDocument<512> d;
        s.exportConfig(d); 
        s.end();
        String js; 
        serializeJson(d, js);
        r->send(200, "application/json", js);
    } else {
        uint32_t new_hot_id = 0;
        uint32_t new_cold_id = 0;

        // 2. Сохранение базовых настроек
        if (r->hasParam("ssid", true))
            s.saveWiFi(r->getParam("ssid", true)->value().c_str(), 
                       r->hasParam("pass", true) ? r->getParam("pass", true)->value().c_str() : "");
        
        if (r->hasParam("eirc_login", true))
            s.saveEircCredentials(r->getParam("eirc_login", true)->value().c_str(), 
                                  r->getParam("eirc_pass", true)->value().c_str());
        
        // 3. Если пришли ID счетчиков, считываем их в объявленные переменные и сохраняем
        if (r->hasParam("hot_id", true) && r->hasParam("cold_id", true)) {
            new_hot_id = r->getParam("hot_id", true)->value().toInt();
            new_cold_id = r->getParam("cold_id", true)->value().toInt();
            s.saveMeterIds(new_hot_id, new_cold_id);
        }
        s.end();

        // 4. КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Обновляем объект в оперативной памяти "на лету"
        if (eirc && (new_hot_id > 0 || new_cold_id > 0)) {
            eirc->setMeterIds(new_hot_id, new_cold_id);
            ESP_LOGI(LOG_TAG, "✅ EIRC meter IDs UPDATED IN MEMORY: Hot=%u, Cold=%u", new_hot_id, new_cold_id);
        }

        r->send(200, "text/plain", "OK");
    }
}

void WebInterface::_handleStatus(AsyncWebServerRequest* r) {
    StaticJsonDocument<512> d;
    d["wifi_ssid"] = WiFi.SSID();
    d["wifi_rssi"] = WiFi.RSSI();
    d["wifi_ip"] = WiFi.localIP().toString();
    d["wifi_mode"] = (WiFi.getMode() == WIFI_MODE_STA) ? "STA" : "AP"; // Исправлено: WIFI_MODE_STA
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        d["time"] = (uint32_t)mktime(&timeinfo);
        d["uptime"] = millis() / 1000;
    }
    if (eirc) {
        d["eirc_status"] = eirc->status;
        d["eirc_window_from"] = eirc->global_date_from;
        d["eirc_window_to"] = eirc->global_date_to;
        d["eirc_attorney_months"] = eirc->attorney_month;
    }
    d["heap_free"] = ESP.getFreeHeap();
    d["heap_min"] = ESP.getMinFreeHeap();
    String js; serializeJson(d, js);
    r->send(200, "application/json", js);
}



void WebInterface::_handleExport(AsyncWebServerRequest* r) {
    Storage s;
    if (!s.begin(NVS_NAMESPACE, true)) { 
        r->send(500, "text/plain", "NVS"); 
        return; 
    }
    
    StaticJsonDocument<1024> d;
    s.exportConfig(d); 
    s.end();
    
    String js; 
    serializeJsonPretty(d, js);
    
    // Корректный способ формирования ответа с пользовательскими заголовками
    AsyncWebServerResponse* response = r->beginResponse(200, "application/json", js);
    response->addHeader("Content-Disposition", "attachment; filename=\"wc_server_config.json\"");
    r->send(response);
}

void WebInterface::_handleImport(AsyncWebServerRequest* r) {
    if (!r->hasParam("config", true)) { r->send(400, "text/plain", "Missing"); return; }
    String j = r->getParam("config", true)->value();
    StaticJsonDocument<1024> d;
    if (deserializeJson(d, j)) { r->send(400, "text/plain", "JSON err"); return; }
    Storage s;
    if (!s.begin(NVS_NAMESPACE)) { r->send(500, "text/plain", "NVS"); return; }
    if (s.importConfig(d)) {
        s.end();
        r->send(200, "text/plain", "OK_RESTART");
        delay(1000);
        ESP.restart();
    } else {
        s.end();
        r->send(500, "text/plain", "Import fail");
    }
}

void fetchEircTask(void* parameter) {
    g_is_eirc_fetching = true;
    ESP_LOGI(LOG_TAG, "EIRC fetch task started");
    
    if (eirc && eirc->fetch()) {
        ESP_LOGI(LOG_TAG, "EIRC fetch SUCCESS. Hot ID: %u, Cold ID: %u", 
                 eirc->getHotId(), eirc->getColdId());
        ESP_LOGI(LOG_TAG, "Window: %u to %u, Attorney: %u", 
                 eirc->global_date_from, eirc->global_date_to, eirc->attorney_month);
        
        // Синхронизация локальной даты с сервером
        uint32_t server_hot_date = eirc->getHotInfo().last_sent_period;
        uint32_t server_cold_date = eirc->getColdInfo().last_sent_period;
        uint32_t max_server_date = (server_hot_date > server_cold_date) ? server_hot_date : server_cold_date;
        
        if (max_server_date > 0) {
            if (max_server_date < g_last_sent_date) {
                ESP_LOGW(LOG_TAG, "⚠️ Fixing future date bug: Local=%u, Server=%u", g_last_sent_date, max_server_date);
            }
            if (max_server_date > g_last_sent_date || g_values_changed) {
                g_last_sent_date = max_server_date;
                Storage s;
                if (s.begin(NVS_NAMESPACE)) {
                    s.saveReadings(g_hot_value, g_cold_value, g_last_sent_date);
                    s.end();
                }
                ESP_LOGI(LOG_TAG, "🔄 Synced last_sent_date to server value: %u", g_last_sent_date);
            }
        }
        
        g_initial_fetch_done = true;
        ESP_LOGI(LOG_TAG, "✅ Base data initialized. Submissions are now allowed.");

    } else {
        ESP_LOGE(LOG_TAG, "EIRC fetch FAILED. Will retry on next loop.");
        // Если первичный запрос провалился, оставляем флаг false, чтобы попробовать снова
    }
    
    g_is_eirc_fetching = false;
    vTaskDelete(NULL);
}

void WebInterface::_handleForceFetch(AsyncWebServerRequest* r) {
    if (!eirc) { 
        r->send(500, "text/plain", "EIRC not initialized"); 
        return; 
    }
    
    ESP_LOGI(LOG_TAG, "Starting EIRC fetch in background task...");
    
    // Создаем отдельную задачу для выполнения запроса
    xTaskCreate(
        fetchEircTask,      // Функция задачи
        "eirc_fetch",       // Имя задачи
        8192,               // Размер стека (8KB)
        NULL,               // Параметры
        1,                  // Приоритет (низкий)
        NULL                // Handle задачи (не нужен)
    );
    
    // Немедленно возвращаем ответ браузеру
    r->send(200, "text/plain", "FETCHING - Check Serial Monitor for results");
}
