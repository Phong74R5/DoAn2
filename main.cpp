#include <stdio.h>
#include <pthread.h>
#include <atomic>
#include <unistd.h>
#include <vector> // Dùng vector quản lý thread cho gọn

#include "config.h"
#include "queue_helper.h"
#include "lcd_driver.h"
#include "tasks.h"
#include "wifi_helper.h"

// ==========================================
// BIẾN TOÀN CỤC (GLOBAL VARIABLES)
// ==========================================
std::atomic<bool> g_running(true);
std::mutex mtx_ai;
std::mutex mtx_users;

// Biến điều khiển trạng thái WiFi
std::atomic<int> g_btn_cmd(0);           // 0: None, 1: Short, 2: Long
std::atomic<bool> g_wifi_mode_active(false); // True: Đang hiện menu WiFi

// ==========================================
// MAIN PROGRAM
// ==========================================
int main() {
    // 1. INIT HARDWARE
    if (!bcm2835_init()) return 1;

    bcm2835_gpio_fsel(PIN_LED, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_DC, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_RST, BCM2835_GPIO_FSEL_OUTP);
    
    bcm2835_spi_begin();
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_8);
    
    lcd_init_full();
    queue_init(&q_display);

    printf("[MAIN] Hardware Init Done.\n");

    // 2. KHỞI TẠO CÁC TASK
    // Dùng vector để quản lý thread dễ hơn
    std::vector<pthread_t> threads;
    pthread_t t;

    // --- NHÓM 1: GIAO DIỆN & ĐIỀU KHIỂN ---
    // Chạy LCD trước để hiển thị logo/boot
    pthread_create(&t, NULL, task_lcd, NULL);          threads.push_back(t);
    pthread_create(&t, NULL, task_btn_register, NULL); threads.push_back(t); // Nút bấm dùng chung cho cả App & Wifi
    pthread_create(&t, NULL, task_btn_power, NULL);    threads.push_back(t);
    
    // --- NHÓM 2: CORE APP (CAMERA & AI) ---
    // Cho phép chạy Offline (nhận diện khuôn mặt cục bộ)
    pthread_create(&t, NULL, task_camera, NULL);       threads.push_back(t);
    pthread_create(&t, NULL, task_ai, NULL);           threads.push_back(t);
    
    // --- NHÓM 3: MẠNG & DATA ---
    pthread_create(&t, NULL, task_sync, NULL);         threads.push_back(t);

    // --- NHÓM 4: GIÁM SÁT MẠNG (QUAN TRỌNG NHẤT) ---
    // Task này sẽ tự động check mạng -> Nếu mất -> Gọi Config Wifi
    // Bạn cần đảm bảo đã khai báo hàm này trong tasks.h
    pthread_create(&t, NULL, task_network_monitor, NULL); threads.push_back(t);

    printf("[MAIN] All tasks started. System running...\n");

    // 3. CHỜ KẾT THÚC (BLOCK MAIN THREAD)
    for (auto& th : threads) {
        pthread_join(th, NULL);
    }

    // 4. CLEANUP
    g_running = false;
    bcm2835_spi_end();
    bcm2835_close();
    return 0;
}