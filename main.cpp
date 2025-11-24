#include <stdio.h>
#include <pthread.h>
#include "config.h"
#include "queue_helper.h"
#include "lcd_driver.h"
#include "tasks.h"

// Định nghĩa thực tế cho các biến extern
//FrameQueue q_raw;
FrameQueue q_display;

int main() {
    // 1. Init Hardware
    if (!bcm2835_init()) return 1;
    
    // Cấu hình GPIO (Nhớ cấu hình PIN_LED ở đây)
    bcm2835_gpio_fsel(PIN_LED, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_DC, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_RST, BCM2835_GPIO_FSEL_OUTP);
    
    // Cấu hình SPI
    bcm2835_spi_begin();
    bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_8);
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);
    
    printf("System initializing...\n");
    lcd_init_full();
    
    // 2. Init Queues
//    queue_init(&q_raw);
    queue_init(&q_display);

    // 3. Create Tasks
    pthread_t t_cam, t_ai, t_lcd;
    printf("Starting tasks...\n");
    
    pthread_create(&t_cam, NULL, task_camera, NULL);
    pthread_create(&t_ai,  NULL, task_ai_demo, NULL);
    pthread_create(&t_lcd, NULL, task_lcd,    NULL);
    
    // 4. Loop
    pthread_join(t_cam, NULL);
    pthread_join(t_ai,  NULL);
    pthread_join(t_lcd, NULL);

    bcm2835_spi_end();
    bcm2835_close();
    return 0;
}
