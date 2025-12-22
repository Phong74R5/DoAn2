#ifndef CONFIG_H
#define CONFIG_H

#include <bcm2835.h>
#include <atomic>

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

#endif