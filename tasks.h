#ifndef TASKS_H
#define TASKS_H

void* task_camera(void* arg);
void* task_ai(void* arg);
void* task_lcd(void* arg);
void* task_btn_register(void* arg);
void* task_btn_power(void* arg);
void* task_sync(void* arg);

#endif