TARGET0 = rtsp_server
TARGET1 = rtsp_pusher
TARGET2 = rtsp_h264_file
TARGET3 = rtsp_h265_file
TARGET4 = rtsp_aac_file

BUILD_DIR = ./build
LIB_DIR = $(BUILD_DIR)/bin
OBJ_DIR = $(BUILD_DIR)/objs

CROSS_COMPILE = arm-rockchip830-linux-uclibcgnueabihf-
#CROSS_COMPILE = /opt/ivot/arm-ca9-linux-gnueabihf-6.5/bin/arm-ca9-linux-gnueabihf-
CXX = $(CROSS_COMPILE)g++

CXXFLAGS =  -I./src
CXXFLAGS += -I./src/3rdpart
CXXFLAGS += -I./src/bsalgo
CXXFLAGS += -I./src/net
CXXFLAGS += -I./src/xop
CXXFLAGS += -I./src/luckfox_mpi
CXXFLAGS += -I./src/generic_log
#Rockchip libs
RKLIBS_ROOT = ./rk_libs
RKLIBS_INCLUDE := $(shell find $(RKLIBS_ROOT) -type d)
INC_FLAGS := $(addprefix -I,$(RKLIBS_INCLUDE))

#RKLIBS cxxflags
CXXFLAGS += -I$(INC_FLAGS)

RKLIBS_LIB = $(RKLIBS_ROOT)/uclibc
LDLIBS += -L$(RKLIBS_LIB) -lrockiva -lsample_comm -lrockit -lrockchip_mpp -lrkaiq -lrga

CXXFLAGS += -O -g -fPIC -pthread -fmessage-length=0 -std=c++14 -Wall -Werror=format
CXXFLAGS += -DDYN_LOG
LDFLAGS = -ldl -lm -lrt -lpthread

$(shell mkdir -p $(LIB_DIR) $(OBJ_DIR))

$(OBJ_DIR)/%.o: ./src/bsalgo/%.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: ./src/net/%.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: ./src/xop/%.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: ./example/%.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: ./src/luckfox_mpi/%.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(OBJ_DIR)/%.o: ./src/generic_log/%.c
	$(CXX) -c $(CXXFLAGS) $< -o $@

CXXFILES0   = $(notdir $(wildcard ./src/net/*.cpp))
CXXOBJS0    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES0))

CXXFILES1   = $(notdir $(wildcard ./src/xop/*.cpp))
CXXOBJS1    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES1))

CXXFILES2   = $(notdir $(wildcard ./src/bsalgo/*.cpp))
CXXOBJS2    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES2))

CXXFILES3   = $(notdir $(wildcard ./example/rtsp_server.cpp))
CXXOBJS3    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES3))

CXXFILES4   = $(notdir $(wildcard ./example/rtsp_pusher.cpp))
CXXOBJS4    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES4))

CXXFILES5   = $(notdir $(wildcard ./example/rtsp_h264_file.cpp))
CXXOBJS5    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES5))

CXXFILES6   = $(notdir $(wildcard ./example/rtsp_h265_file.cpp))
CXXOBJS6    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES6))

CXXFILES7   = $(notdir $(wildcard ./example/rtsp_aac_file.cpp))
CXXOBJS7    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES7))

CXXFILES8   = $(notdir $(wildcard ./src/luckfox_mpi/*.cpp))
CXXOBJS8    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES8))

CXXFILES9   = $(notdir $(wildcard ./src/generic_log/*.c))
CXXOBJS9    = $(patsubst %.c, $(OBJ_DIR)/%.o, $(CXXFILES9))

$(LIB_DIR)/$(TARGET0): $(CXXOBJS0) $(CXXOBJS1) $(CXXOBJS2) $(CXXOBJS3) $(CXXOBJS8) $(CXXOBJS9)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(LIB_DIR)/$(TARGET1): $(CXXOBJS0) $(CXXOBJS1) $(CXXOBJS2) $(CXXOBJS4) $(CXXOBJS8) $(CXXOBJS9)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(LIB_DIR)/$(TARGET2): $(CXXOBJS0) $(CXXOBJS1) $(CXXOBJS2) $(CXXOBJS5) $(CXXOBJS8) $(CXXOBJS9)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(LIB_DIR)/$(TARGET3): $(CXXOBJS0) $(CXXOBJS1) $(CXXOBJS2) $(CXXOBJS6) $(CXXOBJS8) $(CXXOBJS9)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(LIB_DIR)/$(TARGET4): $(CXXOBJS0) $(CXXOBJS1) $(CXXOBJS2) $(CXXOBJS7) $(CXXOBJS8) $(CXXOBJS9)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

all: $(LIB_DIR)/$(TARGET0) $(LIB_DIR)/$(TARGET1) \
	 $(LIB_DIR)/$(TARGET2) $(LIB_DIR)/$(TARGET3) \
	 $(LIB_DIR)/$(TARGET4)
	 @echo "rtsp_server build successful"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
