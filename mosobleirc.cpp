#include "mosobleirc.h"
#include "config.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


int Mosobleirc::_checkStatus(int c) { if(c==200){status=1;return 0;} ESP_LOGW(LOG_TAG,"HTTP err:%d",c); status=0; return 1; }
bool Mosobleirc::authorize() {
    time_t now=time(nullptr); if(_lastLogin>0 && (now-_lastLogin)<60 && _token.length()>0) return true;
    WiFiClientSecure c; c.setInsecure(); HTTPClient h; if(!h.begin(c,"https://lkk.mosobleirc.ru/api/tenants-registration/v2/login")) return false;
    h.addHeader("Content-Type","application/json");
    StaticJsonDocument<256> req; req["phone"]=_user; req["password"]=_pass; req["loginMethod"]="PERSONAL_OFFICE";
    String js; serializeJson(req,js); int sc=h.POST(js); String resp=h.getString(); h.end(); if(_checkStatus(sc)) return false;
    StaticJsonDocument<512> rp; deserializeJson(rp,resp); _token=rp["token"].as<String>(); _lastLogin=now; return true;
}

bool Mosobleirc::_fetchMeterData(uint32_t id) {
    WiFiClientSecure c; 
    c.setInsecure(); 
    HTTPClient h; 
    String u = "https://lkk.mosobleirc.ru/api/api/clients/meters/for-item/"; 
    u += id;
    
    if (!h.begin(c, u)) return false; 
    
    h.addHeader("X-Auth-Tenant-Token", _token); 
    h.addHeader("Content-Type", "application/json"); 
    
    int sc = h.GET(); 
    String r = h.getString(); 
    h.end(); 
    
    // Диагностика сырого JSON
    ESP_LOGI(LOG_TAG, "========================================");
    ESP_LOGI(LOG_TAG, "RAW JSON RESPONSE (First 1000 chars):");
    ESP_LOGI(LOG_TAG, "%s", r.substring(0, 1000).c_str());
    ESP_LOGI(LOG_TAG, "========================================");
    
    if (_checkStatus(sc)) return false; 
    
    delay(100); 
    
    // Фильтр для экономии памяти (добавлены year и month)
    StaticJsonDocument<1024> f; 
    f[0]["meter"]["id"] = true; 
    f[0]["meter"]["name"] = true; 
    f[0]["meter"]["type"] = true; 
    f[0]["meter"]["attorneyDeadline"] = true; 
    f[0]["meter"]["lastValue"]["total"]["value"] = true;
    f[0]["meter"]["lastValue"]["settlementPeriod"]["year"] = true; 
    f[0]["meter"]["lastValue"]["settlementPeriod"]["month"] = true; 
    f[0]["valueSendInfo"]["meterIndicationDate"]["from"] = true; 
    f[0]["valueSendInfo"]["meterIndicationDate"]["to"] = true; 
    f[0]["valueSendInfo"]["willBeCountForPeriod"]["year"] = true; 
    f[0]["valueSendInfo"]["willBeCountForPeriod"]["month"] = true; 
    
    DynamicJsonDocument doc(2048); 
    DeserializationError err = deserializeJson(doc, r, DeserializationOption::Filter(f)); 
    if (err) { 
        ESP_LOGE(LOG_TAG, "JSON parse error: %s", err.c_str()); 
        return false; 
    } 
    
    JsonArray meters = doc.as<JsonArray>(); 
    for (JsonVariant m : meters) { 
        uint32_t mid = m["meter"]["id"].as<uint32_t>(); 
        
        // Объявляем target ЗДЕСЬ, внутри цикла
        MeterInfo* target = nullptr; 
        if (mid == _hot.id) target = &_hot; 
        else if (mid == _cold.id) target = &_cold; 
        
        // Используем target ТОЛЬКО внутри этого блока
        if (target) { 
            target->name = m["meter"]["name"].as<String>(); 
            target->type = m["meter"]["type"].as<String>(); 
            target->attorney = m["meter"]["attorneyDeadline"].as<String>(); 
            target->value = m["meter"]["lastValue"]["total"]["value"].as<uint32_t>(); 
            target->date_from = m["valueSendInfo"]["meterIndicationDate"]["from"].as<uint32_t>(); 
            target->date_to = m["valueSendInfo"]["meterIndicationDate"]["to"].as<uint32_t>(); 
            
            // Парсинг периода последней передачи (год и месяц)
            int y = m["meter"]["lastValue"]["settlementPeriod"]["year"].as<int>();
            int mo = m["meter"]["lastValue"]["settlementPeriod"]["month"].as<int>();
            if (y > 2000 && mo > 0 && mo <= 12) {
                target->last_sent_period = Mosobleirc::convertToInternalDate(y, mo);
            }
            
            // Обновляем глобальное окно подачи
            if (target->date_from > global_date_from) global_date_from = target->date_from; 
            if (target->date_to < global_date_to) global_date_to = target->date_to; 
            
            _parseAttorney(target->attorney.c_str(), attorney_month); 
            
            ESP_LOGI(LOG_TAG, "Loaded meter %u: value=%u, type=%s, period=%u", 
                     mid, target->value, target->type.c_str(), target->last_sent_period); 
        } 
    } 
    
    return true; 
}


//bool Mosobleirc::fetch() { if(!authorize())return false; WiFiClientSecure c; c.setInsecure(); HTTPClient h; if(!h.begin(c,"https://lkk.mosobleirc.ru/api/api/clients/configuration-items"))return false; h.addHeader("X-Auth-Tenant-Token",_token); h.addHeader("Content-Type","application/json"); int sc=h.GET(); String r=h.getString(); h.end(); if(_checkStatus(sc))return false; StaticJsonDocument<1024> f; f["items"][0]["id"]=true; DynamicJsonDocument d(1024); deserializeJson(d,r,DeserializationOption::Filter(f)); for(JsonVariant i:d["items"].as<JsonArray>()) _fetchMeterData(i["id"].as<uint32_t>()); status=1; return true; }

int Mosobleirc::submit(uint32_t mid, uint32_t val) {
    uint32_t m3 = val / 100;
    MeterInfo* m = nullptr;
    
    if (mid == _hot.id) m = &_hot;
    else if (mid == _cold.id) m = &_cold;
    
    if (!m) return 0;

    // Проверка лимита
    int32_t diff = m3 - m->value;
    if (diff > MONTH_LIMIT_M3) {
        ESP_LOGW(LOG_TAG, "Limit exceeded for meter %u: diff=%d", mid, diff);
        return 2;
    }
    if (diff < 0) m3 = m->value;

    if (g_eirc_dry_run) {
        ESP_LOGW(LOG_TAG, "🛡️ [DRY RUN] Отправка ЗАБЛОКИРОВАНА. Meter ID: %u, Значение: %u м³ (исх: %u)", mid, m3, val);
        return 3; // Специальный код: "Успешно проверено, но не отправлено"
    }

    if (!authorize()) return 0;
    
    WiFiClientSecure c; 
    c.setInsecure(); 
    HTTPClient h; 
    String u = "https://lkk.mosobleirc.ru/api/api/clients/meters/"; 
    u += String(mid);
    u += "/values?withOptionalCheck=true";
    
    if (!h.begin(c, u)) return 0;
    
    h.addHeader("X-Auth-Tenant-Token", _token);
    h.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<128> p;
    p["value1"] = m3;
    
    String js;
    serializeJson(p, js);
    
    int sc = h.POST(js);
    h.end();
    
    return _checkStatus(sc) ? 0 : 1;
}

bool Mosobleirc::_parseAttorney(const char* s, uint32_t& out) { uint32_t p[3]={0}; int i=0; const char* ptr=s; char b[16]={0}; int bi=0; while(*ptr&&i<3){if(*ptr=='.'||*ptr=='/'||*ptr=='-'){if(bi>0){b[bi]='\0';p[i++]=atoi(b);bi=0;}ptr++;continue;}if(bi<15)b[bi++]=*ptr;ptr++;}if(bi>0&&i<3){b[bi]='\0';p[i]=atoi(b);} uint32_t y=0,mo=0; if(p[0]>2000)y=p[0]-1900;else if(p[2]>2000)y=p[2]-1900; mo=(p[0]>2000||p[2]>2000)?p[1]:p[0]; if(y>0&&mo>0&&mo<=12){uint32_t tm=y*12+(mo-1);if(tm<out)out=tm;return true;} return false; }

bool Mosobleirc::fetch() { 
    ESP_LOGI(LOG_TAG, "🔍 STARTING FETCH with Meter IDs: Hot=%u, Cold=%u", _hot.id, _cold.id);

    if (!authorize()) return false;
    

    delay(100);

    WiFiClientSecure c; 
    c.setInsecure(); 
    HTTPClient h; 
    
    if (!h.begin(c, "https://lkk.mosobleirc.ru/api/api/clients/configuration-items")) 
        return false; 
    
    h.addHeader("X-Auth-Tenant-Token", _token); 
    h.addHeader("Content-Type", "application/json"); 
    int sc = h.GET(); 
    String r = h.getString(); 
    h.end(); 
    
    if (_checkStatus(sc)) return false; 
    
    StaticJsonDocument<1024> f; 
    f["items"][0]["id"] = true; 
    DynamicJsonDocument d(1024); 
    deserializeJson(d, r, DeserializationOption::Filter(f)); 
    
    for (JsonVariant i : d["items"].as<JsonArray>()) {
        _fetchMeterData(i["id"].as<uint32_t>());
        delay(200);    
    }
    
    status = 1; 
    return true; 
}