#ifndef FACENET_H
#define FACENET_H

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <iostream>
#include <string>
#include <vector>

class FaceNet {
private:
    cv::dnn::Net net;
    bool is_loaded = false; // Cờ đánh dấu

public:
    FaceNet() {}

    void loadModel(const std::string& modelPath) {
        try {
            net = cv::dnn::readNetFromONNX(modelPath);
            
            if (net.empty()) {
                std::cerr << "[FaceNet] Model loaded but EMPTY!" << std::endl;
                is_loaded = false;
                return;
            }

            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            
            is_loaded = true;
            std::cout << "[FaceNet] Model loaded successfully: " << modelPath << std::endl;
        } catch (const cv::Exception& e) {
            std::cerr << "[FaceNet] Error loading model: " << e.what() << std::endl;
            is_loaded = false;
        }
    }

    cv::Mat getEmbedding(const cv::Mat& face_img) {
        // --- FIX CRASH: Kiểm tra model trước khi chạy ---
        if (!is_loaded || net.empty()) {
            // Trả về ma trận rỗng nếu chưa load model
            return cv::Mat(); 
        }

        if (face_img.empty()) return cv::Mat();

        cv::Mat blob;
        // MobileFaceNet chuẩn dùng size 112x112
        cv::dnn::blobFromImage(face_img, blob, 
                               1.0 / 128.0,             
                               cv::Size(112, 112),      
                               cv::Scalar(127.5, 127.5, 127.5), 
                               true,                    
                               false);                  

        net.setInput(blob);
        return net.forward().clone();
    }

    float calculateDistance(const cv::Mat& v1, const cv::Mat& v2) {
        // Nếu vector rỗng (do lỗi model), trả về khoảng cách lớn nhất (khác nhau)
        if (v1.empty() || v2.empty()) return 10.0f; 

        double dot = v1.dot(v2);
        double norm1 = cv::norm(v1);
        double norm2 = cv::norm(v2);
        
        // Tránh chia cho 0
        if (norm1 == 0 || norm2 == 0) return 10.0f;

        double similarity = dot / (norm1 * norm2);
        return 1.0f - (float)similarity;
    }
};

#endif
