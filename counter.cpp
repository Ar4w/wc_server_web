#include "counter.h"
#include "config.h"

void CounterManager::begin() {
    pinMode(_hot.pin, INPUT_PULLUP);
    pinMode(_cold.pin, INPUT_PULLUP);
    _lastPoll = micros();
}

void CounterManager::_processCounter(Counter& c, int raw) {
    // Инвертируем логику: 0 = замкнуто (активно), 1 = разомкнуто
    int active = (raw == 0) ? 1 : 0;
    int inactive = (raw == 0) ? 0 : 1;
    
    // Подсчет длительности активного и неактивного состояния
    if (active == 1 && c.state == 1) {
        c.cont_low++;  // Считаем время в активном состоянии
        if (c.cont_low > c.max_low) c.max_low = c.cont_low;
    }
    
    if (inactive == 1 && c.state == 0) {
        c.cont_high++;  // Считаем время в неактивном состоянии
        if (c.cont_high > c.max_high) c.max_high = c.cont_high;
    }
    
    // Детектируем переход из активного в неактивное состояние (размыкание геркона)
    if (active == 0 && c.state == 1) {
        // Проверяем, что импульс был достаточно длинным
        if (c.max_low >= DEBOUNCE_THRESHOLD) {
            c.value++;
            c.pending_save = true;
            c.last_triggered = millis();
            ESP_LOGD(LOG_TAG, "Pulse detected on pin %d, new value: %u (active duration: %u)", 
                     c.pin, c.value, c.max_low);
        }
        // Сбрасываем счетчики для следующего импульса
        c.cont_low = 0;
        c.cont_high = 0;
        c.max_low = 0;
        c.max_high = 0;
    }
    
    c.state = active;
}

void CounterManager::loop() {
    uint32_t now = micros();
    if (now - _lastPoll >= PULL_TIMER_INTERVAL_US) {
        _lastPoll = now;
        _processCounter(_hot, digitalRead(_hot.pin));
        _processCounter(_cold, digitalRead(_cold.pin));
    }
}