# Dự án: Camera hiển thị lên màn hình SPI LCD (Multi-threading C++)

Dự án này sử dụng **Raspberry Pi** để đọc dữ liệu từ **USB Webcam**, xử lý ảnh bằng **OpenCV** và hiển thị **thời gian thực** lên màn hình **TFT LCD 2.4 inch (Driver ILI9341)** qua giao tiếp **SPI**.

Chương trình được thiết kế theo mô hình **Multi-threading (Đa luồng)** giúp tách biệt:
- Luồng đọc Camera
- Luồng xử lý / AI Demo
- Luồng hiển thị LCD

→ Nhờ đó tận dụng tốt tài nguyên CPU và đạt hiệu suất cao hơn.

---

## 1. Yêu cầu phần cứng

- **Mạch xử lý**: Raspberry Pi 3 Model B/B+ hoặc Raspberry Pi 4  
- **Hệ điều hành**: Raspberry Pi OS (Legacy hoặc bản mới)  
- **Màn hình**: TFT LCD 2.4" hoặc 2.8"  
  - Giao tiếp SPI  
  - Driver ILI9341  
- **Camera**: USB Webcam bất kỳ (Logitech, Genius, v.v.)

---

## 2. Sơ đồ nối dây (Wiring Diagram)

> ⚠ **Lưu ý quan trọng:**  
> Sơ đồ sử dụng chuẩn chân **BCM (Broadcom)** của Raspberry Pi.  
> Hãy nối chính xác từng chân để tránh lỗi **màn hình trắng**.

| Chân LCD (ILI9341) | Chân Raspberry Pi (Vật lý) | BCM GPIO | Chức năng                             |
|--------------------|---------------------------|----------|---------------------------------------|
| VCC                | Pin 1 (3.3V)             | -        | Nguồn 3.3V                            |
| GND                | Pin 6                    | -        | Mass (GND)                            |
| CS                 | Pin 24                   | GPIO 8   | Chip Select (CE0)                     |
| RESET              | Pin 18                   | GPIO 24  | Khởi động lại màn hình               |
| DC / RS            | Pin 22                   | GPIO 25  | Data / Command Select                 |
| SDI / MOSI         | Pin 19                   | GPIO 10  | Truyền dữ liệu (Master Out)          |
| SCK / CLK          | Pin 23                   | GPIO 11  | Xung nhịp (Clock)                    |
| LED                | Pin 16                   | GPIO 23  | Đèn nền (Backlight) – **BẮT BUỘC**   |             |

> 💡 Nếu bạn **đổi chân nối**, hãy cập nhật lại trong file `config.h`.

---

## 3. Cài đặt thư viện (Software Setup)

Mở **Terminal** trên Raspberry Pi và chạy lần lượt các bước sau.

### Bước 1: Cập nhật hệ thống

```bash
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config -y
```

### Bước 2: Cài đặt thư viện OpenCV (C++ Development)

Thư viện này cần để **đọc camera** và **xử lý ma trận ảnh**.

```bash
sudo apt-get install libopencv-dev -y
```

### Bước 3: Cài đặt thư viện BCM2835 (Driver SPI tốc độ cao)

Thư viện này giúp điều khiển **GPIO** và **SPI low-level** với tốc độ cao.

```bash
# 1. Tải về
wget http://www.airspayce.com/mikem/bcm2835/bcm2835-1.73.tar.gz

# 2. Giải nén
tar zxvf bcm2835-1.73.tar.gz

# 3. Vào thư mục và biên dịch
cd bcm2835-1.73
./configure
make
sudo make check
sudo make install

# 4. Quay lại thư mục trước
cd ..
```

### Bước 4: Bật giao tiếp SPI trên Raspberry Pi

```bash
sudo raspi-config
```

- Chọn: `Interface Options` → `SPI` → `Yes`  
- Sau đó khởi động lại:

```bash
sudo reboot
```

---

## 4. Biên dịch và chạy chương trình

### Biên dịch (Build)

Trong thư mục project, chạy:

```bash
make
```

Sau khi biên dịch thành công, sẽ tạo ra file thực thi:

```text
app_camera
```

### Chạy (Run)

```bash
make run
```

### Dọn dẹp (Clean)

Xóa file biên dịch cũ:

```bash
make clean
```

---

## 5. Khắc phục sự cố (Troubleshooting)

| Hiện tượng                                | Nguyên nhân                                 | Cách khắc phục                                                                 |
|-------------------------------------------|---------------------------------------------|---------------------------------------------------------------------------------|
| `bcm2835_init failed`                     | Chạy chương trình không có quyền root       | Thêm `sudo` trước lệnh chạy: `sudo ./app_camera`                               |
| `opencv2/opencv.hpp: No such file`       | Chưa cài thư viện OpenCV Dev                | Cài lại OpenCV ở **Bước 2**                                                    |
| Màn hình trắng xóa                        | Sai dây nối hoặc chưa `RESET` đúng          | Kiểm tra lại dây `DC` (Pin 22) và `RESET` (Pin 18)                             |
| Màn hình tối đen                          | Đèn nền chưa bật                             | Kiểm tra dây `LED` nối Pin 16 (GPIO 23), code đã bật chân này lên `HIGH`       |
| Hình ảnh bị ngược / lật gương            | Sai cấu hình hướng quét (Scan Direction)    | Mở `lcd_driver.cpp`, trong hàm `lcd_init_full`, tìm lệnh gửi `0x36`; thử đổi giá trị: `0x28`, `0xE8`, `0x48` hoặc `0x88` |
| Hình ảnh bị sai màu (Đỏ thành xanh, v.v.) | Sai định dạng màu (BGR <-> RGB)             | Trong `tasks.cpp` đã có đoạn chuyển đổi sang RGB565; nếu vẫn sai kiểm tra lại công thức chuyển đổi |

---

## 6. Cấu trúc thư mục dự án

```text
.
├── main.cpp          # File chính, khởi tạo phần cứng và tạo các luồng (threads)
├── tasks.cpp         # Logic 3 tác vụ: Camera, AI Demo, LCD Display
├── lcd_driver.cpp    # Driver SPI low-level cho màn hình ILI9341
├── queue_helper.cpp  # Hàng đợi chia sẻ dữ liệu giữa các luồng (thread-safe)
├── config.h          # Cấu hình GPIO, độ phân giải màn hình, tham số hệ thống
├── Makefile          # Script build nhanh bằng lệnh `make`
└── README.md         # Tài liệu mô tả dự án (file này)
```
