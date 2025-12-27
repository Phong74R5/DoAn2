#ifndef WIFI_HELPER_H
#define WIFI_HELPER_H

#include <string>
#include <vector>

struct WifiInfo {
    std::string ssid;
    int signal;
};

// Các hàm cũ
bool Wifi_IsConnected();      // Chỉ check IP local
std::string Wifi_GetIP();
std::vector<WifiInfo> Wifi_Scan();
bool Wifi_Connect(std::string ssid, std::string password);

// [NEW] Thêm dòng này để Main App dùng
bool Network_CheckInternet(); // Check ping Google

#endif