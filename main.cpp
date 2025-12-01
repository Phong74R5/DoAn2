#include <stdio.h>
#include <pthread.h>
#include "config.h"
#include "queue_helper.h"
#include "lcd_driver.h"
#include "tasks.h"
#include <atomic>   // Fix lỗi: std::atomic
#include <csignal>  // Fix lỗi: signal, SIGINT
// Định nghĩa thực tế cho các biến extern
FrameQueue q_display;

// Định nghĩa biến toàn cục
std::atomic<bool> g_running(true);

// Khai báo extern để truy cập biến bên tasks.cpp nhằm đánh thức nó
extern std::condition_variable cv_ai; 

// Hàm xử lý khi nhấn Ctrl+C
void signalHandler(int signum) {
    printf("\n[System] Dừng chương trình... (Đang chờ các luồng thoát)\n");
    g_running = false; 

    // QUAN TRỌNG: Đánh thức luồng AI đang ngủ trong cv_ai.wait()
    // Nếu không có dòng này, luồng AI sẽ ngủ mãi mãi -> Treo chương trình
    cv_ai.notify_all(); 
}

int main() {
	signal(SIGINT, signalHandler);
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
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_8); // 31.25MHz on Pi 3/4
    bcm2835_spi_chipSelect(BCM2835_SPI_CS0);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);
    
    printf("System initializing...\n");
    lcd_init_full();
    
    // 2. Init Queues
    // Queue hiển thị cần init (C-style)
    queue_init(&q_display);
    
    // Queue Network (SafeQueue C++) tự động init qua constructor nên không cần gọi hàm init ở đây

    // 3. Create Tasks
    pthread_t t_cam, t_ai, t_lcd, t_btn, t_net;
    printf("Starting 5 tasks...\n");
    
    // Tạo các luồng xử lý
    pthread_create(&t_cam, NULL, task_camera,  NULL);
    pthread_create(&t_ai,  NULL, task_ai,      NULL); // Đã đổi tên từ task_ai_demo -> task_ai
    pthread_create(&t_lcd, NULL, task_lcd,     NULL);
    pthread_create(&t_btn, NULL, task_button,  NULL); // [NEW] Luồng xử lý nút bấm
    pthread_create(&t_net, NULL, task_network, NULL); // [NEW] Luồng xử lý Firebase/Web
    
    // 4. Loop (Chờ các luồng kết thúc - thực tế là chạy mãi mãi)
    pthread_join(t_cam, NULL);
    pthread_join(t_ai,  NULL);
    pthread_join(t_lcd, NULL);
    pthread_join(t_btn, NULL);
    pthread_join(t_net, NULL);

    // Cleanup (Chỉ chạy khi chương trình thoát)
    bcm2835_spi_end();
    bcm2835_close();
    return 0;
}
