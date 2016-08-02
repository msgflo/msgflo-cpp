// cd thirdparty/amqpcpp && make install PREFIX=./install; cd -
// g++ -std=c++11 -o ./spec/fixtures/repeat-cpp ./spec/fixtures/repeat.cpp -I./thirdparty/json11/ -I./thirdparty/amqpcpp/install/include/ -I./thirdparty/amqpcpp/examples/rabbitmq_tutorials/  ./thirdparty/amqpcpp/install/lib/libamqpcpp.a.2.2.0 -lboost_system -pthread

#include <iostream>
#include <algorithm>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <boost/asio/io_service.hpp>
#include "amqpcpp.h"
#include "msgflo.h"

using namespace std;

namespace msgflo {

class MsgFloAmqpHandler : public AMQP::TcpHandler {
    virtual void onConnected(AMQP::TcpConnection *connection) {
    }

    virtual void onError(AMQP::TcpConnection *connection, const char *message) {
    }

    virtual void onClosed(AMQP::TcpConnection *connection) {}

    virtual void monitor(AMQP::TcpConnection *connection, int fd, int flags) {
    }
};

class AmqpEngine : public Engine, public std::enable_shared_from_this<AmqpEngine> {
    friend class Participant;

public:
    AmqpEngine(Participant *p, const string &url)
        : Engine()
        , handler()
        , connection(&handler, AMQP::Address(""))
        , channel(&connection)
        , participant(p)
    {
//        handler.connect(o.host, o.port);
        channel.setQos(1); // TODO: is this prefech?
        setEngine(participant, shared_from_this());
    }

    void start() override {

        for (const auto &p : participant->definition()->inports ) {
            setupInport(p);
        }
        for (const auto &p : participant->definition()->outports ) {
            setupOutport(p);
        }

        sendParticipant();

        ioService.run();
    }

    void stop() override {

    }

private:
    void sendParticipant() {
        std::string data = json11::Json(participant->definition()).dump();
        AMQP::Envelope env(data);
        channel.publish("", "fbp", env);
    }

    void setupOutport(const Definition::Port &p) {
        channel.declareExchange(p.queue, AMQP::fanout);
    }

    void setupInport(const Definition::Port &p) {

        channel.declareQueue(p.queue, AMQP::durable);
        channel.consume(p.queue).onReceived(
            [p, this](const AMQP::Message &message,
                       uint64_t deliveryTag,
                       bool redelivered)
            {
                const auto body = message.message();
                std::cout<<" [x] Received "<<body<<std::endl;
                Message msg;
                msg.deliveryTag = deliveryTag;
                std::string err;
                msg.json = json11::Json::parse(body, err);
                process(this->participant, p.id, msg);
            });

    }

public:
    // Interfaces used by Participant
    void send(std::string port, Message &msg) override {
        const std::string exchange = Definition::queueForPort(participant->definition()->outports, port);
        const std::string data = msg.json.dump();
        AMQP::Envelope env(data);
        std::cout <<" Sending on " << exchange << " :" << data << std::endl;
        channel.publish(exchange, "", env);
    }

    void ack(Message msg) override {
        channel.ack(msg.deliveryTag);
    }
    void nack(Message msg) override {
        // channel.nack(msg.deliveryTag); // FIXME: implement
    }


private:
    boost::asio::io_service ioService;
    MsgFloAmqpHandler handler;
    AMQP::TcpConnection connection;
    AMQP::TcpChannel channel;
    Participant *participant;
};

void Participant::send(std::string port, Message &msg)
{
    if (!_engine) return;
    _engine->send(port, msg);
}

void Participant::ack(Message msg) {
    if (!_engine) return;
    _engine->ack(msg);
}

void Participant::nack(Message msg) {
    if (!_engine) return;
    _engine->nack(msg);
}

shared_ptr<Engine> createEngine(Participant *participant, const std::string &url) {
    return make_shared<AmqpEngine>(participant, url);
}

} // namespace msgflo;
