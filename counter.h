#pragma once
#include <Arduino.h>

struct Counter {
    uint8_t pin;
    uint32_t value = 0;
    uint32_t last_triggered = 0;
    bool pending_save = false;
    uint32_t max_low = 0, max_high = 0, cont_low = 0, cont_high = 0;
    bool state = false;
};

class CounterManager {
    Counter _hot, _cold;
    uint32_t _lastPoll = 0;
    void _processCounter(Counter& c, int raw);
public:
    CounterManager(uint8_t hp, uint8_t cp) { _hot.pin = hp; _cold.pin = cp; }
    void begin();
    void loop(); // Программный опрос вместо аппаратного таймера
    uint32_t getHotValue() const { return _hot.value; }
    uint32_t getColdValue() const { return _cold.value; }
    void setHotValue(uint32_t v) { _hot.value = v; _hot.pending_save = true; }
    void setColdValue(uint32_t v) { _cold.value = v; _cold.pending_save = true; }
    bool hasHotChanged() const { return _hot.pending_save; }
    bool hasColdChanged() const { return _cold.pending_save; }
    void acknowledgeHot() { _hot.pending_save = false; }
    void acknowledgeCold() { _cold.pending_save = false; }
};