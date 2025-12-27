#include "network_helper.h"
#include <iostream>
#include <fstream>
#include <curl/curl.h>
#include <mutex>
#include <map>
#include <ctime>
#include <cstring> 
#include <algorithm>
#include <sstream>
#include "json.hpp" 

using json = nlohmann::json;

// --- CẤU HÌNH ---
const std::string FIREBASE_HOST = "https://facedetect-7c1fe-default-rtdb.firebaseio.com"; 
const std::string LOCAL_DB_FILE = "userdata.dat"; 
const char ENCRYPTION_KEY = 0xAA; 

// Giả định MobileFaceNet output 100 float
const int EMBEDDING_VECTOR_LEN = 1000; 

struct UserDiskRecord {
    char id[64];
    char name[64];
    float embedding[5000]; // Đủ chứa 5 vectors * 128 = 640 floats
    int total_floats;      // Tổng số lượng float đã dùng
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

static std::string firebaseGet(std::string path) {
    CURL *curl = curl_easy_init();
    std::string readBuffer;
    if(curl) {
        std::string url = FIREBASE_HOST + path;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        CURLcode res = curl_easy_perform(curl);
        if(res != CURLE_OK) printf("[Net] GET Failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

static void firebaseRequest(std::string path, std::string method, std::string data = "") {
    CURL *curl = curl_easy_init();
    if(curl) {
        std::string url = FIREBASE_HOST + path;
        struct curl_slist *headers = NULL; 

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

        std::string dummy; 
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dummy);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        CURLcode res = curl_easy_perform(curl);
        if(res != CURLE_OK) printf("[Net] %s Failed: %s\n", method.c_str(), curl_easy_strerror(res));

        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

// --- CORE FUNCTIONS (UPDATED FOR MULTI-EMBEDDINGS) ---

void Network_RewriteStore(const std::vector<UserInfo>& users) {
    std::ofstream ofs(LOCAL_DB_FILE, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return;

    for (const auto& u : users) {
        UserDiskRecord record;
        memset(&record, 0, sizeof(UserDiskRecord));
        strncpy(record.id, u.id.c_str(), 63);
        strncpy(record.name, u.name.c_str(), 63);
        
        // Flatten: Gộp tất cả các vector con thành 1 mảng lớn
        int offset = 0;
        for(const auto& vec : u.embeddings) {
            if (offset + vec.size() <= 1024) {
                std::copy(vec.begin(), vec.end(), record.embedding + offset);
                offset += vec.size();
            }
        }
        record.total_floats = offset;

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
        
        // Un-Flatten: Cắt mảng lớn ra thành các vector 128 float
        u.embeddings.clear();
        int idx = 0;
        while (idx + EMBEDDING_VECTOR_LEN <= record.total_floats) {
            std::vector<float> vec;
            vec.assign(record.embedding + idx, record.embedding + idx + EMBEDDING_VECTOR_LEN);
            u.embeddings.push_back(vec);
            idx += EMBEDDING_VECTOR_LEN;
        }

        // Tương thích ngược: Nếu load file cũ (embedding đơn), coi như 1 vector
        if (u.embeddings.empty() && record.total_floats > 0) {
             std::vector<float> vec;
             vec.assign(record.embedding, record.embedding + record.total_floats);
             u.embeddings.push_back(vec);
        }

        users.push_back(u);
    }
    ifs.close();
    return users;
}

bool Network_SaveUser(const UserInfo& u) {
    // 1. Lưu vào thẻ nhớ (Local)
    UserDiskRecord record;
    memset(&record, 0, sizeof(UserDiskRecord));
    strncpy(record.id, u.id.c_str(), 63);
    strncpy(record.name, u.name.c_str(), 63);
    
    int offset = 0;
    for(const auto& vec : u.embeddings) {
        if (offset + vec.size() <= 1024) {
            std::copy(vec.begin(), vec.end(), record.embedding + offset);
            offset += vec.size();
        }
    }
    record.total_floats = offset;
    
    xor_crypt((char*)&record, sizeof(UserDiskRecord)); 
    std::ofstream ofs(LOCAL_DB_FILE, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) return false;
    ofs.write((char*)&record, sizeof(UserDiskRecord));
    ofs.close();
    
    // 2. Đẩy lên Firebase (Chỉ info, ko đẩy embedding để tiết kiệm)
    json j;
    j[u.id] = { 
        {"name", u.name}, 
        {"created_at", time(nullptr)}
    };
    
    printf("[Net] Uploading user info for ID: %s\n", u.id.c_str());
    firebaseRequest("/users.json", "PATCH", j.dump());
    
    return true;
}

bool Network_SyncFromCloud(std::vector<UserInfo>& local_users) {
    std::string jsonStr = firebaseGet("/users.json");
    if (jsonStr.empty() || jsonStr == "null") return false;

    bool isChanged = false;
    try {
        json j = json::parse(jsonStr);
        std::vector<UserInfo> new_list;
        
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
                printf("[Sync] Access Revoked: %s (ID: %s)\n", u.name.c_str(), u.id.c_str());
                isChanged = true;
            }
        }

        if (isChanged) {
            local_users = new_list;
            Network_RewriteStore(local_users); 
        }

    } catch (const std::exception& e) {
        printf("[Sync] Error parsing JSON: %s\n", e.what());
    }
    return isChanged;
}

void Network_SendLog(const std::string& id, const std::string& name) {
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
        {"timestamp_unix", now}
    };
    
    printf("[Net] Sending Log: %s\n", name.c_str());
    firebaseRequest("/attendance.json", "POST", j.dump());
}