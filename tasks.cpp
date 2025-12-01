#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <bcm2835.h> // [CHANGE] Thay wiringPi bằng bcm2835

#include "tasks.h"
#include "config.h"
#include "lcd_driver.h"
#include "queue_helper.h"
#include "facenet.h"
#include "network_helper.h"

// --- CONFIG ---
// LƯU Ý: Đây là số BCM GPIO.
// Nếu bạn dùng chân vật lý 15, hãy điền 22. Nếu chân vật lý 10, điền 15 (RXD).
// Giả sử bạn dùng GPIO 15 (BCM) - Chân vật lý 10
// --- ĐỊNH NGHĨA STRUCT DÙNG CHUNG ---
struct AIResult {
    std::vector<cv::Rect> faces;
    std::string message;
    cv::Scalar color;
    bool has_detection;
};

// --- GLOBALS ---
std::mutex mtx_ai;
std::condition_variable cv_ai;
cv::Mat shared_ai_frame;
bool new_frame_for_ai = false;

AIResult shared_result;
FaceNet faceNet;

// Queue chuyển việc sang Task Web
SafeQueue<NetworkJob> q_network; 
std::atomic<bool> g_is_register_requested(false);

// ==========================================
// TASK 1: CAMERA
// ==========================================
void* task_camera(void* arg) {
    // ... Code Camera giữ nguyên ...
    cv::VideoCapture cap(0, cv::CAP_V4L2);
    cap.set(cv::CAP_PROP_FRAME_WIDTH, LCD_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, LCD_HEIGHT);
    cap.set(cv::CAP_PROP_FPS, 30);

    if (!cap.isOpened()) return NULL;

    cv::Mat frame;
    while(g_running) {
        cap >> frame;
        if (frame.empty()) { usleep(10000); continue; }
        if (frame.cols != LCD_WIDTH) cv::resize(frame, frame, cv::Size(LCD_WIDTH, LCD_HEIGHT));

        queue_push(&q_display, frame.clone());

        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            shared_ai_frame = frame.clone();
            new_frame_for_ai = true;
        }
        cv_ai.notify_one(); 
        usleep(1000); 
    }
    return NULL;
}

// ==========================================
// TASK 2: BUTTON (Dùng bcm2835)
// ==========================================
void* task_button(void* arg) {
    printf("[Task Button] Init BCM GPIO %d...\n", PIN_BUTTON);
    
    // 1. Cấu hình GPIO dùng thư viện bcm2835
    // Input
    bcm2835_gpio_fsel(PIN_BUTTON, BCM2835_GPIO_FSEL_INPT);
    // Kéo trở kháng lên (Pull-UP) -> Trạng thái bình thường là HIGH (1)
    bcm2835_gpio_set_pud(PIN_BUTTON, BCM2835_GPIO_PUD_UP);

    while(g_running) {
        // 2. Đọc trạng thái (LOW = 0, HIGH = 1)
        uint8_t value = bcm2835_gpio_lev(PIN_BUTTON);

        // Logic Pull-UP: Nhấn nút nối đất sẽ về LOW (0)
        if (value == LOW) {
            // Chống rung (Debounce) đơn giản: Chờ 50ms rồi check lại
            bcm2835_delay(50); 
            
            if (bcm2835_gpio_lev(PIN_BUTTON) == LOW) {
                // Check cờ để không spam request
                if (!g_is_register_requested) {
                    printf("[Button] Check BCM Pin %d: PRESSED!\n", PIN_BUTTON);
                    g_is_register_requested = true;
                    
                    // Chờ nhả nút (để không bị dính phím)
                    while (bcm2835_gpio_lev(PIN_BUTTON) == LOW) {
                        bcm2835_delay(10);
                    }
                }
            }
        }
        
        // Ngủ 20ms để giảm tải CPU
        bcm2835_delay(20);
    }
    return NULL;
}

// ==========================================
// TASK 3: NETWORK / WEB
// ==========================================
void* task_network(void* arg) {
    // ... Code Network giữ nguyên ...
    printf("[Task Net] Loading data...\n");
    Network_LoadUsers(); 

    NetworkJob job;
    while(g_running) {
        queue_pop(&q_network, &job);

        switch (job.type) {
            case JOB_LOG_ATTENDANCE:
                Network_SendLog(job.id, job.name); 
                break;
            case JOB_REGISTER_USER:
                Network_RegisterUser(job.embedding); 
                break;
            case JOB_LOAD_USERS:
                Network_LoadUsers();
                break;
        }
    }
    return NULL;
}

// ==========================================
// TASK 4: AI LOGIC
// ==========================================
void* task_ai(void* arg) {
    // ... Code AI giữ nguyên ...
    if (!faceNet.loadModel("MobileFaceNet.onnx")) return NULL;
    cv::CascadeClassifier face_cascade;
    face_cascade.load("/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml");
    
    cv::Mat process_frame;

    while(g_running) {
        std::unique_lock<std::mutex> lock(mtx_ai);
        
        // Wait có timeout (ví dụ 100ms) để thỉnh thoảng tỉnh dậy check g_running
        // Hoặc dùng wait thường vì ta đã có notify_all bên main rồi
        cv_ai.wait(lock, []{ return new_frame_for_ai || !g_running; });

        // Nếu bị đánh thức vì muốn tắt chương trình -> Thoát luôn
        if (!g_running) break;
        process_frame = shared_ai_frame.clone();
        new_frame_for_ai = false;
        lock.unlock();

        AIResult local_result;
        local_result.has_detection = false;
        local_result.color = cv::Scalar(0, 255, 255);

        std::vector<cv::Rect> faces;
        cv::Mat gray;
        cv::cvtColor(process_frame, gray, cv::COLOR_BGR2GRAY);
        face_cascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(40, 40));

        if (!faces.empty()) {
            local_result.has_detection = true;
            local_result.faces = faces;
            cv::Mat face_roi = process_frame(faces[0]);
            cv::Mat current_embedding = faceNet.getEmbedding(face_roi);

            // [CHECK FLAG] Kiểm tra biến atomic set bởi Task Button
            if (g_is_register_requested) {
                g_is_register_requested = false; // Reset cờ
                
                {
                    std::lock_guard<std::mutex> lk(mtx_ai);
                    shared_result.message = "REGISTERING...";
                    shared_result.color = cv::Scalar(255, 0, 0); 
                }

                NetworkJob job;
                job.type = JOB_REGISTER_USER;
                job.embedding = current_embedding.clone();
                queue_push(&q_network, job);
                continue;
            }

            // Nhận diện bình thường
            std::string name, id;
            float score = 0.0f;
            bool match = Network_FindMatch(current_embedding, faceNet, name, id, score);

            if (match) {
                std::string disp = name.empty() ? id : name;
                local_result.message = disp + " " + std::to_string((int)(score*100)) + "%";
                local_result.color = cv::Scalar(0, 255, 0);

                NetworkJob job;
                job.type = JOB_LOG_ATTENDANCE;
                job.id = id;
                job.name = disp;
                queue_push(&q_network, job);
            } else {
                local_result.message = "UNKNOWN";
                local_result.color = cv::Scalar(0, 0, 255);
            }
        } else {
             if (g_is_register_requested) g_is_register_requested = false;
             local_result.message = "";
        }

        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            shared_result = local_result;
        }
    }
    return NULL;
}

// ==========================================
// TASK 5: LCD
// ==========================================
void* task_lcd(void* arg) {
    uint8_t* spi_buffer = (uint8_t*)malloc(LCD_WIDTH * LCD_HEIGHT * 2);
    // ... Code LCD cũ, dùng bcm2835_spi_transfern ...
    // Giữ nguyên logic vẽ
    cv::Mat frame;
    AIResult current_ai_state;

    while(g_running) {
        queue_pop(&q_display, &frame);
        
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            current_ai_state = shared_result;
        }

        if (current_ai_state.has_detection) {
             for (auto& rect : current_ai_state.faces) {
                cv::rectangle(frame, rect, current_ai_state.color, 2);
            }
            if (!current_ai_state.message.empty() && !current_ai_state.faces.empty()) {
                cv::Point p = current_ai_state.faces[0].tl();
                p.y = (p.y < 20) ? 20 : p.y - 10;
                cv::putText(frame, current_ai_state.message, p, 
                            cv::FONT_HERSHEY_SIMPLEX, 0.6, current_ai_state.color, 2);
            }
        }

        // Convert RGB565
        uint8_t* pSrc = frame.data;
        uint8_t* pDst = spi_buffer;
        int num_pixels = LCD_WIDTH * LCD_HEIGHT;
        for (int i = 0; i < num_pixels; i++) {
            uint8_t b = *pSrc++; uint8_t g = *pSrc++; uint8_t r = *pSrc++;
            uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            *pDst++ = (color >> 8); *pDst++ = (color & 0xFF);
        }

        // Gửi SPI (dùng bcm2835 sẵn có)
        bcm2835_gpio_write(PIN_DC, LOW);
        lcd_set_window(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1); 
        bcm2835_gpio_write(PIN_DC, HIGH);
        bcm2835_spi_transfern((char*)spi_buffer, LCD_WIDTH * LCD_HEIGHT * 2);
    }
    free(spi_buffer);
    return NULL;
}
