#include <stdio.h>
#include <unistd.h>
#include "tasks.h"
#include "queue_helper.h"
#include "lcd_driver.h"
#include "config.h"

// --- TASK 1: CAMERA ---
void* task_camera(void* arg) {
    VideoCapture cap(0, CAP_V4L2);
    cap.set(CAP_PROP_FRAME_WIDTH, 320);
    cap.set(CAP_PROP_FRAME_HEIGHT, 240);
    cap.set(CAP_PROP_FPS, 60);

    if (!cap.isOpened()) {
        printf("[Task Cam] Error: Cannot open camera\n");
        return NULL;
    }
    
    Mat frame;
    printf("[Task Cam] Started\n");
    
    while(1) {
        cap >> frame;
        if (frame.empty()) continue;
        queue_push(&q_raw, frame.clone());
        usleep(1000);
    }
    return NULL;
}

// --- TASK 2: AI DEMO ---
void* task_ai_demo(void* arg) {
    Mat frame;
    printf("[Task AI] Started\n");
    int box_x = 0;
    int direction = 5;

    while(1) {
        queue_pop(&q_raw, &frame);
        
        putText(frame, "AI DEMO: RUNNING", Point(10, 30), 
                FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 255), 2);
        rectangle(frame, Point(box_x, 100), Point(box_x + 50, 150), Scalar(0, 255, 0), 2);
        
        box_x += direction;
        if (box_x > (LCD_WIDTH - 55) || box_x < 5) direction *= -1;
        
        queue_push(&q_display, frame);
    }
    return NULL;
}

// --- TASK 3: LCD ---
void* task_lcd(void* arg) {
    uint8_t* spi_buffer = (uint8_t*)malloc(LCD_WIDTH * LCD_HEIGHT * 2);
    Mat frame;
    printf("[Task LCD] Started\n");
    
    while(1) {
        queue_pop(&q_display, &frame);
        
        int idx = 0;
        uint8_t* data = frame.data;
        int channels = frame.channels();
        
        // Convert BGR -> RGB565
        for (int i = 0; i < LCD_HEIGHT; i++) {
            for (int j = 0; j < LCD_WIDTH; j++) {
                int pixel_idx = (i * LCD_WIDTH * channels) + (j * channels);
                uint8_t b = data[pixel_idx + 0];
                uint8_t g = data[pixel_idx + 1];
                uint8_t r = data[pixel_idx + 2];
                uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                spi_buffer[idx++] = c >> 8;
                spi_buffer[idx++] = c & 0xFF;
            }
        }
        
        // Gửi SPI
        bcm2835_gpio_write(PIN_DC, LOW);
        lcd_set_window(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1);
        bcm2835_gpio_write(PIN_DC, HIGH);
        bcm2835_spi_transfern((char*)spi_buffer, LCD_WIDTH * LCD_HEIGHT * 2);
    }
    free(spi_buffer);
    return NULL;
}