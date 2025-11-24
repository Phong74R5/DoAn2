#include "queue_helper.h"

void queue_init(FrameQueue* q) {
    q->head = 0; q->tail = 0; q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_not_empty, NULL);
}

void queue_push(FrameQueue* q, cv::Mat frame) {
    pthread_mutex_lock(&q->mutex);
    if (q->count == QUEUE_SIZE) {
//	printf("Warning: Queue full, dropping oldest frame!\n");
        q->head = (q->tail + 1) % QUEUE_SIZE;
        q->count--;
    }
    q->frames[q->tail] = frame;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->cond_not_empty);
    pthread_mutex_unlock(&q->mutex);
}

void queue_pop(FrameQueue* q, cv::Mat* frame_out) {
    pthread_mutex_lock(&q->mutex);
    //ch? n?u queue r?ng
    while (q->count == 0) {
        pthread_cond_wait(&q->cond_not_empty, &q->mutex);
    }
    *frame_out = q->frames[q->head];
    //C?p nh?t head
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    pthread_mutex_unlock(&q->mutex);
}
