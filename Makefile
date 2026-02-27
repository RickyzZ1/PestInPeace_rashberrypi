CXX=g++
CXXFLAGS=-O2 -std=c++17 -Wall -Iinclude
LIBS=-lgpiod -lpthread

SRC=src/main.cpp src/ltr559.cpp src/light.cpp src/camera.cpp src/uploader.cpp
TARGET=iot_app

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)
