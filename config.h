#pragma once
#include <Arduino.h>

#define HOT_PIN 23
#define COL_PIN 18
#define LED_PIN 2
#define PULL_TIMER_INTERVAL_US 100000
#define DEBOUNCE_THRESHOLD 3
#define MONTH_LIMIT_M3 30
#define NVS_NAMESPACE "wc_config"
#define WEB_SERVER_PORT 80
#define CAPTIVE_PORTAL_SSID "WC_Server_Config"
#define CAPTIVE_PORTAL_PASS ""
#define LOG_LEVEL ESP_LOG_INFO
#define LOG_TAG "WC_SERVER"

class AsyncWebServer; 
class DNSServer; 
class Mosobleirc;
class CounterManager; 

extern AsyncWebServer* webServer; 
extern DNSServer* dnsServer;
extern Mosobleirc* eirc;
extern CounterManager* counters;
extern uint32_t g_hot_value;
extern uint32_t g_cold_value; 
extern uint32_t g_last_sent_date;
extern bool g_values_changed; 
extern bool g_configured;
extern bool g_is_eirc_fetching;
extern bool g_eirc_dry_run; // Флаг тестового режима (true = только лог, без реальной отправки)
extern bool g_initial_fetch_done;
