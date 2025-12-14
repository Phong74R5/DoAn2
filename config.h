#ifndef CONFIG_H
#define CONFIG_H

#include <bcm2835.h>

// --- CẤU HÌNH PIN ---
#define PIN_DC          RPI_V2_GPIO_P1_22 // GPIO 25
#define PIN_RST         RPI_V2_GPIO_P1_18 // GPIO 24
#define PIN_LED         RPI_V2_GPIO_P1_16 // GPIO 23

// === SỬA LẠI PHẦN NÀY ===
// Nút Đăng ký (Chân vật lý 15 - GPIO 22)
#define PIN_BTN_REG     RPI_V2_GPIO_P1_15 

// Nút Ngủ (Chân vật lý 12 - GPIO 18)
#define PIN_BTN_SLEEP   RPI_V2_GPIO_P1_12
// =========================

// --- CẤU HÌNH MÀN HÌNH ---
#define LCD_WIDTH  320
#define LCD_HEIGHT 240

// --- BIẾN TOÀN CỤC ---
#include <atomic>
extern std::atomic<bool> g_running;
extern std::atomic<bool> g_is_sleeping;

// --- CẤU HÌNH QUEUE ---
#define QUEUE_SIZE 2

#endif // CONFIG_H