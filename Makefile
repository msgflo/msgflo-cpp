
BUILD_DIR=$(shell echo `pwd`/build)
THIRDPARTY=$(shell echo `pwd`/thirdparty)
AMQPCPP=$(THIRDPARTY)/amqpcpp

CXXFLAGS=-I$(THIRDPARTY)/json11/ -I$(AMQPCPP)/install/include/ -I$(AMQPCPP)/examples/rabbitmq_tutorials
LDFLAGS=$(AMQPCPP)/install/lib/libamqpcpp.a.2.2.0 -lboost_system -pthread

all: repeat

dirs:
	mkdir -p $(BUILD_DIR)

amqpcpp:
	cd thirdparty/amqpcpp && make -j4 PREFIX=./install && make install PREFIX=./install

repeat: dirs amqpcpp
	$(CXX) -std=c++11 -o $(BUILD_DIR)/repeat-cpp ./examples/repeat.cpp $(CXXFLAGS) -I./src $(LDFLAGS)


