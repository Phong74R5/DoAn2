CC = g++
CFLAGS = -Wall `pkg-config --cflags opencv4`
LIBS = -lbcm2835 -lpthread `pkg-config --libs opencv4`

# Danh sách các file nguồn
SRCS = main.cpp queue_helper.cpp lcd_driver.cpp tasks.cpp
# Tên file chạy
TARGET = app_camera

all:
	$(CC) -o $(TARGET) $(SRCS) $(CFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)

run:
	sudo ./$(TARGET)