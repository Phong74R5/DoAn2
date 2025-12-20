#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <bcm2835.h>

#include "tasks.h"
#include "config.h"
#include "lcd_driver.h"
#include "queue_helper.h"
#include "facenet.h"
#include "network_helper.h"

// ==========================================
// SHARED DATA STRUCTURES
// ==========================================

struct AIResult {
    std::vector<cv::Rect> faces;
    std::string message;
    cv::Scalar color;
    bool has_detection;
};

struct DetectionFilter {
    std::vector<float> recent_similarities;
    const int WINDOW_SIZE = 7;
    
    bool isStable(float new_similarity) {
        recent_similarities.push_back(new_similarity);
        if (recent_similarities.size() > (size_t)WINDOW_SIZE) {
            recent_similarities.erase(recent_similarities.begin());
        }
        
        if (recent_similarities.size() < 5) return false;
        
        float mean = 0;
        for (float s : recent_similarities) mean += s;
        mean /= recent_similarities.size();
        
        float variance = 0;
        for (float s : recent_similarities) {
            variance += (s - mean) * (s - mean);
        }
        variance /= recent_similarities.size();
        float stddev = sqrt(variance);
        
        return stddev < 0.05f;
    }
    
    float getAverage() {
        if (recent_similarities.empty()) return 0.0f;
        float sum = 0;
        for (float s : recent_similarities) sum += s;
        return sum / recent_similarities.size();
    }
    
    void clear() {
        recent_similarities.clear();
    }
};

// ==========================================
// GLOBAL VARIABLES
// ==========================================

// AI Processing
std::mutex mtx_ai;
std::condition_variable cv_ai;
cv::Mat shared_ai_frame;
bool new_frame_for_ai = false;
AIResult shared_result;

// FaceNet
FaceNet faceNet;
DetectionFilter detection_filter;

// Network Mode
SafeQueue<NetworkJob> q_network;
std::atomic<bool> g_is_register_requested(false);

// Sleep Mode
std::atomic<bool> g_is_sleeping(false);
std::mutex mtx_sleep;

// Trigger load user
std::atomic<bool> g_is_loading_users(false);


// ==========================================
// HELPER FUNCTIONS
// ==========================================

bool isFaceAligned(const cv::Rect& face, const cv::Mat& frame) {
    float aspect_ratio = (float)face.width / face.height;
    if (aspect_ratio < 0.75f || aspect_ratio > 1.25f) return false;
    
    int margin = 30;
    if (face.x < margin || face.y < margin ||
        face.x + face.width > frame.cols - margin ||
        face.y + face.height > frame.rows - margin) {
        return false;
    }
    
    if (face.width < 80 || face.height < 80) return false;
    
    return true;
}

cv::Rect selectBestFace(const std::vector<cv::Rect>& faces, 
                         const cv::Mat& frame,
                         FaceNet& faceNet) {
    if (faces.empty()) return cv::Rect();
    
    float best_score = -1.0f;
    cv::Rect best_face;
    
    for (const auto& face : faces) {
        if (!isFaceAligned(face, frame)) continue;
        
        cv::Mat face_roi = frame(face);
        float quality = faceNet.checkQuality(face_roi);
        float size_score = std::min(1.0f, (face.width * face.height) / 12000.0f);
        
        float total_score = quality * 0.8f + size_score * 0.2f;
        
        if (total_score > best_score) {
            best_score = total_score;
            best_face = face;
        }
    }
    
    return best_face;
}

// ==========================================
// TASK 1: CAMERA
// ==========================================

void* task_camera(void* arg) {
    cv::VideoCapture cap(0, cv::CAP_V4L2);
    cap.set(cv::CAP_PROP_FRAME_WIDTH, LCD_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, LCD_HEIGHT);
    cap.set(cv::CAP_PROP_FPS, 30);


    if (!cap.isOpened()) {
        printf("[Task Cam] Error: Cannot open camera!\n");
        return NULL;
    }

    cv::Mat cam_frame, frame;
    printf("[Task Cam] Started successfully\n");

    while(g_running) {
        // Skip khi đang sleep
        if (g_is_sleeping) {
            usleep(100000); // Sleep 100ms
            continue;
        }

        cap >> cam_frame;
        
        if (cam_frame.empty()) {
            usleep(10000);
            continue;
        }

        if (cam_frame.cols != LCD_WIDTH || cam_frame.rows != LCD_HEIGHT) {
            cv::resize(cam_frame, frame, cv::Size(LCD_WIDTH, LCD_HEIGHT));
        } else {
            frame = cam_frame;
        }

        // Push to display queue
        queue_push(&q_display, frame.clone());

        // Update AI frame
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
// TASK 2: BUTTON (BCM2835)
// ==========================================

void* task_button(void* arg) {
    // Sửa PIN_BUTTON thành PIN_BTN_REG
    printf("[Task Button] Init BCM GPIO (REG) ...\n");
    
    bcm2835_gpio_fsel(PIN_BTN_REG, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PIN_BTN_REG, BCM2835_GPIO_PUD_UP); // Kéo lên nguồn (Input thường là 1)

    while(g_running) {
        if (g_is_sleeping) { bcm2835_delay(100); continue; }

        // Kiểm tra nút bấm (LOW = Đã bấm vì đang nối đất)
        if (bcm2835_gpio_lev(PIN_BTN_REG) == LOW) {
            bcm2835_delay(50); // Chống rung (Debounce)
            
            if (bcm2835_gpio_lev(PIN_BTN_REG) == LOW) {
                if (!g_is_register_requested) {
                    printf("[Button] REGISTER PRESSED!\n");
                    g_is_register_requested = true;
                    
                    // Chờ nhả nút
                    while (bcm2835_gpio_lev(PIN_BTN_REG) == LOW) {
                        bcm2835_delay(10);
                    }
                }
            }
        }
        bcm2835_delay(20);
    }
    return NULL;
}

// ==========================================
// TASK 2.5: SLEEP BUTTON (GPIO 18)
// ==========================================

void* task_sleep_button(void* arg) {
    printf("[Task Sleep] Init BCM GPIO 18...\n");
    
    bcm2835_gpio_fsel(PIN_BTN_SLEEP, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_set_pud(PIN_BTN_SLEEP, BCM2835_GPIO_PUD_UP);

    while(g_running) {
        uint8_t value = bcm2835_gpio_lev(PIN_BTN_SLEEP);

        if (value == LOW) {
            bcm2835_delay(50); // Debounce
            
            if (bcm2835_gpio_lev(PIN_BTN_SLEEP) == LOW) {
                // Toggle sleep mode
                g_is_sleeping = !g_is_sleeping;
                
                if (g_is_sleeping) {
                    printf("[Sleep] Entering SLEEP mode...\n");
                    
                    // Turn off LCD
                    bcm2835_gpio_write(PIN_LED, LOW);
//                    lcd_write_cmd(0x28); // Display OFF

                    
                    printf("[Sleep] LCD OFF | Camera paused | AI paused\n");
                } else {
                    printf("[Sleep] WAKING UP...\n");
                    
                    // Turn on LCD
                    bcm2835_gpio_write(PIN_LED, HIGH);
//                    lcd_write_cmd(0x29); // Display ON
                    
                    printf("[Sleep] System resumed!\n");
                }
                
                // Wait for release
                while (bcm2835_gpio_lev(PIN_BTN_SLEEP) == LOW) {
                    bcm2835_delay(10);
                }
                
                // Extra delay để tránh double-press
                bcm2835_delay(300);
            }
        }
        
        bcm2835_delay(50);
    }
    return NULL;
}

// ==========================================
// TASK 3: NETWORK
// ==========================================

void* task_network(void* arg) {
    printf("[Task Net] Starting...\n");
    Network_LoadUsers();

    NetworkJob job;
    while(g_running) {
        // Check return value
        if (!queue_pop(&q_network, &job)) {
            printf("[Task Net] Shutdown signal received\n");
            break;
        }

        switch (job.type) {
            case JOB_LOG_ATTENDANCE:
                Network_SendLog(job.id, job.name);
                break;
            case JOB_REGISTER_USER:
                Network_RegisterUser(job.embedding);
                break;
            case JOB_LOAD_USERS:
                Network_LoadUsers();
                g_is_loading_users = false; 
                break;
        }
    }
    return NULL;
}

// ==========================================
// TASK 4: AI PROCESSING (NETWORK MODE)
// ==========================================

void* task_ai(void* arg) {
    printf("[Task AI] Loading Models...\n");
    
    // Load model
    if (!faceNet.loadModel("MobileFaceNet.onnx")) {
        printf("[Task AI] CRITICAL: Model load failed!\n");
        return NULL;
    }
    
    // Load cascade
    cv::CascadeClassifier face_cascade;
    if(!face_cascade.load("/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml")) {
        if(!face_cascade.load("haarcascade_frontalface_default.xml")) {
            printf("[Task AI] Error: Cannot load cascade!\n");
            return NULL;
        }
    }

    cv::Mat process_frame;
    printf("[Task AI] ===== NETWORK MODE LOADED =====\n");
    printf("[Task AI] Using: Cosine Similarity | Stability Filter | Network Database\n\n");

    while(g_running) {
        std::unique_lock<std::mutex> lock(mtx_ai);
        cv_ai.wait(lock, []{ return new_frame_for_ai || !g_running; });

        if (!g_running) break;

        // Skip khi đang sleep
        if (g_is_sleeping) {
            new_frame_for_ai = false;
            lock.unlock();
            usleep(100000);
            continue;
        }

        process_frame = shared_ai_frame.clone();
        new_frame_for_ai = false;
        lock.unlock();

        // AI Processing
        AIResult local_result;
        local_result.has_detection = false;
        local_result.message = "Scanning...";
        local_result.color = cv::Scalar(0, 255, 255);
        
        // Detect faces
        std::vector<cv::Rect> faces;
        cv::Mat gray;
        cv::cvtColor(process_frame, gray, cv::COLOR_BGR2GRAY);
        
        face_cascade.detectMultiScale(
            gray, faces,
            1.05, 5, 0,
            cv::Size(60, 60),
            cv::Size(240, 240)
        );

        if (!faces.empty()) {
            cv::Rect best_face = selectBestFace(faces, process_frame, faceNet);
            
            if (best_face.area() > 0) {
                local_result.has_detection = true;
                local_result.faces.push_back(best_face);
                
                cv::Mat face_roi = process_frame(best_face);
                float quality = faceNet.checkQuality(face_roi);

                // === REGISTRATION MODE (Button pressed) ===
                if (g_is_register_requested) {
                    g_is_register_requested = false;
                    
                    if (quality > 0.50f) {
                        cv::Mat current_embedding = faceNet.getEmbedding(face_roi);
                        
                        if (!current_embedding.empty()) {
                            local_result.message = "REGISTERING...";
                            local_result.color = cv::Scalar(255, 165, 0);
                            
                            // Update UI immediately
                            {
                                std::lock_guard<std::mutex> lk(mtx_ai);
                                shared_result = local_result;
                            }

                            // Send to network
                            NetworkJob job;
                            job.type = JOB_REGISTER_USER;
                            job.embedding = current_embedding.clone();
                            queue_push(&q_network, job);
                            
                            printf("[Register] Embedding sent to server (Quality: %.2f)\n", quality);
                        } else {
                            local_result.message = "Embedding failed!";
                            local_result.color = cv::Scalar(0, 0, 255);
                        }
                    } else {
                        local_result.message = "Quality too low!";
                        local_result.color = cv::Scalar(100, 100, 255);
                        printf("[Register] Quality too low: %.2f (need >0.50)\n", quality);
                    }
                }
                // === RECOGNITION MODE ===
                else {
                    if (quality > 0.45f) {
                        cv::Mat current_embedding = faceNet.getEmbedding(face_roi);
                        
                        if (!current_embedding.empty()) {
                            // Search in network database
                            std::string name, id;
                            float similarity = 0.0f;
                            bool match = Network_FindMatch(current_embedding, faceNet, name, id, similarity);

                            if (match) {
                                // Use stability filter like local mode
                                if (name.empty() && !g_is_loading_users) {
                                    printf("[Recognition] Name empty → Reloading users from server...\n");
                            
                                    NetworkJob reload_job;
                                    reload_job.type = JOB_LOAD_USERS;
                                    queue_push(&q_network, reload_job);
                            
                                    g_is_loading_users = true;   // khóa lại
                                }
                                
                                
                                
                                bool is_stable = detection_filter.isStable(similarity);
                                float avg_similarity = detection_filter.getAverage();
                                
                                const float THRESHOLD = 0.90f; // Cosine similarity threshold
                                
                                if (is_stable) {
                                    if (avg_similarity >= THRESHOLD) {
                                        std::string disp = name.empty() ? id : name;
                                        
                                        local_result.message = "WELCOME " + disp;
                                        local_result.color = cv::Scalar(0, 255, 0);
                                        
                                        printf("[Recognition] ✓ MATCH: %s (Sim: %.3f)\n", 
                                               disp.c_str(), avg_similarity);

                                        // Log attendance
                                        NetworkJob job;
                                        job.type = JOB_LOG_ATTENDANCE;
                                        job.id = id;
                                        job.name = disp;
                                        queue_push(&q_network, job);
                                    } else {
                                        local_result.message = "ACCESS DENIED";
                                        local_result.color = cv::Scalar(0, 0, 255);
                                        printf("[Recognition] ✗ Below threshold (Sim: %.3f)\n", 
                                               avg_similarity);
                                    }
                                } else {
                                    // Still analyzing
                                    local_result.message = "Analyzing... " + 
                                                          std::to_string((int)(similarity*100)) + "%";
                                    local_result.color = cv::Scalar(255, 200, 0);
                                }
                            } else {
                                // No match in database
                                local_result.message = "UNKNOWN PERSON";
                                local_result.color = cv::Scalar(0, 0, 255);
                                detection_filter.clear();
                                printf("[Recognition] No match found in database\n");
                            }
                        } else {
                            local_result.message = "Embedding Error";
                            local_result.color = cv::Scalar(150, 150, 150);
                        }
                    } else {
                        local_result.message = "Move closer (Q:" + 
                                              std::to_string((int)(quality*100)) + ")";
                        local_result.color = cv::Scalar(150, 150, 150);
                        detection_filter.clear();
                    }
                }
            }
        } else {
            // No face detected
            local_result.message = "No Face Detected";
            local_result.color = cv::Scalar(200, 200, 200);
            detection_filter.clear();
            
            if (g_is_register_requested) {
                g_is_register_requested = false;
                printf("[Register] Cancelled - no face detected\n");
            }
        }

        // Update result
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            shared_result = local_result;
        }
        
        usleep(5000);
    }
    return NULL;
}

// ==========================================
// TASK 5: LCD DISPLAY
// ==========================================

void* task_lcd(void* arg) {
    uint8_t* spi_buffer = (uint8_t*)malloc(LCD_WIDTH * LCD_HEIGHT * 2);
    if (!spi_buffer) {
        printf("[Task LCD] Malloc failed!\n");
        return NULL;
    }

    cv::Mat frame;
    AIResult current_ai_state;
    printf("[Task LCD] Started\n");
    
    while(g_running) {
        // Check return value - false = shutdown signal
        if (!queue_pop(&q_display, &frame)) {
            printf("[Task LCD] Shutdown signal received\n");
            break;
        }
        
        if (frame.cols != LCD_WIDTH || frame.rows != LCD_HEIGHT) {
            cv::resize(frame, frame, cv::Size(LCD_WIDTH, LCD_HEIGHT));
        }

        // Get AI state
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            current_ai_state = shared_result;
        }

        // Draw UI
        if (current_ai_state.has_detection) {
            for (size_t i = 0; i < current_ai_state.faces.size(); i++) {
                cv::rectangle(frame, current_ai_state.faces[i], 
                             current_ai_state.color, 2);
            }
            
            if (!current_ai_state.faces.empty() && !current_ai_state.message.empty()) {
                cv::Point p = current_ai_state.faces[0].tl();
                p.y = (p.y < 20) ? 20 : p.y - 10;
                cv::putText(frame, current_ai_state.message, p, 
                           cv::FONT_HERSHEY_SIMPLEX, 0.6, 
                           current_ai_state.color, 2);
            }
        } else if (!current_ai_state.message.empty()) {
            cv::putText(frame, current_ai_state.message, cv::Point(5, 20), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, 
                       cv::Scalar(200, 200, 200), 1);
        }

        // Convert BGR to RGB565 (optimized)
        uint8_t* pSrc = frame.data;
        uint8_t* pDst = spi_buffer;
        int num_pixels = LCD_WIDTH * LCD_HEIGHT;
        
        for (int i = 0; i < num_pixels; i++) {
            uint8_t b = *pSrc++;
            uint8_t g = *pSrc++;
            uint8_t r = *pSrc++;
            
            uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            *pDst++ = (color >> 8);
            *pDst++ = (color & 0xFF);
        }
        
        // Send to LCD via SPI
        bcm2835_gpio_write(PIN_DC, LOW);
        lcd_set_window(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1); 
        bcm2835_gpio_write(PIN_DC, HIGH);
        bcm2835_spi_transfern((char*)spi_buffer, LCD_WIDTH * LCD_HEIGHT * 2);
    }
    
    free(spi_buffer);
    return NULL;
}
