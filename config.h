#ifndef CONFIG_H
#define CONFIG_H

#include <bcm2835.h>
#include <atomic>
#include <cstdint>
#define PIN_DC          RPI_V2_GPIO_P1_18 
#define PIN_RST         RPI_V2_GPIO_P1_22 
#define PIN_LED         RPI_V2_GPIO_P1_16 

#define PIN_BTN_REG     RPI_V2_GPIO_P1_15 
#define PIN_BTN_SLEEP   RPI_V2_GPIO_P1_12 

#define LCD_WIDTH  320
#define LCD_HEIGHT 240

extern std::atomic<bool> g_running;
extern std::atomic<bool> g_register_mode;
extern std::atomic<bool> g_is_sleeping;

#define QUEUE_SIZE 2

// --- BIẾN GIAO TIẾP ---
// 0: Không có gì
// 1: Nhấn nhả (Short Press) -> Dùng để Next/Down
// 2: Nhấn giữ (Long Press)  -> Dùng để Select/Enter
extern std::atomic<int> g_btn_cmd; 

// Cờ báo đang ở chế độ Wifi (để task nút biết mà xử lý kiểu khác)
extern std::atomic<bool> g_wifi_mode_active;

#endif
