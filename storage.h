#pragma once
#include <Preferences.h>
#include <ArduinoJson.h>

class Storage {
    Preferences _prefs; bool _initialized = false;
public:
    bool begin(const char* ns, bool ro = false) { 
        if(_initialized) _prefs.end(); 
        _initialized=_prefs.begin(ns,ro); 
        return _initialized; 
    }
    void end() { if(_initialized){_prefs.end(); _initialized=false;} }
    bool saveWiFi(const char* s, const char* p) { if(!_initialized)return false; _prefs.putString("ssid",s); _prefs.putString("pass",p); return true; }
    bool loadWiFi(char* sb, size_t sl, char* pb, size_t pl) { if(!_initialized)return false; String s=_prefs.getString("ssid",""), p=_prefs.getString("pass",""); if(s.length()==0)return false; s.toCharArray(sb,sl); p.toCharArray(pb,pl); return true; }
    bool saveEircCredentials(const char* l, const char* p) { if(!_initialized)return false; _prefs.putString("eirc_login",l); _prefs.putString("eirc_pass",p); return true; }
    bool loadEircCredentials(char* lb, size_t ll, char* pb, size_t pl) { if(!_initialized)return false; String l=_prefs.getString("eirc_login",""), p=_prefs.getString("eirc_pass",""); if(l.length()==0)return false; l.toCharArray(lb,ll); p.toCharArray(pb,pl); return true; }
    bool saveMeterIds(uint32_t h, uint32_t c) { if(!_initialized)return false; _prefs.putULong("hot_id",h); _prefs.putULong("cold_id",c); return true; }
    bool loadMeterIds(uint32_t& h, uint32_t& c) { if(!_initialized)return false; h=_prefs.getULong("hot_id",0); c=_prefs.getULong("cold_id",0); return(h!=0&&c!=0); }
    bool saveReadings(uint32_t h, uint32_t c, uint32_t ls) { if(!_initialized)return false; _prefs.putULong("hot_val",h); _prefs.putULong("cold_val",c); _prefs.putULong("last_sent",ls); return true; }
    bool loadReadings(uint32_t& h, uint32_t& c, uint32_t& ls) { if(!_initialized)return false; h=_prefs.getULong("hot_val",0); c=_prefs.getULong("cold_val",0); ls=_prefs.getULong("last_sent",12310); return true; }
    bool saveWebAuth(const char* u, const char* h) { if(!_initialized)return false; _prefs.putString("web_user",u); _prefs.putString("web_pass_hash",h); return true; }
    bool loadWebAuth(char* ub, size_t ul, char* hb, size_t hl) { if(!_initialized)return false; String u=_prefs.getString("web_user","admin"), h=_prefs.getString("web_pass_hash","5B1A"); u.toCharArray(ub,ul); h.toCharArray(hb,hl); return true; }
    bool isConfigured() { return _initialized && _prefs.getBool("configured", false); }
    void setConfigured(bool v) { if(_initialized) _prefs.putBool("configured", v); }
    void clearAll() { if(_initialized) _prefs.clear(); }
    bool exportConfig(JsonDocument& doc) { if(!_initialized)return false; doc["ssid"]=_prefs.getString("ssid",""); doc["eirc_login"]=_prefs.getString("eirc_login",""); doc["hot_id"]=_prefs.getULong("hot_id",0); doc["cold_id"]=_prefs.getULong("cold_id",0); doc["hot_val"]=_prefs.getULong("hot_val",0); doc["cold_val"]=_prefs.getULong("cold_val",0); doc["last_sent"]=_prefs.getULong("last_sent",0); doc["configured"]=_prefs.getBool("configured",false); return true; }
    bool importConfig(const JsonDocument& doc) { if(!_initialized)return false; if(doc.containsKey("ssid"))_prefs.putString("ssid",doc["ssid"].as<const char*>()); if(doc.containsKey("eirc_login"))_prefs.putString("eirc_login",doc["eirc_login"].as<const char*>()); if(doc.containsKey("hot_id"))_prefs.putULong("hot_id",doc["hot_id"].as<uint32_t>()); if(doc.containsKey("cold_id"))_prefs.putULong("cold_id",doc["cold_id"].as<uint32_t>()); if(doc.containsKey("hot_val"))_prefs.putULong("hot_val",doc["hot_val"].as<uint32_t>()); if(doc.containsKey("cold_val"))_prefs.putULong("cold_val",doc["cold_val"].as<uint32_t>()); if(doc.containsKey("last_sent"))_prefs.putULong("last_sent",doc["last_sent"].as<uint32_t>()); if(doc.containsKey("configured"))_prefs.putBool("configured",doc["configured"].as<bool>()); return true; }
};
inline uint32_t simpleHash(const char* str) { uint32_t h=5381; int c; while((c=*str++)) h=((h<<5)+h)+c; return h; }