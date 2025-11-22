#ifndef QUEUE_HELPER_H
#define QUEUE_HELPER_H

#include <opencv2/opencv.hpp>
#include <pthread.h>
#include "config.h"

using namespace cv;

typedef struct {
    Mat frames[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
} FrameQueue;

// Khai báo hàm
void queue_init(FrameQueue* q);
void queue_push(FrameQueue* q, Mat frame);
void queue_pop(FrameQueue* q, Mat* frame_out);

// Khai báo biến toàn cục (extern) để các file khác cùng thấy
extern FrameQueue q_raw;
extern FrameQueue q_display;

#endif