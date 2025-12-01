# Tên file chạy
TARGET = app_camera

# Trình biên dịch
CC = g++

# Cờ biên dịch (Headers)
CFLAGS = -Wall $(shell pkg-config --cflags opencv4)

# Thư viện liên kết (QUAN TRỌNG: Thêm -lcurl vào đây)
LIBS = -lbcm2835 -lpthread -lcurl $(shell pkg-config --libs opencv4)

# Danh sách file nguồn
SRCS = main.cpp queue_helper.cpp lcd_driver.cpp network_helper.cpp tasks.cpp

# Lệnh build
all:
	$(CC) -o $(TARGET) $(SRCS) $(CFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)

run:
	sudo ./$(TARGET)
