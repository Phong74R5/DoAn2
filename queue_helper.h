#ifndef QUEUE_HELPER_H
#define QUEUE_HELPER_H

#include <opencv2/opencv.hpp>
#include <pthread.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "config.h"

// ==========================================
// PHẦN 1: FRAME QUEUE (C-Style) - Dùng cho Camera
// ==========================================
typedef struct {
    cv::Mat frames[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
} FrameQueue;

// Hàm cho FrameQueue (Code nằm bên .cpp)
void queue_init(FrameQueue* q);
void queue_push(FrameQueue* q, cv::Mat frame);
void queue_pop(FrameQueue* q, cv::Mat* frame_out);

// Biến toàn cục cho FrameQueue
extern FrameQueue q_display;

// ==========================================
// PHẦN 2: SAFE QUEUE (C++ Template) - Dùng cho Network
// ==========================================
// LƯU Ý: Template phải viết toàn bộ trong file .h

template <typename T>
class SafeQueue {
private:
    std::queue<T> q;
    std::mutex mtx;
    std::condition_variable cv;
public:
    // Đẩy vào
    void push(T value) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(value);
        cv.notify_one();
    }

    // Lấy ra
    void pop(T* value) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]{ return !q.empty(); });
        *value = q.front();
        q.pop();
    }
    
    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return q.empty();
    }
};

// --- Wrapper functions ---
// Các hàm này giúp code cũ (queue_push(&q, val)) chạy được với SafeQueue mới
template <typename T>
void queue_push(SafeQueue<T>* sq, T val) {
    sq->push(val);
}

template <typename T>
void queue_pop(SafeQueue<T>* sq, T* val) {
    sq->pop(val);
}

#endif