[![Build Status](https://travis-ci.org/msgflo/msgflo-cpp.svg?branch=master)](https://travis-ci.org/msgflo/msgflo-cpp)
# msgflo-cpp: C++ participant support for MsgFlo

[MsgFlo](https://github.com/msgflo/msgflo) is a distributed, polyglot FBP (flow-based-programming)
runtime. It integrates with other FBP tools like the [Flowhub](http://flowhub.io) visual programming IDE.
This library makes it easy to create MsgFlo participants in C++.

msgflo-cpp is written in C++11 and is built on top of [AMQP-CPP](https://github.com/CopernicaMarketingSoftware/AMQP-CPP),
[boost::asio](http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio.html) and [json11](https://github.com/dropbox/json11).

## Status

*Working prototype*

* Basic Participant support, sends discover
* Supports AMQP/RabbitMQ and MQTT transports
* Not used in production yet

## Usage

See [./examples/repeat.cpp](./examples/repeat.cpp)

    mkdir build
    cmake ..
    make
    ./examples/repeat

## License

MIT, see [./LICENSE](./LICENSE)

## TODO

0.1

* Fix missing port identifier in process()
* Remove boost dependency
* AMQP: Implement NACK
