#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <bcm2835.h>
#include <cmath>
#include <cstdarg> // Để xử lý tham số log
#include <ctime>   // Để lấy thời gian thực
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>

#include "tasks.h"
#include "config.h"
#include "lcd_driver.h"
#include "queue_helper.h"
#include "facenet.h"
#include "network_helper.h"

// =============================================================
// GLOBAL VARIABLES & SHARED DATA
// =============================================================
std::atomic<bool> g_register_mode(false);
std::atomic<bool> g_is_sleeping(false);
std::vector<UserInfo> g_ram_users;

struct AIResult {
    std::vector<cv::Rect> faces;
    std::string message;
    std::string sub_message; // Hiển thị phụ (VD: Độ chính xác %)
    cv::Scalar color;
    bool has_detection;
};

extern std::mutex mtx_users;
extern std::mutex mtx_ai;
cv::Mat shared_ai_frame;
bool new_frame_for_ai = false;
AIResult shared_result;

// =============================================================
// HELPER FUNCTIONS (LOGGING & UI)
// =============================================================

// Hàm ghi log có thời gian thực: [HH:MM:SS] [TAG] Message
void Log(const char* tag, const char* fmt, ...) {
    time_t now = time(0);
    struct tm tstruct;
    char time_buf[80];
    tstruct = *localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tstruct);

    printf("[%s] [%s] ", time_buf, tag);
    
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

// Vẽ thanh ngang trong suốt (Hiệu ứng kính)
void draw_transparent_bar(cv::Mat& img, int y, int height, cv::Scalar color, double alpha) {
    if (y + height > img.rows) height = img.rows - y;
    cv::Mat roi = img(cv::Rect(0, y, img.cols, height));
    cv::Mat color_layer(roi.size(), CV_8UC3, color);
    cv::addWeighted(color_layer, alpha, roi, 1.0 - alpha, 0.0, roi);
}

// Vẽ khung bao quanh mặt chỉ với 4 góc (Corner Bracket)
void draw_corner_rect(cv::Mat& img, cv::Rect r, cv::Scalar color, int length, int thickness) {
    // Góc trên trái
    cv::line(img, cv::Point(r.x, r.y), cv::Point(r.x + length, r.y), color, thickness);
    cv::line(img, cv::Point(r.x, r.y), cv::Point(r.x, r.y + length), color, thickness);
    // Góc trên phải
    cv::line(img, cv::Point(r.x + r.width, r.y), cv::Point(r.x + r.width - length, r.y), color, thickness);
    cv::line(img, cv::Point(r.x + r.width, r.y), cv::Point(r.x + r.width, r.y + length), color, thickness);
    // Góc dưới trái
    cv::line(img, cv::Point(r.x, r.y + r.height), cv::Point(r.x + length, r.y + r.height), color, thickness);
    cv::line(img, cv::Point(r.x, r.y + r.height), cv::Point(r.x, r.y + r.height - length), color, thickness);
    // Góc dưới phải
    cv::line(img, cv::Point(r.x + r.width, r.y + r.height), cv::Point(r.x + r.width - length, r.y + r.height), color, thickness);
    cv::line(img, cv::Point(r.x + r.width, r.y + r.height), cv::Point(r.x + r.width, r.y + r.height - length), color, thickness);
}

// =============================================================
// TASK IMPLEMENTATIONS
// =============================================================

void* task_btn_register(void* arg) {
    bcm2835_gpio_fsel(PIN_BTN_REG, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PIN_BTN_REG, BCM2835_GPIO_PUD_UP);
    printf("[BTN] Init Register Button Pin %d\n", PIN_BTN_REG);

    while (g_running) {
        if (bcm2835_gpio_lev(PIN_BTN_REG) == LOW) {
            bcm2835_delay(50);

            if (bcm2835_gpio_lev(PIN_BTN_REG) == LOW) {
                g_register_mode = !g_register_mode;
                printf("[BTN] Mode Changed: %s\n", g_register_mode ? "REGISTER" : "SCAN");

                while (bcm2835_gpio_lev(PIN_BTN_REG) == LOW && g_running) {
                    bcm2835_delay(50);
                }
                bcm2835_delay(50);
            }
        }
        bcm2835_delay(50);
    }
    return NULL;
}

void* task_btn_power(void* arg) {
    bcm2835_gpio_fsel(PIN_BTN_SLEEP, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PIN_BTN_SLEEP, BCM2835_GPIO_PUD_UP);
    Log("BTN", "Init Power Button Pin %d", PIN_BTN_SLEEP);
    
    while (g_running) {
        if (bcm2835_gpio_lev(PIN_BTN_SLEEP) == LOW) {
            int hold_time = 0;
            while (bcm2835_gpio_lev(PIN_BTN_SLEEP) == LOW && hold_time < 3000) {
                bcm2835_delay(100);
                hold_time += 100;
            }

            if (hold_time >= 3000) {
                {
                    std::lock_guard<std::mutex> lock(mtx_ai);
                    shared_result.message = "SHUTTING DOWN...";
                    shared_result.color = cv::Scalar(0,0,255);
                }
                Log("PWR", "Shutdown triggered!");
                bcm2835_delay(1000); 
                system("sudo poweroff");
                g_running = false; 
            } 
            else if (hold_time > 50) {
                g_is_sleeping = !g_is_sleeping;
                Log("PWR", "Sleep Mode: %s", g_is_sleeping ? "ON" : "OFF");
                if (g_is_sleeping) bcm2835_gpio_write(PIN_LED, LOW);
                else bcm2835_gpio_write(PIN_LED, HIGH);
            }
            bcm2835_delay(300); 
        }
        bcm2835_delay(50); 
    }
    return NULL;
}

void* task_camera(void* arg) {
    cv::VideoCapture cap(0, cv::CAP_V4L2);
    cap.set(cv::CAP_PROP_FRAME_WIDTH, LCD_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, LCD_HEIGHT);
    cap.set(cv::CAP_PROP_FPS, 30);
    
    if(!cap.isOpened()) Log("CAM", "ERR: Cannot open camera!");
    
    cv::Mat frame;
    while(g_running) {
        if (g_is_sleeping) { usleep(500000); continue; }

        cap >> frame;
        if (frame.empty()) continue;
        cv::resize(frame, frame, cv::Size(LCD_WIDTH, LCD_HEIGHT));

        queue_push(&q_display, frame.clone());

        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            shared_ai_frame = frame.clone();
            new_frame_for_ai = true;
        }
        usleep(1000);
    }
    return NULL;
}

void* task_ai(void* arg) {
    // 1. Load FaceNet
    FaceNet faceNet;
    Log("AI", "Loading FaceNet (MobileFaceNet)...");
    try { faceNet.loadModel("MobileFaceNet.onnx"); } 
    catch(...) { Log("AI", "ERR: No FaceNet Model!"); }
    
    // 2. Load YuNet
    Log("AI", "Loading YuNet...");
    cv::Ptr<cv::FaceDetectorYN> detector;
    try {
        detector = cv::FaceDetectorYN::create(
            "face_detection_yunet_2023mar.onnx", 
            "", 
            cv::Size(LCD_WIDTH, LCD_HEIGHT),
            0.9f, 0.3f, 5000
        );
        Log("AI", "YuNet Loaded OK!");
    } catch (const cv::Exception& e) {
        Log("AI", "FATAL: YuNet Error: %s", e.what());
    }

    g_ram_users = Network_LoadDatabase(); 
    Log("AI", "Database Loaded: %zu users", g_ram_users.size());

    cv::Mat process_frame;
    std::vector<cv::Mat> reg_samples;
    int frame_cnt = 0;

    while(g_running) {
        if (g_is_sleeping) { usleep(500000); continue; }

        bool has_new = false;
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            if (new_frame_for_ai) {
                process_frame = shared_ai_frame.clone();
                new_frame_for_ai = false;
                has_new = true;
            }
        }
        if (!has_new) { usleep(10000); continue; }

        AIResult res;
        res.has_detection = false;
        res.color = cv::Scalar(255, 255, 255);
        res.message = "";
        res.sub_message = "";
        frame_cnt++;

        // ----------------------- DETECT -----------------------
        std::vector<cv::Rect> faces_rect;
        
        if (detector) {
            cv::Mat faces_yunet;
            detector->setInputSize(process_frame.size());
            detector->detect(process_frame, faces_yunet);

            for (int i = 0; i < faces_yunet.rows; i++) {
                float conf = faces_yunet.at<float>(i, 14);
                if (conf > 0.85f) {
                    int x = (int)faces_yunet.at<float>(i, 0);
                    int y = (int)faces_yunet.at<float>(i, 1);
                    int w = (int)faces_yunet.at<float>(i, 2);
                    int h = (int)faces_yunet.at<float>(i, 3);
                    
                    x = std::max(0, x); y = std::max(0, y);
                    w = std::min(w, process_frame.cols - x);
                    h = std::min(h, process_frame.rows - y);

                    if (w > 20 && h > 20) faces_rect.push_back(cv::Rect(x, y, w, h));
                }
            }
        }

        if (!faces_rect.empty()) {
            cv::Rect best = faces_rect[0];
            for(auto f: faces_rect) if(f.area() > best.area()) best = f;
            
            res.faces.push_back(best);
            res.has_detection = true;
            cv::Mat face_roi = process_frame(best);

            // ----------------------- REGISTER LOGIC -----------------------
            if (g_register_mode) {
                res.color = cv::Scalar(255, 0, 0); // Xanh dương (BGR)
                
                float q = faceNet.checkQuality(face_roi);
                if (q > 0.1f) { 
                    reg_samples.push_back(face_roi.clone());
                    Log("Reg", "Sample added %zu/5 (Q: %.2f)", reg_samples.size(), q);
                }

                res.message = "REGISTERING...";
                res.sub_message = "Samples: " + std::to_string(reg_samples.size()) + "/5";
                
                if (reg_samples.size() >= 5) {
                    Log("Reg", "Calculating Embedding...");
                    cv::Mat emb_raw = faceNet.registerOwner(reg_samples);
                    cv::Mat emb_mean;

                    if (emb_raw.rows > 1) cv::reduce(emb_raw, emb_mean, 0, cv::REDUCE_AVG, CV_32F);
                    else emb_mean = emb_raw;

                    UserInfo u;
                    u.id = std::to_string(time(nullptr));
                    u.name = "User_" + u.id.substr(u.id.length()-4);
                    
                    u.embedding.clear();
                    if (emb_mean.isContinuous()) {
                        for(int i=0; i<emb_mean.cols; i++) u.embedding.push_back(emb_mean.at<float>(0,i));
                    }

                    if (Network_SaveUser(u)) {
                        {
                            std::lock_guard<std::mutex> lock(mtx_users);
                            g_ram_users.push_back(u);
                        }
                        res.message = "SUCCESS!";
                        res.sub_message = "New User: " + u.name;
                        res.color = cv::Scalar(0, 255, 0);
                        Log("Reg", "Saved user OK: %s", u.name.c_str());
                    } else {
                        res.message = "SAVE FAILED";
                        res.color = cv::Scalar(0, 0, 255);
                    }
                    
                    reg_samples.clear();
                    g_register_mode = false;
                    sleep(2);
                }
            } 
            // ----------------------- RECOGNIZE LOGIC -----------------------
            else {
                std::lock_guard<std::mutex> lock(mtx_users);
                if (g_ram_users.empty()) {
                    res.message = "DB EMPTY";
                    res.color = cv::Scalar(0, 255, 255); 
                } else {
                    cv::Mat cur_emb = faceNet.getEmbedding(face_roi);
                    if (!cur_emb.empty()) {
                        float max_sim = 0;
                        std::string name = "Unknown";
                        std::string id = "";
                        
                        for(auto& u : g_ram_users) {
                            if (u.embedding.size() != (size_t)cur_emb.cols) continue;
                            cv::Mat db_emb(1, u.embedding.size(), CV_32F, u.embedding.data());
                            
                            double dot = cur_emb.dot(db_emb);
                            double n1 = cv::norm(cur_emb);
                            double n2 = cv::norm(db_emb);
                            float sim = (n1>0 && n2>0) ? dot/(n1*n2) : 0;
                            
                            if (sim > max_sim) { max_sim = sim; name = u.name; id = u.id; }
                        }
                        
                        char sim_str[16];
                        sprintf(sim_str, "(%.0f%%)", max_sim * 100);

                        if (max_sim > 0.9f) { 
                            res.message = name;
                            res.sub_message = std::string("Match ") + sim_str;
                            res.color = cv::Scalar(0, 255, 0); // Xanh lá
                            Network_SendLog(id, name);
                        } else {
                            res.message = "UNKNOWN";
                            res.sub_message = std::string("Low ") + sim_str;
                            res.color = cv::Scalar(0, 0, 255); // Đỏ
                        }
                    }
                }
            }
        } else {
             // Không phát hiện khuôn mặt
             res.message = g_register_mode ? "SHOW FACE" : "SCANNING...";
             res.sub_message = "";
             frame_cnt = 0;
        }

        { std::lock_guard<std::mutex> lock(mtx_ai); shared_result = res; }
        usleep(5000);
    }
    return NULL;
}

void* task_lcd(void* arg) {
    uint8_t* spi_buffer = (uint8_t*)malloc(LCD_WIDTH * LCD_HEIGHT * 2);
    
    // =========================================================
    // 1. BOOT SCREEN LOGIC
    // =========================================================
    Log("LCD", "Displaying Boot Screen...");
    
    // Lưu ý: Cần file "boot_logo.jpg" cùng thư mục với file chạy
    cv::Mat boot_img = cv::imread("boot_logo.jpg"); 
    
    if (boot_img.empty()) {
        // Fallback: Màn hình đen chữ trắng nếu không có ảnh
        boot_img = cv::Mat::zeros(LCD_HEIGHT, LCD_WIDTH, CV_8UC3);
        cv::putText(boot_img, "SYSTEM STARTING...", cv::Point(40, 120), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255,255,255), 2);
    } else {
        cv::resize(boot_img, boot_img, cv::Size(LCD_WIDTH, LCD_HEIGHT));
    }

    // Convert Boot Image RGB -> RGB565 & Send
    int b_idx = 0;
    uint8_t* b_p = boot_img.data;
    for(int i=0; i<LCD_WIDTH*LCD_HEIGHT; i++) {
        uint8_t b = *b_p++; uint8_t g = *b_p++; uint8_t r = *b_p++;
        uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        spi_buffer[b_idx++] = c >> 8; spi_buffer[b_idx++] = c & 0xFF;
    }
    
    bcm2835_gpio_write(PIN_DC, LOW);
    lcd_set_window(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1);
    bcm2835_gpio_write(PIN_DC, HIGH);
    bcm2835_spi_transfern((char*)spi_buffer, LCD_WIDTH*LCD_HEIGHT*2);
    
    sleep(3); // Giữ màn hình khởi động trong 3 giây

    // =========================================================
    // 2. MAIN LCD LOOP
    // =========================================================
    cv::Mat frame;
    AIResult ai_state;
    
    while(g_running) {
        if (g_is_sleeping) { usleep(200000); continue; }
        
        queue_pop(&q_display, &frame); 
        { std::lock_guard<std::mutex> lock(mtx_ai); ai_state = shared_result; }

        // --- BẮT ĐẦU VẼ GIAO DIỆN (HUD) ---
        
        // 1. Vẽ thanh trạng thái trên (Top Bar) - Trong suốt
        draw_transparent_bar(frame, 0, 35, cv::Scalar(0,0,0), 0.6);
        
        // Hiển thị giờ hệ thống
        time_t now = time(0);
        struct tm tstruct;
        char time_buf[10];
        tstruct = *localtime(&now);
        strftime(time_buf, sizeof(time_buf), "%H:%M", &tstruct);
        cv::putText(frame, time_buf, cv::Point(265, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(220,220,220), 1);

        // Hiển thị Mode
        if (g_register_mode) {
             cv::putText(frame, "MODE: REGISTER", cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0,0,255), 2);
             cv::circle(frame, cv::Point(170, 20), 6, cv::Scalar(0,0,255), -1); // Dấu tròn đỏ recording
        } else {
             cv::putText(frame, "ACCESS CONTROL", cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0,255,0), 1);
        }

        // 2. Vẽ thanh trạng thái dưới (Bottom Bar) - Trong suốt
        draw_transparent_bar(frame, LCD_HEIGHT-45, 45, cv::Scalar(0,0,0), 0.7);

        // 3. Vẽ Face Box & Info
        if (ai_state.has_detection) {
             for(auto r : ai_state.faces) {
                 // Vẽ khung góc cách điệu (Corner Rect) thay vì hình chữ nhật kín
                 draw_corner_rect(frame, r, ai_state.color, 20, 2);
             }
             
             // Message chính (Tên User) - Font to, rõ
             cv::putText(frame, ai_state.message, cv::Point(10, LCD_HEIGHT-22), 
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, ai_state.color, 2);
             
             // Message phụ (% Match) - Font nhỏ hơn
             if (!ai_state.sub_message.empty()) {
                 cv::putText(frame, ai_state.sub_message, cv::Point(10, LCD_HEIGHT-6), 
                            cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(200,200,200), 1);
             }
        } else {
             // Trạng thái chờ
             cv::putText(frame, ai_state.message, cv::Point(10, LCD_HEIGHT-15), 
                        cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(200,200,200), 1);
        }

        // 4. Convert OpenCV Mat -> RGB565 Buffer cho LCD
        int idx = 0;
        uint8_t* p = frame.data;
        int total_pixels = LCD_WIDTH * LCD_HEIGHT;
        
        for(int i=0; i<total_pixels; i++) {
            uint8_t b = *p++; 
            uint8_t g = *p++; 
            uint8_t r = *p++;
            
            // Công thức chuẩn RGB888 to RGB565
            uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            
            spi_buffer[idx++] = (c >> 8); 
            spi_buffer[idx++] = (c & 0xFF);
        }
        
        // 5. Gửi qua SPI
        bcm2835_gpio_write(PIN_DC, LOW);
        lcd_set_window(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1);
        bcm2835_gpio_write(PIN_DC, HIGH);
        bcm2835_spi_transfern((char*)spi_buffer, LCD_WIDTH*LCD_HEIGHT*2);
    }
    
    free(spi_buffer);
    return NULL;
}

void* task_sync(void* arg) {
    while (g_running) {
        for (int i=0; i<10; i++) { if (!g_running) return NULL; sleep(1); }
        if (g_is_sleeping) continue;
        {
            std::lock_guard<std::mutex> lock(mtx_users);
            if (Network_SyncFromCloud(g_ram_users)) Log("Sync", "Database Updated from Cloud");
        }
    }
    return NULL;
}