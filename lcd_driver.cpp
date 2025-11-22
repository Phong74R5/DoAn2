#include "lcd_driver.h"

void lcd_cmd(uint8_t cmd) {
    bcm2835_gpio_write(PIN_DC, LOW);
    bcm2835_spi_transfer(cmd);
}

void lcd_dat(uint8_t dat) {
    bcm2835_gpio_write(PIN_DC, HIGH);
    bcm2835_spi_transfer(dat);
}

void lcd_set_window(int x1, int y1, int x2, int y2) {
    lcd_cmd(0x2A); lcd_dat(x1>>8); lcd_dat(x1); lcd_dat(x2>>8); lcd_dat(x2);
    lcd_cmd(0x2B); lcd_dat(y1>>8); lcd_dat(y1); lcd_dat(y2>>8); lcd_dat(y2);
    lcd_cmd(0x2C);
}

void lcd_init_full() {
    // Bật đèn nền
    bcm2835_gpio_write(PIN_LED, HIGH);
    
    // Reset cứng
    bcm2835_gpio_write(PIN_RST, LOW); bcm2835_delay(20);
    bcm2835_gpio_write(PIN_RST, HIGH); bcm2835_delay(150);

    lcd_cmd(0x01); bcm2835_delay(120);
    lcd_cmd(0x28);
    lcd_cmd(0xCF); lcd_dat(0x00); lcd_dat(0x83); lcd_dat(0x30);
    lcd_cmd(0xED); lcd_dat(0x64); lcd_dat(0x03); lcd_dat(0x12); lcd_dat(0x81);
    lcd_cmd(0xE8); lcd_dat(0x85); lcd_dat(0x01); lcd_dat(0x79);
    lcd_cmd(0xCB); lcd_dat(0x39); lcd_dat(0x2C); lcd_dat(0x00); lcd_dat(0x34); lcd_dat(0x02);
    lcd_cmd(0xF7); lcd_dat(0x20);
    lcd_cmd(0xEA); lcd_dat(0x00); lcd_dat(0x00);
    lcd_cmd(0xC0); lcd_dat(0x26);
    lcd_cmd(0xC1); lcd_dat(0x11);
    lcd_cmd(0xC5); lcd_dat(0x35); lcd_dat(0x3E);
    lcd_cmd(0xC7); lcd_dat(0xBE);
    
    lcd_cmd(0x36); lcd_dat(0x28); 
    
    lcd_cmd(0x3A); lcd_dat(0x55);
    lcd_cmd(0xB1); lcd_dat(0x00); lcd_dat(0x1B);
    lcd_cmd(0x26); lcd_dat(0x01);
    lcd_cmd(0xE0); lcd_dat(0x1F); lcd_dat(0x1A); lcd_dat(0x18); lcd_dat(0x0A); lcd_dat(0x0F); lcd_dat(0x06); lcd_dat(0x45); lcd_dat(0x87); lcd_dat(0x32); lcd_dat(0x0A); lcd_dat(0x07); lcd_dat(0x02); lcd_dat(0x07); lcd_dat(0x05); lcd_dat(0x00);
    lcd_cmd(0xE1); lcd_dat(0x00); lcd_dat(0x25); lcd_dat(0x27); lcd_dat(0x05); lcd_dat(0x10); lcd_dat(0x09); lcd_dat(0x3A); lcd_dat(0x78); lcd_dat(0x4D); lcd_dat(0x05); lcd_dat(0x18); lcd_dat(0x0D); lcd_dat(0x38); lcd_dat(0x3A); lcd_dat(0x1F);
    lcd_cmd(0x11); bcm2835_delay(120);
    lcd_cmd(0x29); bcm2835_delay(20);
}