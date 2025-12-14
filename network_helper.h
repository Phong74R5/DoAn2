#ifndef NETWORK_HELPER_H
#define NETWORK_HELPER_H

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "facenet.h" // Để dùng class FaceNet tính toán

// --- Cấu trúc Job cho Queue (Để tasks.cpp dùng) ---
enum NetJobType { 
    JOB_LOAD_USERS, 
    JOB_LOG_ATTENDANCE, 
    JOB_REGISTER_USER 
};

struct NetworkJob {
    NetJobType type;
    std::string id;
    std::string name;
    cv::Mat embedding; // Dùng khi đăng ký user mới
};

// --- Các hàm API Public ---

// Tải danh sách user từ Firebase về RAM
void Network_LoadUsers();

// Gửi embedding lên Firebase để tạo User mới
void Network_RegisterUser(const cv::Mat& embedding);

// Gửi log điểm danh lên Firebase (có debounce chống spam)
void Network_SendLog(const std::string& id, const std::string& name);

// Tìm người giống nhất trong danh sách đã tải về
// Trả về true nếu tìm thấy, output ra name, id, score
bool Network_FindMatch(const cv::Mat& current_embedding, FaceNet& faceNet, 
                       std::string& outName, std::string& outId, float& outScore);

#endif