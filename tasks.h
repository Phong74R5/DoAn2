#ifndef TASKS_H
#define TASKS_H

#include <opencv2/opencv.hpp>

// ==========================================
// Task Function Declarations
// ==========================================

// Task 1: Camera capture
void* task_camera(void* arg);

// Task 2: Register button
void* task_button(void* arg);

// Task 2.5: Sleep/Wake button
void* task_sleep_button(void* arg);

// Task 3: Network communication
void* task_network(void* arg);

// Task 4: AI processing
void* task_ai(void* arg);

// Task 5: LCD display
void* task_lcd(void* arg);

#define QUEUE_SIZE 2

#endif // TASKS_H