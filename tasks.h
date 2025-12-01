#ifndef TASKS_H
#define TASKS_H
extern std::atomic<bool> g_running;
// Task Camera: Đọc ảnh từ Camera & đẩy vào Queue
void* task_camera(void* arg);

// Task AI: Xử lý nhận diện & Điều phối logic (Đổi tên từ task_ai_demo)
void* task_ai(void* arg);

// Task LCD: Hiển thị hình ảnh lên màn hình
void* task_lcd(void* arg);

// [NEW] Task Button: Xử lý ngắt/polling nút bấm vật lý (GPIO)
void* task_button(void* arg);

// [NEW] Task Network: Xử lý các request HTTP (Firebase) nặng nề
void* task_network(void* arg);

#endif
