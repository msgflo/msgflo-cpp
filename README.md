# msgflo-cpp: C++ participant support for MsgFlo

[MsgFlo](https://github.com/the-grid/msgflo) is a distributed, polyglot FBP (flow-based-programming)
runtime. It integrates with other FBP tools like the [Flowhub](http://flowhub.io) visual programming IDE.
This library makes it easy to create MsgFlo participants in C++.

msgflo-cpp is written in C++11 and is built on top of [AMQP-CPP](https://github.com/CopernicaMarketingSoftware/AMQP-CPP),
[boost::asio](http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio.html) and [json11](https://github.com/dropbox/json11).

## Status

*Experimental*

* Basic support for setting up and running Participants with AMQP/RabbitMQ transport

## Usage

See [./examples/repeat.cpp](./examples/repeat.cpp)

    make
    ./build/repeat-cpp

## License

MIT, see [./LICENSE]
