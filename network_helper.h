#ifndef NETWORK_HELPER_H
#define NETWORK_HELPER_H

#include <string>
#include <vector>
#include <mutex>

struct UserInfo {
    std::string id;
    std::string name;
    // THAY ĐỔI: Lưu danh sách các embedding (5 góc) thay vì 1 cái trung bình
    std::vector<std::vector<float>> embeddings; 
};

std::vector<UserInfo> Network_LoadDatabase();
bool Network_SaveUser(const UserInfo& newUser);
bool Network_SyncFromCloud(std::vector<UserInfo>& local_users);
void Network_SendLog(const std::string& id, const std::string& name);

#endif