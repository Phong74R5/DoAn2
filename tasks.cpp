// ============================================================
// STANDARD C/C++ HEADERS (ĐẶT TRƯỚC OPENCV)
// ============================================================
#include <cstdint>      // ← THÊM DÒNG NÀY (int64, uint64)
#include <cstddef>      // size_t
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>

// C++ STL
#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <algorithm>

// ============================================================
// OPENCV HEADERS (SAU KHI ĐÃ CÓ CSTDINT)
// ============================================================
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/core.hpp>

// ============================================================
// SYSTEM HEADERS
// ============================================================
#include <pthread.h>
#include <unistd.h>
#include <bcm2835.h>

// ============================================================
// PROJECT HEADERS
// ============================================================
#include "tasks.h"
#include "config.h"
#include "lcd_driver.h"
#include "queue_helper.h"
#include "facenet.h"
#include "network_helper.h"
#include "wifi_helper.h"
// ============================================================
// COMMAND DEFINITIONS
// ============================================================
#define CMD_NONE        0
#define CMD_SINGLE      1   // Short Press (Next)
#define CMD_LONG        2   // (Dự phòng - Không dùng trong Wifi mode này)
#define CMD_SUPER_LONG  3   // Super Long > 3s (Connect)
#define CMD_DOUBLE      4   // Double Click
// --- LỆNH MỚI CHO GIAO DIỆN 2 NÚT ---
#define CMD_WIFI_SELECT 5   // Nút Sleep: Chọn ký tự (Select/Enter)
#define CMD_SCROLL      6   // Nút Reg (Giữ): Cuộn nhanh (Fast Scroll)

// =============================================================
// GLOBAL VARIABLES & SHARED DATA
// =============================================================
std::atomic<bool> g_register_mode(false);
std::atomic<bool> g_is_sleeping(false);
std::vector<UserInfo> g_ram_users;
std::atomic<bool> g_is_online(false); // Mặc định là Offline (False)
// Hướng dẫn hiển thị cho từng bước đăng ký
const std::vector<std::string> REG_STEPS_MSG = {
    "1. LOOK STRAIGHT (Nhin Thang)",
    "2. LOOK LEFT (Quay Trai)",
    "3. LOOK RIGHT (Quay Phai)",
    "4. LOOK UP (Nhin Len)",
    "5. LOOK DOWN (Nhin Xuong)"
};

// Thêm '<' để xóa ký tự sai
const std::string CHAR_SET_WIFI = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._@#<K";

struct AIResult {
    std::vector<cv::Rect> faces;
    std::string message;
    std::string sub_message; 
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

void draw_transparent_bar(cv::Mat& img, int y, int height, cv::Scalar color, double alpha) {
    if (y + height > img.rows) height = img.rows - y;
    cv::Mat roi = img(cv::Rect(0, y, img.cols, height));
    cv::Mat color_layer(roi.size(), CV_8UC3, color);
    cv::addWeighted(color_layer, alpha, roi, 1.0 - alpha, 0.0, roi);
}

void draw_corner_rect(cv::Mat& img, cv::Rect r, cv::Scalar color, int length, int thickness) {
    cv::line(img, cv::Point(r.x, r.y), cv::Point(r.x + length, r.y), color, thickness);
    cv::line(img, cv::Point(r.x, r.y), cv::Point(r.x, r.y + length), color, thickness);
    cv::line(img, cv::Point(r.x + r.width, r.y), cv::Point(r.x + r.width - length, r.y), color, thickness);
    cv::line(img, cv::Point(r.x + r.width, r.y), cv::Point(r.x + r.width, r.y + length), color, thickness);
    cv::line(img, cv::Point(r.x, r.y + r.height), cv::Point(r.x + length, r.y + r.height), color, thickness);
    cv::line(img, cv::Point(r.x, r.y + r.height), cv::Point(r.x, r.y + r.height - length), color, thickness);
    cv::line(img, cv::Point(r.x + r.width, r.y + r.height), cv::Point(r.x + r.width - length, r.y + r.height), color, thickness);
    cv::line(img, cv::Point(r.x + r.width, r.y + r.height), cv::Point(r.x + r.width, r.y + r.height - length), color, thickness);
}
void draw_sleep_aod(cv::Mat& frame, bool is_online) {
    // 1. Tạo nền đen
    frame = cv::Mat::zeros(LCD_HEIGHT, LCD_WIDTH, CV_8UC3);

    // 2. Lấy giờ hiện tại
    time_t now = time(nullptr);
    struct tm tstruct;
    char time_buf[6];
    tstruct = *localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%H:%M", &tstruct);

    // 3. Các thông số chung
    int center_x = LCD_WIDTH / 2;  // Tim màn hình (thường là 160)
    int center_y = LCD_HEIGHT / 2; // Tim màn hình (thường là 120)
    int baseline = 0;

    // ---------------------------------------------------------
    // VẼ GIỜ (TIME) - CĂN GIỮA
    // ---------------------------------------------------------
    double timeScale = 2.5; 
    int timeThick = 3;
    
    // Đo kích thước thật của chữ giờ
    cv::Size timeSize = cv::getTextSize(time_buf, cv::FONT_HERSHEY_SIMPLEX, timeScale, timeThick, &baseline);
    
    // Tính toán tọa độ X để chữ nằm giữa
    int timeX = (LCD_WIDTH - timeSize.width) / 2;
    // Tọa độ Y: Đặt cao hơn tâm một chút để nhường chỗ cho phần bên dưới
    int timeY = center_y + 10; 

    cv::putText(frame, time_buf, cv::Point(timeX, timeY), 
                cv::FONT_HERSHEY_SIMPLEX, timeScale, cv::Scalar(90, 90, 90), timeThick);

    // ---------------------------------------------------------
    // VẼ CHẤM TRẠNG THÁI (DOT) - CĂN GIỮA
    // ---------------------------------------------------------
    cv::Scalar net_color = is_online ? cv::Scalar(0, 150, 0) : cv::Scalar(150, 0, 0);
    int dotY = timeY + 25; // Nằm dưới giờ 25 pixel
    
    // Vẽ ngay tại trục giữa (center_x)
    cv::circle(frame, cv::Point(center_x, dotY), 6, net_color, -1);

    // ---------------------------------------------------------
    // VẼ CHỮ "SLEEP MODE" - CĂN GIỮA
    // ---------------------------------------------------------
    std::string label = "SLEEP MODE";
    double labelScale = 0.55;
    int labelThick = 1;

    // Đo kích thước chữ
    cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, labelScale, labelThick, &baseline);
    
    // Tính X
    int labelX = (LCD_WIDTH - labelSize.width) / 2;
    int labelY = dotY + 25; // Nằm dưới chấm tròn 25 pixel

    cv::putText(frame, label, cv::Point(labelX, labelY), 
                cv::FONT_HERSHEY_SIMPLEX, labelScale, cv::Scalar(60, 60, 60), labelThick);
}


// Hàm tạo hiệu ứng Fading (Sáng dần/Tối dần)
// start_duty: 0-100 (Độ sáng bắt đầu)
// end_duty: 0-100 (Độ sáng kết thúc)
// duration_ms: Thời gian thực hiện hiệu ứng (ms)
// Hàm tạo hiệu ứng Fading (Sáng dần/Tối dần)
// start_duty: 0-100
// end_duty: 0-100
// duration_ms: Thời gian thực hiện (ms)
void lcd_backlight_fade(int start_duty, int end_duty, int duration_ms) {
    if (start_duty == end_duty) return;
    
    int steps = abs(end_duty - start_duty);
    int time_per_step = duration_ms / steps;
    
    // Mỗi chu kỳ PWM: 100 ticks * 20us = 2ms
    int loops_per_step = std::max(1, time_per_step / 2);
    
    int direction = (start_duty < end_duty) ? 1 : -1;
    
    for (int duty = start_duty; duty != end_duty; duty += direction) {
        for (int cycle = 0; cycle < loops_per_step; cycle++) {
            if (duty > 0) {
                bcm2835_gpio_write(PIN_LED, HIGH);
                bcm2835_delayMicroseconds(duty * 20);
            }
            if (duty < 100) {
                bcm2835_gpio_write(PIN_LED, LOW);
                bcm2835_delayMicroseconds((100 - duty) * 20);
            }
        }
    }
    
    // Đảm bảo trạng thái cuối
    bcm2835_gpio_write(PIN_LED, (end_duty > 0) ? HIGH : LOW);
}

// =============================================================
// TASK IMPLEMENTATIONS
// =============================================================
void* task_btn_register(void* arg) {
    // Cấu hình GPIO
    bcm2835_gpio_fsel(PIN_BTN_REG, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PIN_BTN_REG, BCM2835_GPIO_PUD_UP);
    printf("[BTN_REG] Started for NAVIGATION (Next/Scroll)\n");

    while (g_running) {
        // Phát hiện nhấn nút (Active LOW)
        if (bcm2835_gpio_lev(PIN_BTN_REG) == LOW) {
            
            // Chờ 200ms để phân biệt Nhấn nhả (Click) hay Nhấn giữ (Hold)
            bcm2835_delay(200);

            if (bcm2835_gpio_lev(PIN_BTN_REG) == HIGH) {
                // ------------------------------------------------
                // TRƯỜNG HỢP 1: NHẤN NHẢ (SHORT CLICK) -> NEXT
                // ------------------------------------------------
                if (g_wifi_mode_active) {
                    g_btn_cmd = CMD_SINGLE; // Di chuyển 1 bước
                    // printf("[BTN] Next char\n");
                } 
                else {
                    // Logic cũ khi không ở chế độ Wifi (Toggle Register Mode)
                    g_register_mode = !g_register_mode;
                    printf("[BTN] Register Mode: %s\n", g_register_mode ? "ON" : "OFF");
                }
            } 
            else {
                // ------------------------------------------------
                // TRƯỜNG HỢP 2: NHẤN GIỮ (HOLD) -> FAST SCROLL
                // ------------------------------------------------
                while (bcm2835_gpio_lev(PIN_BTN_REG) == LOW && g_running) {
                    if (g_wifi_mode_active) {
                        g_btn_cmd = CMD_SCROLL; // Gửi lệnh cuộn liên tục
                        bcm2835_delay(150);     // Tốc độ cuộn: 150ms/ký tự (Chỉnh số này để nhanh/chậm)
                    } else {
                        // Nếu không phải Wifi mode, không làm gì hoặc chờ nhả
                        bcm2835_delay(100);
                    }
                }
            }
            
            bcm2835_delay(200); // Debounce sau khi thả nút
        }
        bcm2835_delay(20); // Polling delay
    }
    return NULL;
}
void* task_btn_power(void* arg) {
    // Cấu hình GPIO
    bcm2835_gpio_fsel(PIN_BTN_SLEEP, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PIN_BTN_SLEEP, BCM2835_GPIO_PUD_UP);
    
    // Bật đèn nền lúc khởi động
    bcm2835_gpio_write(PIN_LED, HIGH); 
    printf("[BTN_PWR] Started: Single(Sleep/Select) | Double(Reset/Rescan) | Hold(Shutdown/Connect)\n");

    while (g_running) {
        if (bcm2835_gpio_lev(PIN_BTN_SLEEP) == LOW) {
            
            // ---------------------------------------------------------
            // BƯỚC 1: ĐO THỜI GIAN NHẤN LẦN 1
            // ---------------------------------------------------------
            int hold_time = 0;
            while (bcm2835_gpio_lev(PIN_BTN_SLEEP) == LOW) {
                bcm2835_delay(10);
                hold_time += 10;
                // Nếu giữ quá lâu (>3s) thì break luôn để xử lý Shutdown/Connect ngay
                if (hold_time >= 3000) break;
            }

            // ---------------------------------------------------------
            // BƯỚC 2: PHÂN LOẠI HÀNH ĐỘNG
            // ---------------------------------------------------------
            
            // === CASE 1: LONG PRESS (> 3000ms) ===
            if (hold_time >= 3000) {
                // Đợi nhả nút để tránh lặp lệnh
                while(bcm2835_gpio_lev(PIN_BTN_SLEEP) == LOW) bcm2835_delay(50);

                if (g_wifi_mode_active) {
                    printf("[BTN] SUPER LONG -> CONNECT COMMAND!\n");
                    g_btn_cmd = CMD_SUPER_LONG; 
                } else {
                    printf("[PWR] SHUTDOWN TRIGGERED...\n");
                    { std::lock_guard<std::mutex> lock(mtx_ai); shared_result.message = "SHUTTING DOWN..."; }
                    lcd_backlight_fade(100, 0, 1000);
                    g_running = false; 
                    system("sudo poweroff");
                }
            }
            // === CASE 2: SHORT PRESS (< 3000ms) -> CHECK DOUBLE CLICK ===
            else if (hold_time > 50) {
                
                // Mẹo: Chờ 250ms xem có cú nhấn thứ 2 không?
                int wait_double = 0;
                bool is_double_click = false;

                while (wait_double < 250) {
                    bcm2835_delay(10);
                    wait_double += 10;
                    // Nếu phát hiện nhấn lần nữa trong khoảng thời gian chờ
                    if (bcm2835_gpio_lev(PIN_BTN_SLEEP) == LOW) {
                        is_double_click = true;
                        // Chờ nhả nút lần 2
                        while(bcm2835_gpio_lev(PIN_BTN_SLEEP) == LOW) bcm2835_delay(10);
                        break;
                    }
                }

                if (is_double_click) {
                    // >>> XỬ LÝ DOUBLE CLICK TẠI ĐÂY <<<
                    printf("[PWR] DOUBLE CLICK DETECTED!\n");
                    
                    if (g_wifi_mode_active) {
                        // Trong chế độ Wifi: Double click để Rescan hoặc Back
                        g_btn_cmd = CMD_DOUBLE; 
                    } else {
                        // Trong chế độ thường: Double click để Reboot (Ví dụ)
                        printf("[PWR] Rebooting system...\n");
                        { std::lock_guard<std::mutex> lock(mtx_ai); shared_result.message = "REBOOTING..."; }
                         lcd_backlight_fade(100, 0, 500);
                        system("sudo reboot");
                    }
                } 
                else {
                    // >>> XỬ LÝ SINGLE CLICK TẠI ĐÂY <<<
                    if (g_wifi_mode_active) {
                        printf("[BTN] CLICK -> SELECT CHAR\n");
                        g_btn_cmd = CMD_WIFI_SELECT;
                    } else {
                        // Sleep / Wake
                        g_is_sleeping = !g_is_sleeping;
                        printf("[PWR] Sleep Mode: %s\n", g_is_sleeping ? "ON" : "OFF");
                        if (g_is_sleeping) lcd_backlight_fade(100, 15, 500);
                        else lcd_backlight_fade(15, 100, 500);
                    }
                }
            }

            bcm2835_delay(300); // Debounce an toàn sau cùng
        }
        bcm2835_delay(20); // Polling nhanh hơn chút để bắt click nhạy hơn
    }
    return NULL;
}
void* task_camera(void* arg) {
    cv::VideoCapture cap(0, cv::CAP_V4L2);
    cap.set(cv::CAP_PROP_FRAME_WIDTH, LCD_WIDTH); cap.set(cv::CAP_PROP_FRAME_HEIGHT, LCD_HEIGHT); cap.set(cv::CAP_PROP_FPS, 30);
    cv::Mat frame;
    while(g_running) {
        if (g_wifi_mode_active || g_is_sleeping) { usleep(500000); continue; }
        cap >> frame;
        if (frame.empty()) continue;
        cv::resize(frame, frame, cv::Size(LCD_WIDTH, LCD_HEIGHT));
        queue_push(&q_display, frame.clone());
        { std::lock_guard<std::mutex> lock(mtx_ai); shared_ai_frame = frame.clone(); new_frame_for_ai = true; }
        usleep(1000);
    }
    return NULL;
}

// --- TASK AI (ĐÃ CẬP NHẬT LOGIC DELAY & MANUAL EMBEDDING) ---

void* task_ai(void* arg) {
    FaceNet faceNet;
    Log("AI", "Loading FaceNet...");
    try { faceNet.loadModel("MobileFaceNet.onnx"); } catch(...) { Log("AI", "ERR: No FaceNet Model!"); }
    
    cv::Ptr<cv::FaceDetectorYN> detector;
    try {
        detector = cv::FaceDetectorYN::create("face_detection_yunet_2023mar.onnx", "", cv::Size(LCD_WIDTH, LCD_HEIGHT), 0.9f, 0.3f, 5000);
    } catch (const cv::Exception& e) { Log("AI", "YuNet Error: %s", e.what()); }

    g_ram_users = Network_LoadDatabase(); 
    Log("AI", "Database Loaded: %zu users", g_ram_users.size());

    cv::Mat process_frame;
    std::vector<cv::Mat> reg_samples;
    
    // [FIX] Khởi tạo thời gian ban đầu
    auto last_reg_capture = std::chrono::steady_clock::now();

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
        
        // ----------------------- DETECT -----------------------
        std::vector<cv::Rect> faces_rect;
        if (detector) {
            cv::Mat faces_yunet;
            detector->setInputSize(process_frame.size());
            detector->detect(process_frame, faces_yunet);

            for (int i = 0; i < faces_yunet.rows; i++) {
                if (faces_yunet.at<float>(i, 14) > 0.85f) {
                    int x = std::max(0, (int)faces_yunet.at<float>(i, 0));
                    int y = std::max(0, (int)faces_yunet.at<float>(i, 1));
                    int w = std::min((int)faces_yunet.at<float>(i, 2), process_frame.cols - x);
                    int h = std::min((int)faces_yunet.at<float>(i, 3), process_frame.rows - y);
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

            // ----------------------- REGISTER LOGIC (MULTI-POSE) -----------------------
            if (g_register_mode) {
                res.color = cv::Scalar(255, 0, 0);
                int step = reg_samples.size();
                
                if (step < 5) {
                    res.message = "REGISTERING...";
                    res.sub_message = REG_STEPS_MSG[step];
                }
                
                float q = faceNet.checkQuality(face_roi);
                
                // Hiển thị chất lượng khuôn mặt
                char quality_buf[32];
                sprintf(quality_buf, "Quality: %.0f%%", q * 100);
                res.sub_message += std::string(" | ") + quality_buf;
                
                auto now = std::chrono::steady_clock::now();
                int ms_since_last = std::chrono::duration_cast<std::chrono::milliseconds>
                    (now - last_reg_capture).count();
                
                // Capture nếu chất lượng tốt và đã đủ thời gian
                if (q > 0.1f && step < 5 && ms_since_last > 1500) {
                    reg_samples.push_back(face_roi.clone());
                    last_reg_capture = now;
                    res.color = cv::Scalar(0, 255, 0); // Flash green
                    Log("Reg", "Captured pose %d/5 (Quality: %.2f)", step + 1, q);
                }
                
                // Khi đủ 5 mẫu
                if (reg_samples.size() >= 5) {
                    res.message = "PROCESSING...";
                    res.sub_message = "Extracting features...";
                    
                    UserInfo u;
                    u.id = std::to_string(time(nullptr));
                    u.name = "User_" + u.id.substr(u.id.length() - 4);
                    u.embeddings.clear();
                    
                    for (size_t i = 0; i < reg_samples.size(); i++) {
                        cv::Mat raw_emb = faceNet.getEmbedding(reg_samples[i]);
                        if (!raw_emb.empty()) {
                            std::vector<float> vec;
                            if (raw_emb.isContinuous()) {
                                vec.assign((float*)raw_emb.datastart, 
                                          (float*)raw_emb.dataend);
                            } else {
                                for (int c = 0; c < raw_emb.cols; c++) {
                                    vec.push_back(raw_emb.at<float>(0, c));
                                }
                            }
                            if (!vec.empty()) {
                                u.embeddings.push_back(vec);
                            }
                        }
                    }
                    
                    if (!u.embeddings.empty() && Network_SaveUser(u)) {
                        {
                            std::lock_guard<std::mutex> lock(mtx_users);
                            g_ram_users.push_back(u);
                        }
                        res.message = "SUCCESS!";
                        res.sub_message = "Saved: " + u.name;
                        res.color = cv::Scalar(0, 255, 0);
                        Log("Reg", "Saved user with %zu embeddings", u.embeddings.size());
                    } else {
                        res.message = "SAVE FAILED";
                        res.sub_message = "No valid features";
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
                        float global_max_sim = 0;
                        std::string best_name = "Unknown";
                        std::string best_id = "";
                        
                        for(auto& u : g_ram_users) {
                            float user_max_sim = 0;
                            for (auto& stored_vec : u.embeddings) {
                                if (stored_vec.size() != (size_t)cur_emb.cols) continue;
                                cv::Mat db_emb(1, stored_vec.size(), CV_32F, (void*)stored_vec.data());
                                
                                double dot = cur_emb.dot(db_emb);
                                double n1 = cv::norm(cur_emb);
                                double n2 = cv::norm(db_emb);
                                float sim = (n1>0 && n2>0) ? dot/(n1*n2) : 0;
                                if (sim > user_max_sim) user_max_sim = sim;
                            }

                            if (user_max_sim > global_max_sim) { 
                                global_max_sim = user_max_sim; 
                                best_name = u.name; 
                                best_id = u.id; 
                            }
                        }
                        
                        char sim_str[16];
                        sprintf(sim_str, "(%.0f%%)", global_max_sim * 100);

                        if (global_max_sim > 0.90f) {
                            res.message = best_name;
                            res.sub_message = std::string("Match ") + sim_str;
                            res.color = cv::Scalar(0, 255, 0); 
                            Network_SendLog(best_id, best_name);
                        } else {
                            res.message = "UNKNOWN";
                            res.sub_message = std::string("Low ") + sim_str;
                            res.color = cv::Scalar(0, 0, 255); 
                        }
                    }
                }
            }
        } else {
             res.message = g_register_mode ? "SHOW FACE" : "SCANNING...";
             if (g_register_mode && !reg_samples.empty()) res.sub_message = "Keep Face in Frame!";
             else res.sub_message = "";
        }

        { std::lock_guard<std::mutex> lock(mtx_ai); shared_result = res; }
        usleep(5000);
    }
    return NULL;
}

// --- Helper chuyển đổi Mat sang buffer SPI ---
void send_mat_to_lcd(const cv::Mat& frame, uint8_t* buffer) {
    if (frame.empty()) return;
    int idx = 0;
    const uint8_t* p = frame.data;
    int total_pixels = LCD_WIDTH * LCD_HEIGHT;
    
    for(int i=0; i<total_pixels; i++) {
        uint8_t b = *p++; uint8_t g = *p++; uint8_t r = *p++;
        uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        buffer[idx++] = (c >> 8); buffer[idx++] = (c & 0xFF);
    }
    bcm2835_gpio_write(PIN_DC, LOW); 
    lcd_set_window(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1);
    bcm2835_gpio_write(PIN_DC, HIGH); 
    bcm2835_spi_transfern((char*)buffer, LCD_WIDTH*LCD_HEIGHT*2);
}

void* task_lcd(void* arg) {
    uint8_t* spi_buffer = (uint8_t*)malloc(LCD_WIDTH * LCD_HEIGHT * 2);
    
    // ---------------------------------------------------------
    // 1. LOAD BOOT LOGO (Sửa lỗi mất logo)
    // ---------------------------------------------------------
    Log("LCD", "Loading Boot Logo...");
    cv::Mat logo_img = cv::imread("boot_logo.jpg"); // Đọc file ảnh
    
    // Nếu không có ảnh hoặc lỗi, tạo nền đen
    if (logo_img.empty()) {
        Log("LCD", "WARN: boot_logo.jpg not found! Using black screen.");
        logo_img = cv::Mat::zeros(LCD_HEIGHT, LCD_WIDTH, CV_8UC3);
    } else {
        // Resize logo cho vừa màn hình
        cv::resize(logo_img, logo_img, cv::Size(LCD_WIDTH, LCD_HEIGHT));
    }

    // ---------------------------------------------------------
    // 2. HIỂN THỊ MÀN HÌNH KHỞI ĐỘNG
    // ---------------------------------------------------------
    cv::Mat boot_screen = logo_img.clone();    
    send_mat_to_lcd(boot_screen, spi_buffer);
    sleep(2); // Giữ màn hình logo 2 giây

    // ---------------------------------------------------------
    // 3. VÒNG LẶP CHÍNH
    // ---------------------------------------------------------
    cv::Mat frame;
    cv::Mat display_frame;
    AIResult ai_state;
    
    while(g_running) {
        
        if (g_wifi_mode_active) {
            if (queue_pop(&q_display, &frame)) {
                send_mat_to_lcd(frame, spi_buffer);
            }
            usleep(20000); // Nghỉ nhẹ để không chiếm CPU
            continue;      // <--- QUAN TRỌNG: Bỏ qua hết code bên dưới
        }
        // Lấy trạng thái AI (để check Shutdown message)
        { std::lock_guard<std::mutex> lock(mtx_ai); ai_state = shared_result; }

        // --- CASE A: SHUTDOWN (Ưu tiên cao nhất) ---
        if (ai_state.message == "SHUTTING DOWN...") {
            cv::putText(display_frame, "SHUTTING DOWN...", cv::Point(50, LCD_HEIGHT - 30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 0, 0), 2);
            send_mat_to_lcd(display_frame, spi_buffer);
            usleep(100000); // Giảm tải
            continue;
        }

        // --- CASE B: SLEEP MODE ---
        if (g_is_sleeping) {
          draw_sleep_aod(display_frame, g_is_online);
          send_mat_to_lcd(display_frame, spi_buffer);
          sleep(1);   // 1 FPS – tiết kiệm CPU
          continue;
        }

        // --- CASE C: NORMAL CAMERA MODE ---
        if (queue_pop(&q_display, &frame)) {
            display_frame = frame; // Dùng frame từ camera
        } else {
            // Nếu không có frame camera, dùng logo tạm
            display_frame = logo_img.clone();
        }

        // [Vẽ giao diện HUD như cũ]
        draw_transparent_bar(display_frame, 0, 35, cv::Scalar(0,0,0), 0.6);
        
        time_t now = time(0);
        struct tm tstruct;
        char time_buf[10];
        tstruct = *localtime(&now);
        strftime(time_buf, sizeof(time_buf), "%H:%M", &tstruct);
        cv::putText(display_frame, time_buf, cv::Point(265, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(220,220,220), 1);

        // VẼ CHẤM TRẠNG THÁI (Cho Normal Mode)
        if (g_is_online) cv::circle(display_frame, cv::Point(245, 18), 6, cv::Scalar(0, 255, 0), -1);
        else cv::circle(display_frame, cv::Point(245, 18), 6, cv::Scalar(0, 0, 255), -1);

        if (g_register_mode) {
             cv::putText(display_frame, "MODE: REGISTER", cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0,0,255), 2);
             cv::circle(display_frame, cv::Point(170, 20), 6, cv::Scalar(0,0,255), -1); 
        } else {
             cv::putText(display_frame, "ACCESS CONTROL", cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0,255,0), 1);
        }

        draw_transparent_bar(display_frame, LCD_HEIGHT-45, 45, cv::Scalar(0,0,0), 0.7);

        if (ai_state.has_detection) {
             for(auto r : ai_state.faces) draw_corner_rect(display_frame, r, ai_state.color, 20, 2);
             cv::putText(display_frame, ai_state.message, cv::Point(10, LCD_HEIGHT-22), cv::FONT_HERSHEY_SIMPLEX, 0.7, ai_state.color, 2);
             if (!ai_state.sub_message.empty()) {
                 cv::putText(display_frame, ai_state.sub_message, cv::Point(10, LCD_HEIGHT-6), cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(200,200,200), 1);
             }
        } else {
             cv::putText(display_frame, ai_state.message, cv::Point(10, LCD_HEIGHT-15), cv::FONT_HERSHEY_SIMPLEX, 0.65, cv::Scalar(200,200,200), 1);
        }

        // Gửi ra LCD
        send_mat_to_lcd(display_frame, spi_buffer);
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


void* task_config_wifi(void* arg) {
    Log("WIFI", "Entering Config Mode...");
    
    if (g_is_sleeping) {
        Log("WIFI", "System is sleeping -> Force WAKE UP for Config!");
        g_is_sleeping = !g_is_sleeping;
        lcd_backlight_fade(15, 100, 500);
    }
    
    g_wifi_mode_active = true; // Báo cho hệ thống biết đang ở Mode Config
    
    // Reset các biến trạng thái
    enum State { SCAN, SELECT, PASS, CONNECTING, SUCCESS, FAIL };
    State state = SCAN;
    
    std::vector<WifiInfo> networks;
    int char_idx = 0;
    int cursor = 0;
    std::string selected_ssid = "";
    std::string password = "";
    bool finished = false;

    // Biến đếm để không check mạng quá liên tục
    int auto_check_counter = 0;

    while (!finished && g_running) {
        
        // ============================================================
        // LOGIC TỰ THOÁT NẾU ĐÃ CÓ MẠNG (AUTO EXIT)
        // ============================================================
        auto_check_counter++;
        if (auto_check_counter > 40) {
            auto_check_counter = 0;
            if (Wifi_IsConnected()) {
                if (Network_CheckInternet()) {
                    Log("WIFI", "Detected Internet! Auto-exiting...");
                    cv::Mat ui = cv::Mat::zeros(LCD_HEIGHT, LCD_WIDTH, CV_8UC3);
                    cv::putText(ui, "AUTO RECONNECTED!", cv::Point(30, 120), 1, 1.0, cv::Scalar(0, 255, 0), 2);
                    queue_push(&q_display, ui.clone());
                    sleep(2);
                    finished = true;
                    continue; 
                }
            }
        }

        // ============================================================
        // XỬ LÝ GIAO DIỆN & INPUT
        // ============================================================
        cv::Mat ui = cv::Mat::zeros(LCD_HEIGHT, LCD_WIDTH, CV_8UC3);
        int cmd = g_btn_cmd.exchange(0); // Lấy lệnh và reset về 0

        // Xử lý Double Click để Rescan (nếu cần)
        if (cmd == CMD_DOUBLE) {
            state = SCAN;
            char_idx = 0;
            cursor = 0;
            password = "";
            Log("WIFI", "Double click -> Force Re-SCAN");
        }

        switch (state) {
            case SCAN:
                cv::putText(ui, "SCANNING...", cv::Point(80, 120), 1, 1.2, cv::Scalar(0, 255, 255), 2);
                queue_push(&q_display, ui.clone()); 
                networks = Wifi_Scan();
                state = networks.empty() ? SCAN : SELECT;
                break;

            case SELECT:
                cv::putText(ui, "SELECT WIFI:", cv::Point(10, 30), 1, 1.0, cv::Scalar(0, 255, 0), 2);
                for (int i=0; i<(int)networks.size() && i<5; i++) {
                    cv::Scalar col = (i==cursor) ? cv::Scalar(0,255,255) : cv::Scalar(150,150,150);
                    cv::putText(ui, (i==cursor?"> ":"  ") + networks[i].ssid, cv::Point(10, 70 + i*30), 1, 0.8, col, 1);
                }
                
                // Logic chọn Wifi: Nút REG di chuyển, Nút SLEEP chọn
                if (cmd == CMD_SINGLE || cmd == CMD_SCROLL) cursor = (cursor + 1) % networks.size();
                if (cmd == CMD_WIFI_SELECT) { selected_ssid = networks[cursor].ssid; state = PASS; password = ""; }
                break;
            
            case PASS:
            {
                // --- 1. DISPLAY UI ---
                cv::putText(ui, "SSID: " + selected_ssid, cv::Point(10, 20), 1, 0.8, cv::Scalar(200,200,200), 1);
                
                // [UPDATE UI] Hướng dẫn nút bấm mới
                cv::putText(ui, "REG:Move | PWR:Select", cv::Point(10, 210), 1, 0.6, cv::Scalar(150,150,150), 1);
                
                // Ô nhập liệu
                cv::rectangle(ui, cv::Rect(10, 30, 300, 35), cv::Scalar(50,50,50), -1);
                cv::putText(ui, password, cv::Point(15, 58), 1, 1.2, cv::Scalar(255,255,255), 1);
            
                // Ký tự hiện tại (Carousel)
                char currentChar = CHAR_SET_WIFI[char_idx];
                std::string displayChar(1, currentChar);
                cv::Scalar charColor = cv::Scalar(0, 255, 255); 
            
                if (currentChar == '<') {
                    displayChar = "DEL"; 
                    charColor = cv::Scalar(0, 0, 255); 
                }
            
                cv::putText(ui, "[" + displayChar + "]", cv::Point(110, 140), 1, 3.0, charColor, 3);
            
                if (password.length() >= 8) {
                    cv::putText(ui, "HOLD PWR 3s TO CONNECT", cv::Point(20, 180), 1, 0.7, cv::Scalar(0,255,0), 2);
                }
            
                // --- 2. INPUT LOGIC (FIXED) ---
                
                // [FIX 1] DI CHUYỂN & CUỘN (Nút Register)
                // Chấp nhận cả CMD_SINGLE (nhấn nhả) và CMD_SCROLL (nhấn giữ từ task_register mới)
                if (cmd == CMD_SINGLE || cmd == CMD_SCROLL) { 
                    char_idx++; 
                    if(char_idx >= (int)CHAR_SET_WIFI.length()) char_idx = 0; 
                }
                
                // [FIX 2] CHỌN / XÓA (Nút Sleep/Power nhấn ngắn)
                // Thay thế CMD_LONG (2) cũ bằng CMD_WIFI_SELECT (5)
                if (cmd == CMD_WIFI_SELECT) { 
                    if (currentChar == '<') {
                        if (!password.empty()) password.pop_back();
                    } else {
                        password += currentChar;
                    }
                    
                    // Feedback visual: Nháy màu xanh một chút để báo đã chọn
                    cv::circle(ui, cv::Point(280, 210), 8, cv::Scalar(0, 255, 0), -1);
                }
                
                // [FIX 3] KẾT NỐI (Nút Sleep/Power nhấn giữ > 3s)
                if (cmd == CMD_SUPER_LONG) {
                    if (password.length() >= 8) {
                         state = CONNECTING;
                    } else {
                         cv::putText(ui, "MIN 8 CHARS!", cv::Point(160, 180), 1, 0.7, cv::Scalar(0,0,255), 2);
                    }
                }
                
                break;
            }

            case CONNECTING:
                cv::putText(ui, "CONNECTING...", cv::Point(70, 120), 1, 1.2, cv::Scalar(0, 255, 255), 2);
                queue_push(&q_display, ui.clone());
                if (Wifi_Connect(selected_ssid, password)) state = SUCCESS;
                else state = FAIL;
                break;

            case SUCCESS:
                cv::putText(ui, "CONNECTED!", cv::Point(50, 120), 1, 1.2, cv::Scalar(0, 255, 0), 2);
                queue_push(&q_display, ui.clone());
                sleep(2);
                finished = true; 
                break;

            case FAIL:
                cv::putText(ui, "FAILED!", cv::Point(90, 120), 1, 1.2, cv::Scalar(0, 0, 255), 2);
                queue_push(&q_display, ui.clone());
                sleep(2);
                state = SCAN;
                break;
        }

        if (!finished) {
            queue_push(&q_display, ui.clone());
            usleep(50000); 
        }
    }

    g_wifi_mode_active = false; 
    return NULL;
}
void* task_network_monitor(void* arg) {
    printf("[NET] Monitor Started\n");
    int fail_count = 0;
    sleep(5); 

    while (g_running) {
        if (!g_wifi_mode_active) {
            
            // Kiểm tra mạng
            bool has_internet = Network_CheckInternet();
            
            // --- [MỚI] CẬP NHẬT BIẾN TOÀN CỤC ---
            g_is_online = has_internet; 
            // ------------------------------------

            if (has_internet) {
                fail_count = 0;
            } else {
                fail_count++;
                if (fail_count >= 6) {
                    printf("[NET] Lost Connection! Entering WiFi Config Mode...\n");
                    task_config_wifi(NULL); 
                    fail_count = 0; 
                    printf("[NET] Returned to Normal Mode\n");
                }
            }
        }
        sleep(5);
    }
    return NULL;
}
