#include "queue_helper.h"

void queue_init(FrameQueue* q) {
    q->head = 0; q->tail = 0; q->count = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_not_empty, NULL);
}

void queue_push(FrameQueue* q, Mat frame) {
    pthread_mutex_lock(&q->mutex);
    if (q->count == QUEUE_SIZE) {
        q->tail = (q->tail + 1) % QUEUE_SIZE;
        q->count--;
    }
    q->frames[q->head] = frame;
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->cond_not_empty);
    pthread_mutex_unlock(&q->mutex);
}

void queue_pop(FrameQueue* q, Mat* frame_out) {
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0) {
        pthread_cond_wait(&q->cond_not_empty, &q->mutex);
    }
    *frame_out = q->frames[q->tail];
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count--;
    pthread_mutex_unlock(&q->mutex);
}