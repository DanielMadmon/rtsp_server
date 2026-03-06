TARGET0 = rtsp_server

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
LDLIBS += -L$(RKLIBS_LIB) -lrockit -lrockchip_mpp -lrkaiq -lrga 

CXXFLAGS += -O -g -fPIC -pthread -fmessage-length=0 -std=c++14 -Wall -Werror=format
CXXFLAGS += -DDYN_LOG  -DISP_HW_V30 -DRKPLATFORM -DUAPI2
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

CXXFILES8   = $(notdir $(wildcard ./src/luckfox_mpi/*.cpp))
CXXOBJS8    = $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(CXXFILES8))

CXXFILES9   = $(notdir $(wildcard ./src/generic_log/*.c))
CXXOBJS9    = $(patsubst %.c, $(OBJ_DIR)/%.o, $(CXXFILES9))

$(LIB_DIR)/$(TARGET0): $(CXXOBJS0) $(CXXOBJS1) $(CXXOBJS2) $(CXXOBJS3) $(CXXOBJS8) $(CXXOBJS9)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)


all: $(LIB_DIR)/$(TARGET0) 
	 @echo "rtsp_server build successful"

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
