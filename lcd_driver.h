#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdint.h>
#include "config.h"

void lcd_cmd(uint8_t cmd);
void lcd_dat(uint8_t dat);
void lcd_init_full();
void lcd_set_window(int x1, int y1, int x2, int y2);

#endif