#include <iostream>
#include <algorithm>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <boost/asio/io_service.hpp>
#include "amqpcpp.h"
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
        static_cast<void>(connection);
        connected = true;
    }

    void onError(AMQP::TcpConnection *connection, const char *message) {
        static_cast<void>(connection);
        static_cast<void>(message);
        connected = false;
    }

    void onClosed(AMQP::TcpConnection *connection) {
        static_cast<void>(connection);
        connected = false;
    }

    void monitor(AMQP::TcpConnection *connection, int fd, int flags) {
        static_cast<void>(connection);
        static_cast<void>(fd);
        static_cast<void>(flags);
    }
};

class AmqpEngine final : public Engine, public std::enable_shared_from_this<AmqpEngine> {

public:
    AmqpEngine(Participant *p, const string &url)
        : Engine()
        , handler()
        , connection(&handler, AMQP::Address(url))
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
                static_cast<void>(redelivered);

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

    void ack(const Message &msg) override {
        channel.ack(msg.deliveryTag);
    }
    void nack(const Message &msg) override {
        static_cast<void>(msg);
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
            _listener(), _participant(participant), client(&_listener, host, port, keep_alive, client_id, clean_session) {
        client.connect();
    }

    ~MosquittoEngine() {
        client.disconnect();
    }

    void send(string port, Message &msg) override {
        static_cast<void>(port);
        static_cast<void>(msg);
    }

    void ack(const Message &msg) override {
        static_cast<void>(msg);
    }

    void nack(const Message &msg) override {
        static_cast<void>(msg);
    }

    bool connected() override {
        return client.connected();
    }

private:
    class listener : public mqtt_event_listener {
    public:
        listener() {
            cerr << "woot" << endl;
        }

        virtual void on_msg(const string &str) override {
            cerr << "mqtt: " << str << endl;
        }
    };

    listener _listener;
    const Participant *_participant;
    mqtt_client<trygvis::mqtt_support::mqtt_client_personality::threaded> client;
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

        if (i_up != string::npos) {
            string up = s.substr(0, i_up);
            cout << "up: " << up << endl;

            auto i_u = up.find(':');

            if (i_u != string::npos) {
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

} // namespace msgflo
