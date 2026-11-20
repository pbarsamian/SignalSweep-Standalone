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

#define FW_VERSION "v3.21-c6c"

DetectionRegistry g_reg;
SemaphoreHandle_t g_reg_mutex;
QueueHandle_t     g_queue;
Settings          g_cfg;

// UI task (priority 2: above the matching worker so the screen stays responsive, below the radio control task)
static void ui_task(void*) {
    ui_init(g_cfg);
    static DetectionRegistry snap;   // ~12 KB: keep it off the task stack
    for (;;) {
        // touch every ~5 ms; the registry copy and the drawing every ~20 ms
        for (int k = 0; k < 4; k++) { ui_touch(snap, g_cfg); vTaskDelay(pdMS_TO_TICKS(5)); }
        if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            snap = g_reg;
            xSemaphoreGive(g_reg_mutex);
        }
        ui_update(snap, g_cfg);

        int idx;
        while (xQueueReceive(g_queue, &idx, 0) == pdTRUE) {
            if (xSemaphoreTake(g_reg_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (idx >= 0 && idx < g_reg.count) ui_alert(g_reg.items[idx], g_cfg);
                xSemaphoreGive(g_reg_mutex);
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("[SignalSweep C6 " FW_VERSION "] boot (scanners + UI)");
    settings_load(g_cfg);
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
        if (c == 'i') { scan_c6_print_info(); stats_print(); }
        else if (c >= '1' && c <= '3') scan_c6_inject_demo(c);   // demo detections for looking at the UI
    }
    delay(50);
}
