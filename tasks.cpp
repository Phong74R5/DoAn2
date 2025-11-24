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

// --- DỮ LIỆU CHIA SẺ (SHARED DATA) ---
// Struct lưu kết quả nhận diện để luồng LCD vẽ
struct AIResult {
    std::vector<cv::Rect> faces;
    std::string message;
    cv::Scalar color;
    bool has_detection;
};

// Biến toàn cục và Mutex bảo vệ
std::mutex mtx_ai;              // Khóa an toàn
cv::Mat shared_ai_frame;        // Frame mới nhất để AI xử lý
bool new_frame_for_ai = false;  // Cờ báo có ảnh mới
AIResult shared_result;         // Kết quả AI để LCD hiển thị

// Đối tượng FaceNet và biến lưu chủ nhân
FaceNet faceNet;
cv::Mat owner_embedding;
bool has_owner = false;

// --- TASK 1: CAMERA (PRODUCER) ---
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
    
    cv::Mat frame;
    printf("[Task Cam] Started successfully\n");
    
    while(1) {
        cap >> frame;
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
void* task_ai_demo(void* arg) {
    printf("[Task AI] Loading Models...\n");
    
    // 1. Load Model MobileFaceNet ONNX
    // Đảm bảo file .onnx nằm cùng thư mục file chạy
    try {
        faceNet.loadModel("MobileFaceNet.onnx");
    } catch (const cv::Exception& e) {
        printf("[Task AI] Error loading ONNX model: %s\n", e.what());
    }
    
    // 2. Load Haar Cascade (Phát hiện khuôn mặt)
    cv::CascadeClassifier face_cascade;
    // Đường dẫn chuẩn trên Raspberry Pi OS (kiểm tra lại nếu lỗi)
    if(!face_cascade.load("/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml")){
        // Fallback: Tìm file xml ở thư mục hiện tại
        if(!face_cascade.load("haarcascade_frontalface_default.xml")) {
             printf("[Task AI] Error: Cannot load haarcascade xml!\n");
        }
    }

    cv::Mat process_frame;
    printf("[Task AI] Ready to process\n");

    while(1) {
        bool has_new = false;

        // 1. Lấy ảnh an toàn từ biến chia sẻ
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            if (new_frame_for_ai) {
                process_frame = shared_ai_frame.clone();
                new_frame_for_ai = false;
                has_new = true;
            }
        }

        if (!has_new) {
            usleep(10000); // Ngủ 10ms nếu không có ảnh mới
            continue;
        }

        // 2. Xử lý AI (Phần nặng nhất)
        AIResult local_result;
        local_result.has_detection = false;
        local_result.message = "Scanning...";
        local_result.color = cv::Scalar(0, 255, 255); // Vàng

        std::vector<cv::Rect> faces;
        cv::Mat gray;
        cv::cvtColor(process_frame, gray, cv::COLOR_BGR2GRAY);
        
        // Detect khuôn mặt (Tham số tối ưu cho Pi)
        face_cascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(30, 30));

        if (!faces.empty()) {
            local_result.has_detection = true;
            local_result.faces = faces;

            // Xử lý khuôn mặt to nhất (faces[0])
            cv::Mat face_roi = process_frame(faces[0]);
            
            // Lấy embedding từ MobileFaceNet
            cv::Mat current_embedding = faceNet.getEmbedding(face_roi);

            if (!has_owner) {
                // Logic đăng ký: Giữ mặt 50 frame (khoảng vài giây)
                static int frame_count = 0;
                frame_count++;
                if (frame_count > 30) {
                    owner_embedding = current_embedding.clone();
                    has_owner = true;
                    printf("[Task AI] --> OWNER REGISTERED <--\n");
                } else {
                    local_result.message = "Keep Still...";
                }
            } else {
                // Logic so sánh
                float dist = faceNet.calculateDistance(current_embedding, owner_embedding);
                
                // Ngưỡng (Threshold): 0.4 - 0.5 thường dùng cho Cosine Distance
                if (dist < 0.45) {
                    local_result.message = "ACCESS GRANTED";
                    local_result.color = cv::Scalar(0, 255, 0); // Xanh lá
                } else {
                    local_result.message = "UNKNOWN";
                    local_result.color = cv::Scalar(0, 0, 255); // Đỏ
                }
                // printf("Dist: %.3f\n", dist); // Debug khoảng cách
            }
        } else {
            local_result.message = "No Face";
        }

        // 3. Cập nhật kết quả ra ngoài
        {
            std::lock_guard<std::mutex> lock(mtx_ai);
            shared_result = local_result;
        }
    }
    return NULL;
}

// --- TASK 3: LCD DISPLAY (CONSUMER) ---
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
