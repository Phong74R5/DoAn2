#include "queue_helper.h"
#include <stdio.h>

// Định nghĩa biến toàn cục
FrameQueue q_display;

void queue_init(FrameQueue* q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_not_empty, NULL);
}

void queue_push(FrameQueue* q, cv::Mat frame) {
    pthread_mutex_lock(&q->mutex);
    // Nếu queue chưa đầy thì thêm vào
    if (q->count < QUEUE_SIZE) {
        q->frames[q->head] = frame;
        q->head = (q->head + 1) % QUEUE_SIZE;
        q->count++;
        pthread_cond_signal(&q->cond_not_empty);
    } else {
        // Queue đầy: có thể drop frame hoặc ghi đè (ở đây chọn drop để giữ đơn giản)
        // printf("Queue full!\n");
    }
    pthread_mutex_unlock(&q->mutex);
}

// === PHẦN SỬA QUAN TRỌNG: Đổi void -> bool ===
bool queue_pop(FrameQueue* q, cv::Mat* frame_out) {
    pthread_mutex_lock(&q->mutex);
    
    while (q->count == 0) {
        pthread_cond_wait(&q->cond_not_empty, &q->mutex);
    }

    *frame_out = q->frames[q->tail];
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count--;
    
    pthread_mutex_unlock(&q->mutex);
    
    return true; // Trả về true để báo lấy thành công
}