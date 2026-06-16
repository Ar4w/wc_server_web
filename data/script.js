// Глобальные переменные
let readingsCache = null;
let configCache = null;

// Утилиты
const $ = (id) => document.getElementById(id);

function formatDate(dateStr) {
    if (!dateStr) return '—';
    const year = 1900 + Math.floor(dateStr / 100);
    const month = dateStr % 100;
    return `${month.toString().padStart(2, '0')}.${year}`;
}

function formatMonths(months) {
    if (months === 0xFFFFFFFF) return 'не определено';
    const now = new Date();
    const currentMonths = (now.getFullYear() - 1900) * 12 + now.getMonth();
    const diff = months - currentMonths;
    if (diff < 0) return `⚠️ просрочена (${Math.abs(diff)} мес.)`;
    if (diff < 4) return `⚠️ ${diff} мес.`;
    return `${diff} мес.`;
}

// API вызовы
async function apiGet(endpoint) {
    try {
        const resp = await fetch(endpoint);
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        return await resp.json();
    } catch (e) {
        console.error('API error:', e);
        log(`❌ Ошибка: ${e.message}`);
        return null;
    }
}

async function apiPost(endpoint, params = {}) {
    try {
        const formData = new URLSearchParams();
        for (const [k, v] of Object.entries(params)) {
            formData.append(k, v);
        }
        const resp = await fetch(endpoint, {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: formData
        });
        const text = await resp.text();
        return { ok: resp.ok, status: resp.status, text };
    } catch (e) {
        console.error('API error:', e);
        log(`❌ Ошибка: ${e.message}`);
        return { ok: false, text: e.message };
    }
}

// Логирование в интерфейс
function log(msg) {
    const el = $('system-log');
    if (el) {
        const time = new Date().toLocaleTimeString();
        el.textContent = `[${time}] ${msg}\n` + el.textContent;
        // Ограничение длины
        if (el.textContent.length > 2000) {
            el.textContent = el.textContent.slice(0, 2000);
        }
    }
}

// Загрузка показаний
async function fetchReadings() {
    log('📡 Загрузка показаний...');
    const data = await apiGet('/api/readings');
    if (data) {
        $('hot-value').textContent = data.hot;
        $('cold-value').textContent = data.cold;
        $('last-sent').textContent = formatDate(data.last_sent);
        
        readingsCache = data;
        updatePushButton();
        log(`✅ Показания: горячая=${data.hot}, холодная=${data.cold}`);
    }
}

// Обновление статуса кнопки отправки
function updatePushButton() {
    const btn = $('push-btn');
    if (!btn || !readingsCache) return;
    
    // Кнопка активна, если есть изменения и не отправлено за этот период
    btn.disabled = !readingsCache.changed;
    btn.title = readingsCache.changed ? 
        'Готово к отправке' : 'Нет изменений с последней отправки';
}

// Отправка в ЕИРЦ
async function pushToEirc() {
    if (!confirm('Отправить текущие показания в ЕИРЦ?')) return;
    
    log('📤 Отправка в ЕИРЦ...');
    const result = await apiPost('/api/push');
    
    if (result.ok) {
        if (result.text === 'OK_SENT') {
            log('✅ Показания успешно отправлены');
            alert('✅ Отправлено!');
            fetchReadings();
            fetchStatus();
        } else if (result.text === 'LIMIT_EXCEEDED') {
            log('⚠️ Превышен месячный лимит');
            alert('⚠️ Превышен лимит потребления. Подайте показания вручную.');
        } else {
            log(`⚠️ Ответ: ${result.text}`);
        }
    } else {
        log(`❌ Ошибка отправки: ${result.text}`);
        alert(`❌ Ошибка: ${result.text}`);
    }
}

// Установка значений
$('set-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    const hot = $('hot-input').value;
    const cold = $('cold-input').value;
    
    if (!confirm(`Установить значения:\nГорячая: ${hot}\nХолодная: ${cold}?`)) return;
    
    log(`⚙️ Установка: hot=${hot}, cold=${cold}`);
    const result = await apiPost('/api/set', { hot, cold });
    
    if (result.ok) {
        log('✅ Значения сохранены');
        $('hot-input').value = '';
        $('cold-input').value = '';
        fetchReadings();
    } else {
        log(`❌ Ошибка: ${result.text}`);
    }
});

// Загрузка данных ЕИРЦ
async function fetchEircData() {
    log('🔄 Обновление данных ЕИРЦ...');
    // Данные обновляются автоматически, но можно форсировать
    const status = await apiGet('/api/status');
    if (status) {
        updateEircInfo(status);
        log('✅ Данные ЕИРЦ обновлены');
    }
}

function updateEircInfo(status) {
    const from = status.eirc_window_from || 1;
    const to = status.eirc_window_to || 31;
    $('eirc-window').textContent = `${from}–${to} число месяца`;
    $('eirc-attorney').textContent = formatMonths(status.eirc_attorney_months);
    $('eirc-status').textContent = 
        status.eirc_status === 1 ? '☁️ ЕИРЦ: ✓' : '☁️ ЕИРЦ: ✗';
}

// Загрузка конфигурации
async function fetchConfig() {
    const data = await apiGet('/api/config');
    if (data) {
        configCache = data;
        // Заполнение форм
        if ($('wifi-ssid')) $('wifi-ssid').value = data.ssid || '';
        if ($('eirc-login')) $('eirc-login').value = data.eirc_login || '';
        if ($('hot-id')) $('hot-id').value = data.hot_id || '';
        if ($('cold-id')) $('cold-id').value = data.cold_id || '';
    }
}

// Сохранение WiFi
$('wifi-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    if (!confirm('Сохранить настройки WiFi и перезагрузить устройство?')) return;
    
    const ssid = $('wifi-ssid').value;
    const pass = $('wifi-pass').value;
    
    log(`📡 Сохранение WiFi: ${ssid}`);
    const result = await apiPost('/api/config', { ssid, pass });
    
    if (result.ok && result.text === 'OK') {
        log('✅ Настройки сохранены, перезагрузка...');
        setTimeout(() => {
            alert('Настройки сохранены. Устройство перезагружается.');
            location.reload();
        }, 2000);
    } else {
        log(`❌ Ошибка: ${result.text}`);
    }
});

// Сохранение ЕИРЦ
$('eirc-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    const params = {
        eirc_login: $('eirc-login').value,
        eirc_pass: $('eirc-pass').value,
        hot_id: $('hot-id').value,
        cold_id: $('cold-id').value
    };
    
    log('🏢 Сохранение настроек ЕИРЦ...');
    const result = await apiPost('/api/config', params);
    
    if (result.ok) {
        log('✅ Настройки ЕИРЦ сохранены');
        alert('✅ Сохранено!');
        fetchConfig();
    } else {
        log(`❌ Ошибка: ${result.text}`);
    }
});

// Экспорт конфигурации
async function exportConfig() {
    log('📥 Экспорт конфигурации...');
    const resp = await fetch('/api/export');
    if (resp.ok) {
        const blob = await resp.blob();
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'wc_server_config.json';
        a.click();
        URL.revokeObjectURL(url);
        log('✅ Конфигурация экспортирована');
    } else {
        log('❌ Ошибка экспорта');
    }
}

// Импорт конфигурации
async function importConfig(input) {
    const file = input.files[0];
    if (!file) return;
    
    if (!confirm('Импортировать конфигурацию из файла? Устройство будет перезагружено.')) {
        input.value = '';
        return;
    }
    
    log(`📤 Импорт: ${file.name}`);
    const formData = new FormData();
    formData.append('config', file);
    
    const resp = await fetch('/api/import', {
        method: 'POST',
        body: formData
    });
    const text = await resp.text();
    
    if (resp.ok && text === 'OK_RESTART') {
        log('✅ Конфигурация импортирована, перезагрузка...');
        setTimeout(() => location.reload(), 3000);
    } else {
        log(`❌ Ошибка импорта: ${text}`);
    }
    input.value = '';
}

// Сброс конфигурации
async function resetConfig() {
    if (!confirm('⚠️ Вы уверены? Все настройки будут удалены!')) return;
    
    log('⚠️ Сброс конфигурации...');
    const result = await apiPost('/reset');
    
    if (result.ok) {
        log('✅ Сброс выполнен, перезагрузка...');
        setTimeout(() => {
            alert('Настройки сброшены. Устройство перезагружается в режим настройки.');
            location.reload();
        }, 2000);
    }
}

// Перезагрузка устройства
async function restartDevice() {
    if (!confirm('Перезагрузить устройство?')) return;
    
    log('🔄 Перезагрузка...');
    const result = await apiPost('/restart');
    
    if (result.ok) {
        setTimeout(() => location.reload(), 2000);
    }
}

// Загрузка статуса системы
async function fetchStatus() {
    const status = await apiGet('/api/status');
    if (status) {
        // WiFi статус
        $('wifi-status').textContent = 
            `🔌 ${status.wifi_ssid || '—'} (${status.wifi_rssi || 0} dBm)`;
        
        // Время
        if (status.time) {
            const date = new Date(status.time * 1000);
            $('time-status').textContent = `🕐 ${date.toLocaleString()}`;
        }
        
        // ЕИРЦ
        updateEircInfo(status);
        
        // Системная информация
        const logLines = [
            `Heap: ${status.heap_free} / ${status.heap_min} bytes`,
            `Uptime: ${Math.floor(status.uptime / 60)} мин`,
            `Mode: ${status.wifi_mode}`
        ];
        $('system-log').textContent = logLines.join('\n') + '\n' + $('system-log').textContent;
    }
}

// Авто-обновление
function startAutoRefresh() {
    fetchReadings();
    fetchStatus();
    fetchConfig();
    
    setInterval(fetchReadings, 30000);   // Каждые 30 сек
    setInterval(fetchStatus, 60000);     // Каждую минуту
}

// Инициализация
document.addEventListener('DOMContentLoaded', () => {
    log('🚀 Интерфейс загружен');
    startAutoRefresh();
    
    // Обработчик изменения полей для кнопки отправки
    $('hot-input')?.addEventListener('input', updatePushButton);
    $('cold-input')?.addEventListener('input', updatePushButton);
});