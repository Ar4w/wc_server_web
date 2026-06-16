#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
struct MeterInfo { 
    uint32_t id=0; 
    String name, type, attorney; 
    uint32_t value=0, date_from=0, date_to=32;
    uint32_t last_sent_period=0; 
};
class Mosobleirc {
    String _user, _pass, _token; time_t _lastLogin=0; MeterInfo _hot, _cold;
    bool _parseAttorney(const char* s, uint32_t& m); int _checkStatus(int c); bool _fetchMeterData(uint32_t id);
public:
    uint32_t attorney_month=0xFFFFFFFF, global_date_from=0, global_date_to=32; int status=0;
    Mosobleirc() {}
    void setCredentials(const char* l, const char* p) { _user=l; _pass=p; }
    void setMeterIds(uint32_t h, uint32_t c) { _hot.id=h; _cold.id=c; }
    bool authorize(); bool fetch(); int submit(uint32_t id, uint32_t val);
    const MeterInfo& getHotInfo() const { return _hot; } const MeterInfo& getColdInfo() const { return _cold; }
    uint32_t getHotId() const { return _hot.id; } uint32_t getColdId() const { return _cold.id; }
    static uint32_t convertToInternalDate(int y, int m) { return (y-1900)*100+m; }
    static void convertFromInternalDate(uint32_t in, int& y, int& m) { y=1900+(in/100); m=in%100; }
};