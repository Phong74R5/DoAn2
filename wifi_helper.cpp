#include "wifi_helper.h"
#include <cstdio>
#include <iostream>
#include <fstream>
#include <memory>
#include <array>
#include <algorithm> 
#include <sstream>
#include <map>
#include <cstdlib> // Cho system()

// ... (Giữ nguyên các hàm exec, Wifi_IsConnected, Wifi_GetIP, Wifi_Scan cũ của bạn) ...

std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    auto pipe_deleter = [](FILE* f) { pclose(f); };
    std::unique_ptr<FILE, decltype(pipe_deleter)> pipe(popen(cmd, "r"), pipe_deleter);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool Wifi_IsConnected() {
    std::string ip = Wifi_GetIP();
    return (ip.length() >= 7); 
}

std::string Wifi_GetIP() {
    std::string out = exec("hostname -I | awk '{print $1}'");
    if (!out.empty() && out[out.length()-1] == '\n') {
        out.erase(out.length()-1);
    }
    return out;
}

// ... (Giữ nguyên hàm compareWifi và Wifi_Scan cũ của bạn) ...

bool compareWifi(const WifiInfo &a, const WifiInfo &b) {
    return a.signal > b.signal; 
}

std::vector<WifiInfo> Wifi_Scan() {
    // ... (Giữ nguyên nội dung hàm Wifi_Scan như bạn đã gửi) ...
    // Copy code Wifi_Scan của bạn vào đây
    std::vector<WifiInfo> networks;
    std::map<std::string, int> unique_checker; 

    std::string output = exec("sudo iwlist wlan0 scan | egrep 'ESSID|Signal level'");
    std::stringstream ss(output);
    std::string line;
    int current_signal = -100; 
    
    while(std::getline(ss, line)) {
        if (line.find("Signal level") != std::string::npos) {
            size_t pos = line.find("Signal level=");
            if (pos != std::string::npos) {
                try { current_signal = std::stoi(line.substr(pos + 13)); } 
                catch (...) { current_signal = -100; }
            }
        }
        else if (line.find("ESSID:") != std::string::npos) {
            size_t first = line.find("\"");
            size_t last = line.find_last_of("\"");
            if (first != std::string::npos && last != std::string::npos) {
                std::string ssid = line.substr(first + 1, last - first - 1);
                if (!ssid.empty() && ssid.find("\\x00") == std::string::npos && unique_checker.find(ssid) == unique_checker.end()) {
                    WifiInfo w; w.ssid = ssid; w.signal = current_signal;
                    networks.push_back(w);
                    unique_checker[ssid] = 1; 
                }
            }
        }
    }
    std::sort(networks.begin(), networks.end(), compareWifi);
    if (networks.size() > 5) networks.resize(5);
    return networks;
}

// ... (Giữ nguyên hàm Wifi_Connect) ...

bool Wifi_Connect(std::string ssid, std::string password) {
    std::string cmd = "sudo nmcli dev wifi connect \"" + ssid + "\" password \"" + password + "\"";
    std::cout << "Connecting to " << ssid << "..." << std::endl;
    int result = system(cmd.c_str());
    if (result == 0) {
        std::cout << "Connected successfully!" << std::endl;
        return true;
    } else {
        std::cerr << "Connection failed." << std::endl;
        return false;
    }
}

// ========================================================
// [MỚI] THÊM HÀM NÀY ĐỂ CHECK INTERNET CHO TASK MONITOR
// ========================================================
bool Network_CheckInternet() {
    // Ping Google DNS (8.8.8.8)
    // -c 1: Chỉ gửi 1 gói tin
    // -W 1: Chờ tối đa 1 giây
    // > /dev/null 2>&1: Ẩn output, không in ra màn hình
    
    int result = system("ping -c 1 -W 1 8.8.8.8 > /dev/null 2>&1");
    
    // Nếu ping thành công, system trả về 0 -> Có mạng
    if (result == 0) return true;
    return false;
}