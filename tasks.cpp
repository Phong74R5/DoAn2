#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <string>

#include "tasks.h"
#include "queue_helper.h"
#include "lcd_driver.h"
#include "config.h"
#include "facenet.h" 
//Tổng quan hệ thống 3 task chạy song song
// --- DỮ LIỆU CHIA SẺ (SHARED DATA) ---

// Struct lưu kết quả nhận diện để luồng LCD vẽ

struct AIResult {
    std::vector<cv::Rect> faces;
    std::string message;
    cv::Scalar color;
    bool has_detection;
};

// Biến toàn cục và Mutex bảo vệ
// Biến toàn cục và Mutex bảo vệ
std::mutex mtx_ai;              // Khóa an toàn
cv::Mat shared_ai_frame;        // Frame mới nhất để AI xử lý
bool new_frame_for_ai = false;  // Cờ báo có ảnh mới
AIResult shared_result;         // Kết quả AI để LCD hiển thị

// Đối tượng FaceNet và biến lưu chủ nhân
// Lưu trữ nhiều embeddings cho việc đăng ký
std::vector<cv::Mat> owner_face_samples;  // Lưu ảnh mẫu
FaceNet faceNet;
cv::Mat owner_embedding;
bool has_owner = false;
const int REQUIRED_SAMPLES = 10;           // Cần 15 mẫu tốt để đăng ký
const int MIN_FRAME_GAP = 15;             // Chờ 15 frame giữa các mẫu
int frame_counter_since_last_sample = 0;  // Đếm frame

// Thống kê để debug
struct RegistrationStats {
    std::vector<float> quality_scores;
    std::vector<float> similarities;  // So sánh giữa các mẫu
    
    void addSample(float quality) {
        quality_scores.push_back(quality);
    }
    
    void printStats() {
        if (quality_scores.empty()) return;
        
        float avg_quality = 0;
        for (float q : quality_scores) avg_quality += q;
        avg_quality /= quality_scores.size();
        
        printf("\n=== REGISTRATION STATS ===\n");
        printf("Samples collected: %zu\n", quality_scores.size());
        printf("Average quality: %.2f\n", avg_quality);
        
        if (!similarities.empty()) {
            float avg_sim = 0;
            for (float s : similarities) avg_sim += s;
            avg_sim /= similarities.size();
            printf("Avg inter-sample similarity: %.3f\n", avg_sim);
        }
        printf("==========================\n\n");
    }
    
    void clear() {
        quality_scores.clear();
        similarities.clear();
    }
};

RegistrationStats reg_stats;

// Bộ lọc kết quả
struct DetectionFilter {
    std::vector<float> recent_similarities;
    const int WINDOW_SIZE = 7;  // Tăng lên 7 frame
    
    bool isStable(float new_similarity) {
        recent_similarities.push_back(new_similarity);
        if (recent_similarities.size() > (size_t)WINDOW_SIZE) {
            recent_similarities.erase(recent_similarities.begin());
        }
        
        if (recent_similarities.size() < 5) return false;
        
        // Tính độ lệch chuẩn
        float mean = 0;
        for (float s : recent_similarities) mean += s;
        mean /= recent_similarities.size();
        
        float variance = 0;
        for (float s : recent_similarities) {
            variance += (s - mean) * (s - mean);
        }
        variance /= recent_similarities.size();
        float stddev = sqrt(variance);
        
        // Ổn định nếu stddev < 0.05
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

DetectionFilter detection_filter;

// === HÀM HỖ TRỢ CẢI TIẾN ===

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

// Kiểm tra mẫu có đủ khác biệt không
bool isSampleDiverse(const cv::Mat& new_sample, FaceNet& faceNet) {
    if (owner_face_samples.size() < 2) return true;
    
    cv::Mat new_emb = faceNet.getEmbedding(new_sample);
    if (new_emb.empty()) return false;
    
    // So sánh với mẫu cuối cùng
    cv::Mat last_sample = owner_face_samples.back();
    cv::Mat last_emb = faceNet.getEmbedding(last_sample);
    
    if (last_emb.empty()) return false;
    
    float similarity = faceNet.cosineSimilarity(new_emb, last_emb);
    reg_stats.similarities.push_back(similarity);
    
    // Nếu quá giống (>0.95) thì từ chối
    if (similarity > 0.95f) {
        printf("[Diversity Check] Too similar (%.3f) - REJECTED\n", similarity);
        return false;
    }
    
    printf("[Diversity Check] Similarity: %.3f - OK\n", similarity);
    return true;
}



// --- TASK 1: CAMERA (PRODUCER) ---
//Camera Thread  -->  đưa ảnh vào Queue
/*Nhiệm vụ:
✔ Mở camera
✔ Lấy frame liên tục
✔ Đẩy frame vào queue hiển thị
✔ Gửi frame cho AI xử lý (shared frame)*/
void* task_camera(void* arg) {
    // Mở Camera (Ưu tiên V4L2 trên Linux/Pi)
    cv::VideoCapture cap(0, cv::CAP_V4L2);

    // Cấu hình cứng độ phân giải khớp với LCD để không phải resize
    cap.set(cv::CAP_PROP_FRAME_WIDTH, LCD_WIDTH);  // 320
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, LCD_HEIGHT); // 240
    cap.set(cv::CAP_PROP_FPS, 30);


    if (!cap.isOpened()) {
        printf("[Task Cam] Error: Cannot open camera! Check connection.\n");
        return NULL;
    }

    cv::Mat cam_frame;
    cv::Mat frame;
    printf("[Task Cam] Started successfully\n");

    while(1) {
        cap >> cam_frame;
        cv::resize(cam_frame, frame, cv::Size(LCD_WIDTH, LCD_HEIGHT));   
        if (frame.empty()) {
            usleep(10000);
            continue;
        }

        // 1. Đẩy vào hàng đợi hiển thị (Queue Display)
        // Clone() là bắt buộc để tránh xung đột vùng nhớ
        queue_push(&q_display, frame.clone());

        // 2. Cập nhật frame cho AI (Ghi đè frame cũ nếu AI chưa xử lý kịp)
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            shared_ai_frame = frame.clone();
            new_frame_for_ai = true;
        }

        // Ngủ nhẹ để giảm tải CPU nếu cần (tùy chọn)
        usleep(1000); 
    }
    return NULL;
}


// --- TASK 2: AI PROCESSING (BACKGROUND) ---
//AI Thread -->  đọc shared frame -> detect face -> embed -> compare
/*Nhiệm vụ:
1️⃣ Load model MobileFaceNet
2️⃣ Load Haar cascade để detect face
3️⃣ Lấy frame từ thread Camera
🔍 AI xử lý gồm 3 bước lớn
Bước A: Detect Face
Bước B: Lấy embedding từ khuôn mặt lớn nhất
Bước C: So sánh embedding để nhận diện
Nếu chưa có chủ nhân (has_owner = false)
Nếu đã có chủ nhân → So sánh cosine distance
4️⃣ Cập nhật kết quả cho LCD
*/
// === TASK AI NÂNG CẤP ===
// === TASK AI FINAL VERSION ===

void* task_ai_improved(void* arg) {
    printf("[Task AI] Loading Models...\n");
    
    // Load Model
    try {
        faceNet.loadModel("MobileFaceNet.onnx");
        if (!faceNet.isLoaded()) {
            printf("[Task AI] CRITICAL: Model load failed!\n");
            return NULL;
        }
    } catch (const cv::Exception& e) {
        printf("[Task AI] Error: %s\n", e.what());
        return NULL;
    }
    
    // Load Haar Cascade
    cv::CascadeClassifier face_cascade;
    if(!face_cascade.load("/home/pi/opencv/data/haarcascades/haarcascade_frontalface_default.xml")) {
        if(!face_cascade.load("haarcascade_frontalface_default.xml")) {
            printf("[Task AI] Error: Cannot load cascade!\n");
            return NULL;
        }
    }

    cv::Mat process_frame;
    printf("[Task AI] ===== FINAL VERSION LOADED =====\n");
    printf("[Task AI] Using: Cosine Similarity | Augmentation | Diversity Check\n\n");

    while(1) {
        bool has_new = false;

        // Lấy frame
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            if (new_frame_for_ai) {
                process_frame = shared_ai_frame.clone();
                new_frame_for_ai = false;
                has_new = true;
            }
        }

        if (!has_new) {
            usleep(10000);
            continue;
        }

        frame_counter_since_last_sample++;

        // === XỬ LÝ AI ===
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
                
                // === ĐĂNG KÝ CHỦ NHÂN ===
                if (!has_owner) {
                    // Yêu cầu: Quality cao + Đợi đủ frame gap + Đa dạng
                    if (quality > 0.55f && frame_counter_since_last_sample >= MIN_FRAME_GAP) {
                        
                        // Kiểm tra độ đa dạng
                        if (isSampleDiverse(face_roi, faceNet)) {
                            owner_face_samples.push_back(face_roi.clone());
                            reg_stats.addSample(quality);
                            frame_counter_since_last_sample = 0;
                            
                            int progress = (owner_face_samples.size() * 100) / REQUIRED_SAMPLES;
                            local_result.message = "Register: " + std::to_string(progress) + "%";
                            local_result.color = cv::Scalar(255, 200, 0);
                            
                            printf("[Register] Sample %zu/%d | Q: %.2f | Gap: OK\n", 
                                   owner_face_samples.size(), REQUIRED_SAMPLES, quality);
                            
                            // Đủ mẫu
                            if (owner_face_samples.size() >= REQUIRED_SAMPLES) {
                                printf("\n[Register] Processing samples...\n");
                                owner_embedding = faceNet.registerOwner(owner_face_samples);
                                
                                if (!owner_embedding.empty()) {
                                    has_owner = true;
                                    local_result.message = "REGISTRATION COMPLETE!";
                                    local_result.color = cv::Scalar(0, 255, 0);
                                    
                                    reg_stats.printStats();
                                    printf("[Register] ==> SUCCESS <==\n\n");
                                } else {
                                    printf("[Register] Failed! Retrying...\n");
                                    owner_face_samples.clear();
                                    reg_stats.clear();
                                    frame_counter_since_last_sample = 0;
                                }
                            }
                        } else {
                            local_result.message = "Move your head slightly";
                            local_result.color = cv::Scalar(255, 150, 0);
                        }
                    } else {
                        if (quality <= 0.55f) {
                            local_result.message = "Low Quality (Q:" + 
                                                  std::to_string((int)(quality*100)) + ")";
                        } else {
                            int frames_left = MIN_FRAME_GAP - frame_counter_since_last_sample;
                            local_result.message = "Wait " + std::to_string(frames_left) + " frames";
                        }
                        local_result.color = cv::Scalar(100, 100, 255);
                    }
                }
                // === NHẬN DIỆN ===
                else {
                    if (quality > 0.45f) {
                        cv::Mat current_embedding = faceNet.getEmbedding(face_roi);
                        
                        if (!current_embedding.empty()) {
                            float similarity = faceNet.cosineSimilarity(
                                current_embedding, 
                                owner_embedding
                            );
                            
                            // Lọc ổn định
                            bool is_stable = detection_filter.isStable(similarity);
                            float avg_similarity = detection_filter.getAverage();
                            
                            // QUAN TRỌNG: Threshold cao hơn cho Cosine Similarity
                            const float THRESHOLD = 0.90f;  // >= 0.90 = cùng người
                            
                            if (is_stable) {
                                if (avg_similarity >= THRESHOLD) {
                                    local_result.message = "ACCESS GRANTED";
                                    local_result.color = cv::Scalar(0, 255, 0);
                                    printf("[VERIFY] ✓ OWNER (Sim: %.3f)\n", avg_similarity);
                                } else {
                                    local_result.message = "ACCESS DENIED";
                                    local_result.color = cv::Scalar(0, 0, 255);
                                    printf("[VERIFY] ✗ UNKNOWN (Sim: %.3f)\n", avg_similarity);
                                }
                            } else {
                                local_result.message = "Analyzing... (" + 
                                                      std::to_string((int)(similarity*100)) + "%)";
                                local_result.color = cv::Scalar(200, 200, 0);
                            }
                        }
                    } else {
                        local_result.message = "Move closer (Q:" + 
                                              std::to_string((int)(quality*100)) + ")";
                        local_result.color = cv::Scalar(150, 150, 150);
                    }
                }
            }
        } else {
            local_result.message = "No Face";
            detection_filter.clear();
            frame_counter_since_last_sample = 0;
        }

        // Cập nhật kết quả
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            shared_result = local_result;
        }
        
        usleep(5000);
    }
    return NULL;
}

// --- TASK 3: LCD DISPLAY (CONSUMER) ---
//LCD Thread -->  lấy frame từ Queue -> vẽ UI -> convert RGB -> đẩy ra LCD SPI
//LCD sẽ lấy kết quả của AI từ đây để vẽ.
/*Nhiệm vụ:
✔ Lấy frame từ queue
✔ Vẽ bounding box + message của AI
✔ Convert BGR → RGB565
✔ Gửi frame qua SPI cho LCD*/
void* task_lcd(void* arg) {
    // Cấp phát buffer SPI 1 lần duy nhất
    uint8_t* spi_buffer = (uint8_t*)malloc(LCD_WIDTH * LCD_HEIGHT * 2);
    if (!spi_buffer) {
        printf("[Task LCD] Malloc failed!\n");
        return NULL;
    }

    cv::Mat frame;
    AIResult current_ai_state;
    printf("[Task LCD] Started\n");
    
    while(1) {
        // Lấy frame từ hàng đợi (Blocking wait -> Tiết kiệm CPU khi không có ảnh)
        queue_pop(&q_display, &frame);
        
        // 1. Resize nếu kích thước camera không khớp LCD
        if (frame.cols != LCD_WIDTH || frame.rows != LCD_HEIGHT) {
            cv::resize(frame, frame, cv::Size(LCD_WIDTH, LCD_HEIGHT));
        }

        // 2. Lấy thông tin AI mới nhất
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            current_ai_state = shared_result;
        }

        // 3. Vẽ UI lên ảnh (Vẽ TRƯỚC khi convert màu)
        if (current_ai_state.has_detection) {
            for (size_t i = 0; i < current_ai_state.faces.size(); i++) {
                cv::rectangle(frame, current_ai_state.faces[i], current_ai_state.color, 2);
            }
            // Vẽ chữ
            if (!current_ai_state.faces.empty()) {
                cv::Point p = current_ai_state.faces[0].tl();
                p.y = (p.y < 20) ? 20 : p.y - 10;
                cv::putText(frame, current_ai_state.message, p, 
                            cv::FONT_HERSHEY_SIMPLEX, 0.6, current_ai_state.color, 2);
            }
        } else {
             // Hiển thị trạng thái chờ ở góc
             cv::putText(frame, "Waiting...", cv::Point(5, 20), 
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);
        }

        // 4. Chuyển đổi BGR sang RGB565 (Tối ưu hóa vòng lặp)
        int idx = 0;
        uint8_t* data = frame.data;
        int channels = frame.channels(); // 3
        int width = frame.cols;
        int height = frame.rows;

        for (int i = 0; i < height; i++) {
            // Tối ưu hóa: Tính index dòng trước
            int row_offset = i * width * channels;
            
            for (int j = 0; j < width; j++) {
                int pixel_idx = row_offset + (j * channels);
                
                uint8_t b = data[pixel_idx + 0];
                uint8_t g = data[pixel_idx + 1];
                uint8_t r = data[pixel_idx + 2];
                
                // RGB565 conversion
                uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                
                // Big Endian cho SPI
                spi_buffer[idx++] = (c >> 8) & 0xFF;
                spi_buffer[idx++] = c & 0xFF;
            }
        }
        
        // 5. Gửi ra LCD qua SPI
        bcm2835_gpio_write(PIN_DC, LOW); // Command mode
        lcd_set_window(0, 0, LCD_WIDTH-1, LCD_HEIGHT-1); 
        
        bcm2835_gpio_write(PIN_DC, HIGH); // Data mode
        bcm2835_spi_transfern((char*)spi_buffer, LCD_WIDTH * LCD_HEIGHT * 2);
    }
    
    free(spi_buffer);
    return NULL;
}



    

