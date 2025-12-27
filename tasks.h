#ifndef TASKS_H
#define TASKS_H

// Các task hiển thị & điều khiển
void* task_lcd(void* arg);
void* task_btn_register(void* arg);
void* task_btn_power(void* arg);

// Các task xử lý chính (Camera & AI)
void* task_camera(void* arg);
void* task_ai(void* arg);

// Các task mạng & dữ liệu
void* task_sync(void* arg);
void* task_config_wifi(void* arg);

// [MỚI] Thêm dòng này để Main gọi được task giám sát mạng
void* task_network_monitor(void* arg);

#endif