
CXX_STD=c++11 # set to c++0x if using old GCC
CPP=g++

BUILD_DIR=$(shell echo `pwd`/build)
THIRDPARTY=$(shell echo `pwd`/thirdparty)
AMQPCPP=$(THIRDPARTY)/amqpcpp
AMQPCPP_VERSION=$(shell sed -n "s,.*VERSION[\t ]*=[\t ]*\(.*\),\1,p" $(AMQPCPP)/Makefile)
MSGFLO_SRC=$(wildcard src/*.cpp) thirdparty/json11/json11.cpp

CXXFLAGS=-I$(THIRDPARTY)/json11/ -I$(AMQPCPP)/install/include/ -I$(AMQPCPP)/examples/rabbitmq_tutorials
LDFLAGS=$(AMQPCPP)/install/lib/libamqpcpp.a.$(AMQPCPP_VERSION) $(EXTRA_LDFLAGS) -lboost_system -lmosquitto -pthread

all: repeat

dirs:
	mkdir -p $(BUILD_DIR)

amqpcpp:
	cd thirdparty/amqpcpp && make -j4 CPP=$(CPP) LD=$(CPP) PREFIX=./install && make install PREFIX=./install

repeat: dirs amqpcpp
	$(CPP) -std=$(CXX_STD) -o $(BUILD_DIR)/repeat-cpp $(MSGFLO_SRC) ./examples/repeat.cpp $(CXXFLAGS) -I./include $(LDFLAGS)
