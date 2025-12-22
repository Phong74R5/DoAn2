#include <stdio.h>
#include <pthread.h>
#include <atomic>
#include "config.h"
#include "queue_helper.h"
#include "lcd_driver.h"
#include "tasks.h"

std::atomic<bool> g_running(true);
std::mutex mtx_users;
std::mutex mtx_ai;
int main() {
    if (!bcm2835_init()) return 1;

    bcm2835_gpio_fsel(PIN_LED, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_DC, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_RST, BCM2835_GPIO_FSEL_OUTP);

    bcm2835_spi_begin();
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_8); 
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);

    lcd_init_full();
    queue_init(&q_display);

    pthread_t t_cam, t_ai, t_lcd, t_reg, t_pwr, t_sync_th;
    
    pthread_create(&t_cam, NULL, task_camera, NULL);
    pthread_create(&t_ai,  NULL, task_ai, NULL);
    pthread_create(&t_lcd, NULL, task_lcd, NULL);
    pthread_create(&t_reg, NULL, task_btn_register, NULL); 
    pthread_create(&t_pwr, NULL, task_btn_power, NULL);  
    pthread_create(&t_sync_th, NULL, task_sync, NULL);

    pthread_join(t_cam, NULL);
    pthread_join(t_ai, NULL);
    pthread_join(t_lcd, NULL);
    pthread_join(t_reg, NULL);
    pthread_join(t_pwr, NULL);
    pthread_join(t_sync_th, NULL);

    bcm2835_spi_end();
    bcm2835_close();
    return 0;
}