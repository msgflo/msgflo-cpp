#include <iostream>
#include <algorithm>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <boost/asio/io_service.hpp>
#include "amqpcpp.h"
#include "mosquitto.h"
#include "mqtt_support.h"
#include "msgflo.h"

using namespace std;
using namespace trygvis::mqtt_support;

namespace msgflo {

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

class MsgFloAmqpHandler : public AMQP::TcpHandler {
public:
    bool connected;

protected:

    void onConnected(AMQP::TcpConnection *connection) {
        connected = true;
    }

    void onError(AMQP::TcpConnection *connection, const char *message) {
        connected = false;
    }

    void onClosed(AMQP::TcpConnection *connection) {
        connected = false;
    }

    void monitor(AMQP::TcpConnection *connection, int fd, int flags) {
    }
};

class AmqpEngine final : public Engine, public std::enable_shared_from_this<AmqpEngine> {
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

        for (const auto &port : participant->definition()->inports) {
            setupInport(port);
        }
        for (const auto &port : participant->definition()->outports) {
            setupOutport(port);
        }

        sendParticipant();

        ioService.run();
    }

    bool connected() override {
        return handler.connected;
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

class MosquittoEngine final : public Engine {
public:
    MosquittoEngine(Participant *participant, const string &host, const int port, const int keep_alive,
                    const string &client_id, const bool clean_session) :
            _participant(participant), client(host, port, keep_alive, client_id, clean_session) {
        client.connect();
    }

    ~MosquittoEngine() {
        client.disconnect();
    }

    void send(std::string port, Message &msg) override {
    }

    void ack(Message msg) override {
    }

    void nack(Message msg) override {
    }

    bool connected() override {
        return client.connected();
    }

private:
    mqtt_client<trygvis::mqtt_support::mqtt_client_personality::threaded> client;
    const Participant *_participant;
};

shared_ptr<Engine> createEngine(Participant *participant, const std::string &url) {
    if (boost::starts_with(url, "mqtt://")) {
        string host, username, password;
        int port = 1883;
        int keep_alive = 180;
        string client_id;
        bool clean_session = true;

        string s = url.substr(7);
        cout << "s: " << s << endl;
        auto i_up = s.find('@');

        if (i_up >= 0) {
            string up = s.substr(0, i_up);
            cout << "up: " << up << endl;

            auto i_u = up.find(':');

            if (i_u >= 0) {
                username = up.substr(0, i_u);
                password = up.substr(i_u + 1);
            }
            s = s.substr(i_up + 1);
            cout << "s: " << s << endl;
        }

        host = s;
//        cout << "host: " << host << endl;
//        cout << "username: " << username << endl;
//        cout << "password: " << password << endl;
//        cout << "client_id: " << client_id << endl;
//        cout << "keep_alive: " << keep_alive << endl;
//        cout << "clean_session: " << clean_session << endl;

        return make_shared<MosquittoEngine>(participant, host, port, keep_alive, client_id, clean_session);
    } else if (boost::starts_with(url, "amqp://")) {
        return make_shared<AmqpEngine>(participant, url);
    }

    throw std::runtime_error("Unsupported URL scheme: " + url);
}

} // namespace msgflo;
