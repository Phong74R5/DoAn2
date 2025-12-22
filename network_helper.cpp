#include "network_helper.h"
#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <mutex>
#include <map>
#include <ctime>
#include <cstring> 
#include <algorithm>
#include <sstream> // Cần để convert embedding sang string
#include "json.hpp" 

using json = nlohmann::json;

// --- CẤU HÌNH ---
const std::string FIREBASE_HOST = "https://facedetect-7c1fe-default-rtdb.firebaseio.com"; 
const std::string LOCAL_DB_FILE = "userdata.dat"; 
const char ENCRYPTION_KEY = 0xAA; 

struct UserDiskRecord {
    char id[64];
    char name[64];
    float embedding[1024]; 
    int embedding_size; 
};

static std::mutex mtx_log;
static std::map<std::string, time_t> last_checkin;

// --- HELPER FUNCTIONS ---

void xor_crypt(char* data, size_t size) {
    for (size_t i = 0; i < size; ++i) data[i] ^= ENCRYPTION_KEY;
}

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Convert vector float sang JSON string mảng: "[0.1, 0.2, ...]"
std::string EmbeddingToJsonString(const std::vector<float>& emb) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < emb.size(); ++i) {
        ss << emb[i];
        if (i < emb.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

static std::string firebaseGet(std::string path) {
    CURL *curl = curl_easy_init();
    std::string readBuffer;
    if(curl) {
        std::string url = FIREBASE_HOST + path;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        // Bỏ qua xác thực SSL để chạy nhanh trên embedded (Production nên bật lại)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // Timeout 5s
        
        CURLcode res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            printf("[Net] GET Failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

static void firebaseRequest(std::string path, std::string method, std::string data = "") {
    CURL *curl = curl_easy_init();
    if(curl) {
        std::string url = FIREBASE_HOST + path;
        struct curl_slist *headers = NULL; // Khai báo ngoài để free sau này

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        
        if (method == "POST") curl_easy_setopt(curl, CURLOPT_POST, 1L);
        else if (method == "PATCH") curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        else if (method == "PUT") curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        else if (method == "DELETE") curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

        if (!data.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        std::string dummy; // Hứng response để không in ra stdout
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dummy);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        CURLcode res = curl_easy_perform(curl);
        if(res != CURLE_OK) {
            printf("[Net] %s Failed: %s\n", method.c_str(), curl_easy_strerror(res));
        }

        // [QUAN TRỌNG] Giải phóng bộ nhớ headers để tránh leak RAM
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

// --- CORE FUNCTIONS ---

void Network_RewriteStore(const std::vector<UserInfo>& users) {
    std::ofstream ofs(LOCAL_DB_FILE, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return;

    for (const auto& u : users) {
        UserDiskRecord record;
        memset(&record, 0, sizeof(UserDiskRecord));
        strncpy(record.id, u.id.c_str(), 63);
        strncpy(record.name, u.name.c_str(), 63);
        
        record.embedding_size = (int)u.embedding.size();
        if (record.embedding_size > 1024) record.embedding_size = 1024; // Limit check
        
        if (record.embedding_size > 0) {
            std::copy(u.embedding.begin(), u.embedding.begin() + record.embedding_size, record.embedding);
        }

        xor_crypt((char*)&record, sizeof(UserDiskRecord));
        ofs.write((char*)&record, sizeof(UserDiskRecord));
    }
    ofs.close();
    printf("[Storage] Database rewritten with %zu users.\n", users.size());
}

std::vector<UserInfo> Network_LoadDatabase() {
    std::vector<UserInfo> users;
    std::ifstream ifs(LOCAL_DB_FILE, std::ios::binary);
    if (!ifs.is_open()) return users;
    
    UserDiskRecord record;
    while (ifs.read((char*)&record, sizeof(UserDiskRecord))) {
        xor_crypt((char*)&record, sizeof(UserDiskRecord)); 
        UserInfo u;
        u.id = std::string(record.id);
        u.name = std::string(record.name);
        
        int size = record.embedding_size;
        if (size <= 0 || size > 1024) size = 128; // Fallback an toàn
        
        u.embedding.assign(record.embedding, record.embedding + size);
        users.push_back(u);
    }
    ifs.close();
    return users;
}

bool Network_SaveUser(const UserInfo& u) {
    // 1. Lưu vào thẻ nhớ (Local)
    if (u.embedding.size() > 1024) return false; 
    UserDiskRecord record;
    memset(&record, 0, sizeof(UserDiskRecord));
    strncpy(record.id, u.id.c_str(), 63);
    strncpy(record.name, u.name.c_str(), 63);
    record.embedding_size = (int)u.embedding.size();
    std::copy(u.embedding.begin(), u.embedding.end(), record.embedding);
    
    xor_crypt((char*)&record, sizeof(UserDiskRecord)); 
    std::ofstream ofs(LOCAL_DB_FILE, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) return false;
    ofs.write((char*)&record, sizeof(UserDiskRecord));
    ofs.close();
    
    // 2. Đẩy lên Firebase (Cloud)
    // Lưu ý: Đẩy vào node "/users" thay vì tách biệt metadata
    json j;
    j[u.id] = { 
        {"name", u.name}, 
        {"created_at", time(nullptr)}
        // Bỏ comment dòng dưới nếu muốn đẩy cả vector mặt lên cloud (nặng data hơn)
        // , {"embedding", u.embedding} 
    };
    
    // Dùng PATCH để update node users mà không xóa người khác
    printf("[Net] Uploading user info for ID: %s\n", u.id.c_str());
    firebaseRequest("/users.json", "PATCH", j.dump());
    
    return true;
}

bool Network_SyncFromCloud(std::vector<UserInfo>& local_users) {
    // [FIX] Đường dẫn phải trùng với nơi SaveUser đẩy lên ("/users.json")
    std::string jsonStr = firebaseGet("/users.json");
    
    if (jsonStr.empty() || jsonStr == "null") return false;

    bool isChanged = false;
    try {
        json j = json::parse(jsonStr);
        std::vector<UserInfo> new_list;
        
        // Logic: Duyệt qua danh sách người dùng LOCAL
        // Nếu Server không có ID đó -> Xóa ở Local (Thu hồi quyền truy cập)
        // Nếu Server có ID đó nhưng khác Tên -> Đổi tên ở Local
        
        for (auto& u : local_users) {
            if (j.contains(u.id)) {
                std::string server_name = j[u.id]["name"];
                if (u.name != server_name) {
                    printf("[Sync] Rename: %s -> %s\n", u.name.c_str(), server_name.c_str());
                    u.name = server_name;
                    isChanged = true;
                }
                new_list.push_back(u);
            } else {
                // User có ở Local nhưng không có trên Cloud -> Đã bị xóa/Block
                printf("[Sync] Access Revoked: %s (ID: %s)\n", u.name.c_str(), u.id.c_str());
                isChanged = true;
            }
        }

        if (isChanged) {
            local_users = new_list;
            Network_RewriteStore(local_users); // Ghi đè lại file binary local
        }

    } catch (const std::exception& e) {
        printf("[Sync] Error parsing JSON: %s\n", e.what());
    }
    return isChanged;
}

void Network_SendLog(const std::string& id, const std::string& name) {
    // Debounce: Chỉ gửi log mỗi 30 giây cho cùng 1 người
    {
        std::lock_guard<std::mutex> lock(mtx_log);
        time_t now = time(nullptr);
        if (last_checkin.count(id) && (now - last_checkin[id] < 30)) return;
        last_checkin[id] = now;
    }

    time_t now = time(nullptr);
    char buf[40];
    strftime(buf, 40, "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    json j = { 
        {"id", id}, 
        {"name", name}, 
        {"timestamp", buf},
        {"timestamp_unix", now} // Thêm timestamp dạng số để dễ sort trên App/Web
    };
    
    printf("[Net] Sending Log: %s\n", name.c_str());
    firebaseRequest("/attendance.json", "POST", j.dump());
}