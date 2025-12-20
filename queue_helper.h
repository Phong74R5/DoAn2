#ifndef QUEUE_HELPER_H
#define QUEUE_HELPER_H

#include <opencv4/opencv2/opencv.hpp>
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
bool queue_pop(FrameQueue* q, cv::Mat* frame_out); // Đã sửa thành bool

// Biến toàn cục cho FrameQueue
extern FrameQueue q_display;

// ==========================================
// PHẦN 2: SAFE QUEUE (C++ Template) - Dùng cho Network
// ==========================================

template <typename T>
class SafeQueue {
private:
    std::queue<T> q;
    std::mutex mtx;
    std::condition_variable cv;
    bool running = true; // Thêm cờ để báo hiệu dừng queue

public:
    // Đẩy vào
    void push(T value) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(value);
        cv.notify_one();
    }

    // Lấy ra (Sửa từ void -> bool)
    bool pop(T* value) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // Chờ dữ liệu HOẶC tín hiệu dừng (running = false)
        cv.wait(lock, [this]{ return !q.empty() || !running; });
        
        // Nếu queue rỗng và đã có lệnh dừng -> trả về false
        if (q.empty() && !running) {
            return false;
        }

        *value = q.front();
        q.pop();
        return true; // Lấy thành công
    }
    
    // Hàm để dừng queue khi tắt app
    void stop() {
        std::lock_guard<std::mutex> lock(mtx);
        running = false;
        cv.notify_all();
    }
    
    bool empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return q.empty();
    }
};

// --- Wrapper functions ---
// Sửa wrapper trả về bool để khớp với logic trong tasks.cpp

template <typename T>
void queue_push(SafeQueue<T>* sq, T val) {
    sq->push(val);
}

template <typename T>
bool queue_pop(SafeQueue<T>* sq, T* val) {
    return sq->pop(val);
}

#endif