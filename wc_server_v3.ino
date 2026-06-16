#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <time.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <LittleFS.h>

#include "config.h"
#include "storage.h"
#include "counter.h"
#include "mosobleirc.h"
#include "captive.h"
#include "webserver.h"

#include <nvs_flash.h>

// Глобальные объекты
AsyncWebServer* webServer = nullptr;
DNSServer* dnsServer = nullptr;
Mosobleirc* eirc = nullptr;
CounterManager* counters = nullptr;
Storage* storage = nullptr;
WebInterface* webUI = nullptr;
CaptivePortal* captive = nullptr;

WiFiMulti wifiMulti;

// Глобальные переменные
uint32_t g_hot_value = 0;
uint32_t g_cold_value = 0;
uint32_t g_last_sent_date = 12310;
bool g_values_changed = false;
bool g_configured = false;
bool g_is_eirc_fetching = false;
bool g_eirc_dry_run = false; // ⚠️ ПО УМОЛЧАНИЮ ВКЛЮЧЕНО: реальная отправка в ЕИРЦ ЗАБЛОКИРОВАНА
bool g_initial_fetch_done = false;

// NTP
const char* ntpServer1 = "ntp3.vniiftri.ru";
const char* ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = 10800;
const int daylightOffset_sec = 0;

// Таймеры
uint32_t lastEircFetch = 0;
uint32_t lastAutoSend = 0;
uint32_t lastDiag = 0;
const uint32_t EIRC_FETCH_INTERVAL = 86400000;
const uint32_t AUTO_SEND_CHECK_INTERVAL = 60000;

void timeSyncCallback(struct timeval* t) {
    ESP_LOGI(LOG_TAG, "Time synchronized via NTP");
}

void setupWiFiOptimizations() {
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(82); // 20.5 dBm
    WiFi.enableIPv6(false);
    WiFi.setHostname("wc-server-esp32");
}

bool connectToWiFi(const char* ssid, const char* pass, uint32_t timeout_ms) {
    setupWiFiOptimizations();
    ESP_LOGI(LOG_TAG, "Attempting connection to SSID: '%s'", ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);
    
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout_ms) {
            wl_status_t status = WiFi.status();
            ESP_LOGE(LOG_TAG, "Connection FAILED after %lu ms. Status code: %d", timeout_ms, status);
            switch(status) {
                case WL_NO_SSID_AVAIL: ESP_LOGE(LOG_TAG, "  -> SSID not found"); break;
                case WL_CONNECT_FAILED: ESP_LOGE(LOG_TAG, "  -> Wrong password"); break;
                case WL_CONNECTION_LOST: ESP_LOGE(LOG_TAG, "  -> Connection dropped"); break;
                case WL_IDLE_STATUS: ESP_LOGE(LOG_TAG, "  -> Still negotiating..."); break;
                default: ESP_LOGE(LOG_TAG, "  -> Unknown state"); break;
            }
            return false;
        }
        delay(200);
    }
    ESP_LOGI(LOG_TAG, "Connected! IP: %s | RSSI: %d dBm | Channel: %d", 
             WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
    return true;
}

void setup() {
    Serial.begin(115200);
    esp_log_level_set("*", LOG_LEVEL);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    ESP_LOGI(LOG_TAG, "=== WC Server v2.0 starting ===");

    if (!LittleFS.begin(true)) {
        ESP_LOGE(LOG_TAG, "Failed to mount LittleFS");
    } else {
        ESP_LOGI(LOG_TAG, "LittleFS mounted successfully");
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    storage = new Storage();
    if (storage->begin(NVS_NAMESPACE)) {
        g_configured = storage->isConfigured();
        storage->loadReadings(g_hot_value, g_cold_value, g_last_sent_date);
        storage->end();
        ESP_LOGI(LOG_TAG, "Config loaded: configured=%d, hot=%u, cold=%u", g_configured, g_hot_value, g_cold_value);
    }

    counters = new CounterManager(HOT_PIN, COL_PIN);
    counters->begin();

    counters->setHotValue(g_hot_value);
    counters->setColdValue(g_cold_value);
    ESP_LOGI(LOG_TAG, "Counters initialized from NVS: hot=%u, cold=%u", g_hot_value, g_cold_value);

    eirc = new Mosobleirc();
    if (g_configured) {
        Storage tmp;
        if (tmp.begin(NVS_NAMESPACE, true)) {
            char login[64], pass[64];
            uint32_t hot_id, cold_id;
            if (tmp.loadEircCredentials(login, sizeof(login), pass, sizeof(pass)) &&
                tmp.loadMeterIds(hot_id, cold_id)) {
                eirc->setCredentials(login, pass);
                eirc->setMeterIds(hot_id, cold_id);
                ESP_LOGI(LOG_TAG, "EIRC credentials loaded");
            }
            tmp.end();
        }
    }

    sntp_set_time_sync_notification_cb(timeSyncCallback);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);

    webServer = new AsyncWebServer(WEB_SERVER_PORT);
    dnsServer = new DNSServer();
    webUI = new WebInterface(webServer);
    captive = new CaptivePortal(webServer, dnsServer);

    if (!g_configured) {
        ESP_LOGI(LOG_TAG, "First run - starting captive portal");
        captive->begin(CAPTIVE_PORTAL_SSID, CAPTIVE_PORTAL_PASS);
    } else {
        Storage tmp;
  
        bool connected = false;
        if (tmp.begin(NVS_NAMESPACE, true)) {
            char ssid[32], pass[64];
            if (tmp.loadWiFi(ssid, sizeof(ssid), pass, sizeof(pass))) {
                connected = connectToWiFi(ssid, pass, 15000); 
            }
            tmp.end();
        } 
 
        if (connected) {
            webUI->begin();
            ESP_LOGI(LOG_TAG, "Web interface started");
        } else {
            ESP_LOGW(LOG_TAG, "Failed to connect to saved WiFi, starting captive portal");
            captive->begin(CAPTIVE_PORTAL_SSID, CAPTIVE_PORTAL_PASS);
        }
    }
    ESP_LOGI(LOG_TAG, "Setup complete");
}

void loop() {
    // 1. Обработка Captive Portal (если активен, остальной код не выполняем)
    if (captive && captive->isActive()) {
        captive->loop();
        return; 
    }

    // 2. Обработка счетчиков
    if (counters) {
        counters->loop();
        if (counters->hasHotChanged()) {
            g_hot_value = counters->getHotValue();
            g_values_changed = true;
            counters->acknowledgeHot();
            Storage tmp; if (tmp.begin(NVS_NAMESPACE)) { tmp.saveReadings(g_hot_value, g_cold_value, g_last_sent_date); tmp.end(); }
        }
        if (counters->hasColdChanged()) {
            g_cold_value = counters->getColdValue();
            g_values_changed = true;
            counters->acknowledgeCold();
            Storage tmp; if (tmp.begin(NVS_NAMESPACE)) { tmp.saveReadings(g_hot_value, g_cold_value, g_last_sent_date); tmp.end(); }
        }
    }

    // 3. Сетевые операции (только если подключены и настроены)
    if (eirc && WiFi.status() == WL_CONNECTED && g_configured) {
        uint32_t now = millis();
        struct tm timeinfo;
        bool timeIsSynced = getLocalTime(&timeinfo); // Явная проверка времени

        // А) Первичный запрос: как только время синхронизировано и фетч еще не делался
        if (!g_initial_fetch_done && timeIsSynced && !g_is_eirc_fetching) {
            ESP_LOGI(LOG_TAG, "🚀 Triggering INITIAL post-boot EIRC fetch...");
            g_is_eirc_fetching = true; // Блокируем сразу, чтобы не создать две задачи
            xTaskCreate(fetchEircTask, "init_fetch", 8192, NULL, 1, NULL);
        }

        // Б) Периодический запрос (раз в 24 часа), если первичный уже прошел
        if (g_initial_fetch_done && (now - lastEircFetch > EIRC_FETCH_INTERVAL) && !g_is_eirc_fetching) {
            ESP_LOGI(LOG_TAG, "🔄 Triggering DAILY EIRC fetch...");
            g_is_eirc_fetching = true;
            xTaskCreate(fetchEircTask, "daily_fetch", 8192, NULL, 1, NULL);
            lastEircFetch = now;
        }

        // В) Авто-отправка показаний (в 17:00, если попали в окно)
        // Безопасно, так как g_initial_fetch_done гарантирует, что m->value > 0
        if (g_initial_fetch_done && timeIsSynced && (now - lastAutoSend > AUTO_SEND_CHECK_INTERVAL) && !g_is_eirc_fetching) {
            uint32_t current_date = Mosobleirc::convertToInternalDate(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1);
            
            if (current_date > g_last_sent_date &&
                timeinfo.tm_mday >= eirc->global_date_from &&
                timeinfo.tm_mday <= eirc->global_date_to &&
                timeinfo.tm_hour == 17 && 
                g_values_changed) {
                
                ESP_LOGI(LOG_TAG, "⏰ Auto-send conditions met, sending...");
                
                int hot_res = eirc->submit(eirc->getHotId(), g_hot_value);
                int cold_res = eirc->submit(eirc->getColdId(), g_cold_value);
                
                if (hot_res == 1 && cold_res == 1) {
                    g_last_sent_date = current_date;
                    g_values_changed = false;
                    Storage tmp; 
                    if (tmp.begin(NVS_NAMESPACE)) { 
                        tmp.saveReadings(g_hot_value, g_cold_value, g_last_sent_date); 
                        tmp.end(); 
                    }
                    ESP_LOGI(LOG_TAG, "✅ Auto-send successful");
                } else if (hot_res == 3 && cold_res == 3) {
                    ESP_LOGI(LOG_TAG, "🛡️ [DRY RUN] Auto-send skipped (test mode active).");
                } else if (hot_res == 2 || cold_res == 2) {
                    ESP_LOGW(LOG_TAG, "⚠️ Auto-send skipped: LIMIT_EXCEEDED");
                } else {
                    ESP_LOGE(LOG_TAG, "❌ Auto-send failed: EIRC_ERR");
                }
            }
            lastAutoSend = now;
        }
    }

    // 4. Веб-интерфейс
    if (webUI) webUI->loop();

    // 5. Диагностика (каждые 10 секунд)
    if (millis() - lastDiag > 10000) {
        ESP_LOGI(LOG_TAG, "RSSI: %d dBm | Heap: %u/%u | Status: %s", 
                 WiFi.RSSI(), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                 WiFi.status() == WL_CONNECTED ? "OK" : "FAIL");
        lastDiag = millis();
    }

    // 6. Мигание светодиодом (индикатор жизни)
    static uint32_t lastBlink = 0;
    if (millis() - lastBlink > 1000) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        lastBlink = millis();
    }
    
    delay(10);
}