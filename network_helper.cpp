#include "network_helper.h"
#include <iostream>
#include <curl/curl.h>
#include <mutex>
#include <map>
#include <ctime>
#include "json.hpp" // File json.hpp bạn đã tải

using json = nlohmann::json;

// ================= CONFIG =================
const std::string FIREBASE_HOST = "https://YOUR_PROJECT_ID.firebaseio.com"; 
const float MATCH_THRESHOLD = 0.60f; // Độ chính xác > 60% là nhận

// ================= DATA STORE =================
struct UserInfo {
    std::string id;
    std::string name;
    std::vector<float> embedding;
};

// Biến toàn cục (Chỉ trong file này thấy)
static std::vector<UserInfo> g_users;
static std::mutex mtx_users;           // Khóa bảo vệ g_users
static std::mutex mtx_log;             // Khóa bảo vệ log
static std::map<std::string, time_t> last_checkin; // Cache thời gian check-in

// ================= INTERNAL HELPER =================

// Lấy thời gian hiện tại
static void getCurrentDateTime(std::string &dateStr, std::string &timeStr) {
    time_t now = time(nullptr);
    tm *ltm = localtime(&now);

    // [FIX] Tăng size lên 40 cho trình biên dịch hết lo lắng
    char d[40], t[40];

    snprintf(d, sizeof(d), "%04d-%02d-%02d", ltm->tm_year+1900, ltm->tm_mon+1, ltm->tm_mday);
    snprintf(t, sizeof(t), "%02d:%02d:%02d", ltm->tm_hour, ltm->tm_min, ltm->tm_sec);

    dateStr = d;
    timeStr = t;
}

// Callback hứng data từ Curl
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Hàm gửi request tổng quát
static std::string firebaseRequest(const std::string &endpoint, const std::string &method, const std::string &jsonData = "") {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;
    std::string url = FIREBASE_HOST + endpoint;

    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!jsonData.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Bỏ qua check SSL
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);       // Timeout 10s

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            fprintf(stderr, "[Network] Curl Failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    return readBuffer;
}

// ================= IMPLEMENTATION API =================

void Network_LoadUsers() {
    printf("[Network] Loading users from Firebase...\n");
    std::string resp = firebaseRequest("/users.json", "GET");

    if (resp == "null" || resp.empty()) return;

    try {
        json j = json::parse(resp);
        
        std::lock_guard<std::mutex> lock(mtx_users); // Lock để ghi vào RAM
        g_users.clear();

        for (auto& element : j.items()) {
            UserInfo u;
            u.id = element.key();
            u.name = element.value().value("name", "Unknown");
            
            if (element.value().contains("embedding")) {
                u.embedding = element.value()["embedding"].get<std::vector<float>>();
                g_users.push_back(u);
            }
        }
        printf("[Network] Loaded %zu users success.\n", g_users.size());
    } catch (std::exception& e) {
        printf("[Network] JSON Parse Error: %s\n", e.what());
    }
}

void Network_RegisterUser(const cv::Mat& embedding) {
    // 1. Tạo ID
    std::string dateStr, timeStr;
    getCurrentDateTime(dateStr, timeStr);
    std::string newId = "id_" + dateStr + "_" + timeStr;
    std::replace(newId.begin(), newId.end(), '-', '_');
    std::replace(newId.begin(), newId.end(), ':', '_');

    // 2. Convert Mat -> Vector -> JSON
    std::vector<float> embVec;
    if (embedding.isContinuous()) {
        embVec.assign((float*)embedding.datastart, (float*)embedding.dataend);
    } else {
        for (int i = 0; i < embedding.cols; ++i) 
            embVec.push_back(embedding.at<float>(0, i));
    }

    json j;
    j[newId] = {
        {"name", ""}, // Tên để trống, cập nhật trên Web sau
        {"embedding", embVec}
    };

    // 3. Gửi lên
    printf("[Network] Uploading user: %s\n", newId.c_str());
    firebaseRequest("/users.json", "PATCH", j.dump());

    // 4. Reload lại list user ngay lập tức để nhận diện được luôn
    Network_LoadUsers();
}

void Network_SendLog(const std::string& id, const std::string& name) {
    // Check spam log (Debounce 30s)
    {
        std::lock_guard<std::mutex> lock(mtx_log);
        time_t now = time(nullptr);
        if (last_checkin.count(id) && (now - last_checkin[id] < 30)) {
            return; // Chưa đủ 30s, bỏ qua
        }
        last_checkin[id] = now;
    }

    std::string dateStr, timeStr;
    getCurrentDateTime(dateStr, timeStr);

    json j;
    j[id] = {
        {"name", name},
        {"date", dateStr},
        {"time", timeStr}
    };

    // Gửi log
    firebaseRequest("/attendance.json", "PATCH", j.dump());
    printf("[Network] Logged: %s at %s\n", name.c_str(), timeStr.c_str());
}

bool Network_FindMatch(const cv::Mat& current_embedding, FaceNet& faceNet, 
                       std::string& outName, std::string& outId, float& outScore) {
    
    std::lock_guard<std::mutex> lock(mtx_users); // Lock để đọc an toàn
    if (g_users.empty()) return false;

    float maxScore = -1.0f;
    int bestIdx = -1;

    // Duyệt qua tất cả user trong RAM
    for (size_t i = 0; i < g_users.size(); ++i) {
        // Tạo Mat từ vector
        cv::Mat userEmb(1, g_users[i].embedding.size(), CV_32F, g_users[i].embedding.data());
        
        // Tính similarity (Cosine Distance)
        float score = faceNet.calculateSimilarity(current_embedding, userEmb);
        
        if (score > maxScore) {
            maxScore = score;
            bestIdx = i;
        }
    }

    if (bestIdx != -1 && maxScore > MATCH_THRESHOLD) {
        outId = g_users[bestIdx].id;
        outName = g_users[bestIdx].name;
        outScore = maxScore;
        return true;
    }

    return false;
}