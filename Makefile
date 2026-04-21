CXX=g++
CXXFLAGS=-O2 -std=c++17 -Wall -Iinclude
LIBS=-lgpiod -lpthread

SRC=src/main.cpp src/ltr559.cpp src/bme280.cpp src/mcp3008.cpp src/soil.cpp src/light.cpp src/spray.cpp src/camera.cpp src/uploader.cpp src/upload_queue.cpp src/capture_lux.cpp src/capture_cleanup.cpp src/capture_env.cpp src/capture_round_log.cpp
TARGET=iot_app

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)
