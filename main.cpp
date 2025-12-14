#include <stdio.h>
#include <pthread.h>
#include <atomic>
#include "config.h"
#include "queue_helper.h"
#include "lcd_driver.h"
#include "tasks.h"

// Biến toàn cục
std::atomic<bool> g_running(true);

int main() {
    // 1. Init Hardware (BCM2835)
    if (!bcm2835_init()) {
        printf("BCM2835 Init Failed!\n");
        return 1;
    }
    
    // Cấu hình GPIO Output (LED, LCD Control)
    bcm2835_gpio_fsel(PIN_LED, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_DC, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_RST, BCM2835_GPIO_FSEL_OUTP);
    
    // Cấu hình SPI
    bcm2835_spi_begin();
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_8); // 31.25MHz
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);
    
    printf("System initializing...\n");
    lcd_init_full();
    
    // 2. Init Queues
    queue_init(&q_display);
    
    // 3. Create Tasks
    pthread_t t_cam, t_ai, t_lcd, t_btn, t_net, t_sleep;
    printf("Starting 6 tasks...\n");
    
    pthread_create(&t_cam,   NULL, task_camera,      NULL);
    pthread_create(&t_ai,    NULL, task_ai,          NULL);
    pthread_create(&t_lcd,   NULL, task_lcd,         NULL);
    pthread_create(&t_btn,   NULL, task_button,      NULL);
    pthread_create(&t_net,   NULL, task_network,     NULL);
    pthread_create(&t_sleep, NULL, task_sleep_button, NULL); // [NEW] Sleep/Wake button
    
    printf("[System] All threads started. Running...\n");
    
    // 4. Join threads (chờ thoát - thực tế chạy mãi mãi)
    pthread_join(t_cam, NULL);
    pthread_join(t_ai,  NULL);
    pthread_join(t_lcd, NULL);
    pthread_join(t_btn, NULL);
    pthread_join(t_net, NULL);
    pthread_join(t_sleep, NULL);
    
    // 5. Cleanup
    bcm2835_spi_end();
    bcm2835_close();
    
    return 0;
}