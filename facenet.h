#ifndef FACENET_H
#define FACENET_H

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

class FaceNet {
private:
    cv::dnn::Net net;
    bool is_loaded = false;

public:
    FaceNet() {}

    bool loadModel(const std::string& modelPath) {
        try {
            net = cv::dnn::readNetFromONNX(modelPath);
            if (net.empty()) {
                std::cerr << "[FaceNet] ERROR: Model is empty!" << std::endl;
                is_loaded = false;
                return false;
            }
            // Ưu tiên chạy CPU trên Raspberry Pi (OpenCV backend)
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            
            is_loaded = true;
            std::cout << "[FaceNet] Model loaded: " << modelPath << std::endl;
            return true;
        } catch (const cv::Exception& e) {
            std::cerr << "[FaceNet] Exception: " << e.what() << std::endl;
            is_loaded = false;
            return false;
        }
    }

    cv::Mat getEmbedding(const cv::Mat& face_img) {
        if (!is_loaded || face_img.empty()) return cv::Mat();

        cv::Mat blob;
        // Chuẩn hóa input cho MobileFaceNet: 112x112, scale 1/127.5, mean 127.5
        cv::dnn::blobFromImage(face_img, blob, 
                               1.0 / 127.5,        
                               cv::Size(112, 112), 
                               cv::Scalar(127.5, 127.5, 127.5), 
                               true, false);

        net.setInput(blob);
        cv::Mat output = net.forward();

        // --- CỰC KỲ QUAN TRỌNG: L2 NORMALIZE ---
        // Vector phải có độ dài = 1 để so sánh chính xác bằng Cosine
        cv::Mat outputNorm;
        cv::normalize(output, outputNorm); 
        
        return outputNorm.clone();
    }

    // Tính độ tương đồng (Cosine Similarity)
    // Giá trị từ -1 đến 1. Càng gần 1 càng giống nhau.
    float calculateSimilarity(const cv::Mat& v1, const cv::Mat& v2) {
        if (v1.empty() || v2.empty()) return -1.0f;
        // Vì đã Normalize, tích vô hướng chính là Cosine Similarity
        return (float)v1.dot(v2);
    }
};

#endif