#ifndef CONFIG_H
#define CONFIG_H

#include <bcm2835.h>

// --- CẤU HÌNH PIN ---
#define PIN_DC     RPI_V2_GPIO_P1_22 // GPIO 25
#define PIN_RST    RPI_V2_GPIO_P1_18 // GPIO 24
#define PIN_LED    RPI_V2_GPIO_P1_16 // GPIO 23

// --- CẤU HÌNH MÀN HÌNH ---
#define LCD_WIDTH  320
#define LCD_HEIGHT 240

// --- CẤU HÌNH QUEUE ---
#define QUEUE_SIZE 2

#endif