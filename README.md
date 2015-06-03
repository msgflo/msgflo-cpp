# msgflo-cpp: C++ participant support for MsgFlo

[MsgFlo](https://github.com/the-grid/msgflo) is a distributed, polyglot FBP (flow-based-programming)
runtime. It integrates with other FBP tools like the [Flowhub](http://flowhub.io) visual programming IDE.
This library makes it easy to create MsgFlo participants in C++.

## Status

*Experiemental*

* Basic support for setting up and running Participants with AMQP/RabbitMQ transport

## Usage

See [./examples/repeat.cpp](./examples/repeat.cpp)

    make
    ./build/repeat-cpp
