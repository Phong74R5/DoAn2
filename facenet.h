#ifndef FACENET_H
#define FACENET_H

#include <opencv4/opencv2/opencv.hpp>
#include <opencv4/opencv2/dnn.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

class FaceNet {
private:
    cv::dnn::Net net;
    bool is_loaded = false;

    // ---------------------------
    // Chuẩn hóa preprocessing (MobileFaceNet / InsightFace)
    // ---------------------------
    cv::Mat preprocessFaceStandard(const cv::Mat& face_img) {
        if (face_img.empty()) return cv::Mat();

        cv::Mat processed;
        
        // 1. Resize về 112x112 (Input chuẩn của MobileFaceNet)
        cv::resize(face_img, processed, cv::Size(112, 112), 0, 0, cv::INTER_LINEAR);
        
        // 2. Convert sang RGB
        cv::cvtColor(processed, processed, cv::COLOR_BGR2RGB);
        
        // 3. Normalize: (img - 127.5) / 128.0
        processed.convertTo(processed, CV_32FC3);
        processed = (processed - 127.5) / 128.0;
        
        return processed;
    }

    // ---------------------------
    // Đánh giá chất lượng ảnh (Giữ nguyên để lọc ảnh mờ)
    // ---------------------------
    float assessFaceQuality(const cv::Mat& face_img) {
        if (face_img.empty() || face_img.cols < 40 || face_img.rows < 40) return 0.0f;

        float score = 0.0f;

        // 1. Kích thước (ảnh to thì tốt)
        float size_score = std::min(1.0f, (face_img.cols * face_img.rows) / 8000.0f);
        score += size_score * 0.3f;

        // 2. Độ sắc nét (Laplacian)
        cv::Mat gray, laplacian;
        if (face_img.channels() == 3) cv::cvtColor(face_img, gray, cv::COLOR_BGR2GRAY);
        else gray = face_img.clone();
        
        cv::Laplacian(gray, laplacian, CV_64F);
        cv::Scalar mean, stddev;
        cv::meanStdDev(laplacian, mean, stddev);
        float sharpness = stddev[0] * stddev[0];
        float sharpness_score = std::min(1.0f, sharpness / 400.0f);
        score += sharpness_score * 0.4f;

        // 3. Độ sáng
        cv::Scalar avg_brightness = cv::mean(gray);
        float brightness_score = 1.0f - std::abs(avg_brightness[0] - 127.0f) / 127.0f;
        score += brightness_score * 0.3f;

        return score;
    }

public:
    FaceNet() {}

    // ---------------------------
    // Load Model
    // ---------------------------
    bool loadModel(const std::string& modelPath) {
        try {
            net = cv::dnn::readNetFromONNX(modelPath);
            if (net.empty()) {
                std::cerr << "[FaceNet] Model loaded but EMPTY\n";
                is_loaded = false;
                return false;
            }
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            is_loaded = true;
            std::cout << "[FaceNet] Model loaded: " << modelPath << std::endl;
            return true;
        } catch (const cv::Exception& e) {
            std::cerr << "[FaceNet] Error: " << e.what() << std::endl;
            is_loaded = false;
            return false;
        }
    }

    // ---------------------------
    // Lấy Embedding (Quan trọng nhất)
    // ---------------------------
    cv::Mat getEmbedding(const cv::Mat& face_img) {
        if (!is_loaded || net.empty() || face_img.empty()) return cv::Mat();

        // Preprocessing chuẩn
        cv::Mat processed = preprocessFaceStandard(face_img);
        if (processed.empty()) return cv::Mat();

        // Tạo blob
        cv::Mat blob = cv::dnn::blobFromImage(
            processed,
            1.0,                    // scale = 1.0 vì đã normalize thủ công
            cv::Size(112, 112),
            cv::Scalar(0, 0, 0),    // mean = 0
            false,                  // swapRB = false
            false                   // crop
        );

        net.setInput(blob);
        cv::Mat emb = net.forward();
        
        // L2 Normalization (Bắt buộc để tính Cosine Similarity chính xác)
        // Nếu không có bước này, vector sẽ có độ dài lung tung, so sánh sai hết.
        double norm = cv::norm(emb, cv::NORM_L2);
        if (norm > 1e-6) {
            emb /= norm;
        }
        
        return emb.clone(); // Trả về bản sao an toàn
    }

    // ---------------------------
    // (Đã xóa registerOwner phức tạp)
    // Chúng ta xử lý logic đăng ký ở tasks.cpp
    // ---------------------------

    bool isLoaded() const { return is_loaded; }
    
    // Public hàm checkQuality để tasks.cpp gọi
    float checkQuality(const cv::Mat& face_img) { return assessFaceQuality(face_img); }
};

#endif