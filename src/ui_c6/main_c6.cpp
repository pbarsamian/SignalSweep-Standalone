// SPDX-License-Identifier: GPL-3.0-or-later
// SignalSweep C6 — Phase 4 firmware: scanners + touch UI on the 1.47" display. Serial: 'i' prints heap/stack/queue info.
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_event.h>

#include "hal/board.h"
#include "detection.h"
#include "settings.h"
#include "focus.h"
#include "signatures.h"
#include "ui.h"
#include "scan_c6/scan_c6.h"
#include "hal/power.h"
#include "sd_log/sd_store.h"

#define FW_VERSION "v3.21-c6d"

DetectionRegistry g_reg;
SemaphoreHandle_t g_reg_mutex;
QueueHandle_t     g_queue;
Settings          g_cfg;
static volatile uint32_t s_touch_max_us = 0, s_upd_max_us = 0, s_upd_sum_us = 0, s_upd_n = 0;   // UI loop timing, printed by 'i'
static volatile bool s_sd_dump_req = false;   // serial 'a': SD I/O must happen in the UI task (shared SPI bus)

// UI task (priority 2: above the matching worker so the screen stays responsive, below the radio control task)
static void ui_task(void*) {
    ui_init(g_cfg);
    static DetectionRegistry snap;   // ~12 KB: keep it off the task stack
    for (;;) {
        // touch every ~5 ms; the registry copy and the drawing every ~20 ms
        for (int k = 0; k < 4; k++) {
            uint32_t t0 = micros();
            ui_touch(snap, g_cfg);
            uint32_t d = micros() - t0; if (d > s_touch_max_us) s_touch_max_us = d;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            snap = g_reg;
            xSemaphoreGive(g_reg_mutex);
        }
        uint32_t t1 = micros();
        ui_update(snap, g_cfg);
        uint32_t du = micros() - t1; s_upd_sum_us += du; s_upd_n++; if (du > s_upd_max_us) s_upd_max_us = du;
        if (s_sd_dump_req) { s_sd_dump_req = false; sd_dump_alerts(); }

        int idx;
        while (xQueueReceive(g_queue, &idx, 0) == pdTRUE) {
            // Copy the entry out and release the registry before alerting: ui_alert may write the SD card (tens of ms)
            // and must not hold g_reg_mutex meanwhile, or the matching worker's post_detection() would time out.
            static Detection d;
            bool have = false;
            if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (idx >= 0 && idx < g_reg.count) { d = g_reg.items[idx]; have = true; }
                xSemaphoreGive(g_reg_mutex);
            }
            if (have) ui_alert(d, g_cfg);
        }
    }
}

void setup() {
    Serial.begin(115200);
    // Never block a task on serial output: with no host reading the USB port the TX buffer fills after a few log lines,
    // and every later printf would stall the calling task (the UI task logs each gesture) for the write timeout.
    Serial.setTxTimeoutMs(0);
    delay(1500);
    Serial.println("[SignalSweep C6 " FW_VERSION "] boot (scanners + UI)");
    settings_load(g_cfg);
    Serial.printf("[boot] settings: drone=%d tracker=%d camera=%d bodycam=%d attack=%d flash=%d buzzer=%d brightness=%d\n",
        g_cfg.detect_drone, g_cfg.detect_tracker, g_cfg.detect_camera, g_cfg.detect_bodycam, g_cfg.detect_attack,
        g_cfg.led_en, g_cfg.speaker_en, g_cfg.brightness);
    focus_init();
    g_reg_mutex = xSemaphoreCreateMutex();
    g_queue     = xQueueCreate(16, sizeof(int));
    esp_err_t e = esp_event_loop_create_default();   // see docs/port/TASKS.md
    Serial.printf("[boot] default event loop: %s\n", e == ESP_OK ? "created" : esp_err_to_name(e));
    xTaskCreate(ui_task, "ui", 8192, nullptr, 2, nullptr);
    scan_c6_start(g_cfg);
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == 'i') {
            scan_c6_print_info(); stats_print();
            uint32_t n = s_upd_n ? s_upd_n : 1;
            Serial.printf("[ui-time] ui_update avg=%lu us max=%lu us (n=%lu) | ui_touch max=%lu us\n", (unsigned long)(s_upd_sum_us / n),
                          (unsigned long)s_upd_max_us, (unsigned long)s_upd_n, (unsigned long)s_touch_max_us);
            s_upd_sum_us = s_upd_n = s_upd_max_us = s_touch_max_us = 0;
        }
        else if (c == 'a') s_sd_dump_req = true;
        else if (c == 'v') Serial.printf("[bat] %d mV (VBAT = 3 x VADC, -1 = not available)\n", power_battery_mv());
        else if (c >= '1' && c <= '3') scan_c6_inject_demo(c);   // demo detections for looking at the UI
    }
    delay(50);
}
