#include "msgflo.h"

#include <iostream>
#include <thread>
#include <boost/asio/io_service.hpp>
#include "amqpcpp.h"
#include "mqtt_support.h"

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

class AmqpEngine final : public Engine {

public:
    AmqpEngine(Participant *p, const string &url)
        : Engine()
        , handler()
        , connection(&handler, AMQP::Address(url))
        , channel(&connection)
        , participant(p)
    {
        channel.setQos(1); // TODO: is this prefech?
        setEngine(participant, this);

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
        std::string data = json11::Json(*participant->definition()).dump();
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

private:
    boost::asio::io_service ioService;
    MsgFloAmqpHandler handler;
    AMQP::TcpConnection connection;
    AMQP::TcpChannel channel;
    Participant *participant;
};

// We're all into threads
using msg_flo_mqtt_client = mqtt_client<trygvis::mqtt_support::mqtt_client_personality::threaded>;

class MosquittoEngine final : public Engine, protected mqtt_event_listener {
public:
    MosquittoEngine(Participant *participant, const string &host, const int port, const int keep_alive,
                    const string &client_id, const bool clean_session) :
            _participant(participant), client(this, host, port, keep_alive, client_id, clean_session) {
        setEngine(participant, this);

        client.connect();
    }

    virtual ~MosquittoEngine() {
    }

    void send(string port, Message &msg) override {
        auto queue = Definition::queueForPort(_participant->definition()->outports, port);

        if(queue.empty()) {
            throw std::domain_error("No such port: " + port);
        }

        client.publish(nullptr, queue, 0, false, msg.json.dump());
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

protected:
    virtual void on_msg(const string &msg) override {
        cout << "mqtt: " << msg << endl;
    }

    virtual void on_connect(int rc) override {
        auto d = _participant->definition();
        string data = json11::Json(*d).dump();
        cout << "data: " << data << endl;
        client.publish(nullptr, "fbp", 0, false, data);

        for (auto &p : d->inports) {
            client.subscribe(nullptr, p.queue, 0);
        }
    }

private:
    const Participant *_participant;
    msg_flo_mqtt_client client;
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
            } else {
                username = up;
            }
            cout << "username: " << username << endl;
            cout << "password: " << password << endl;

            s = s.substr(i_up + 1);
            cout << "s: " << s << endl;
        }

        auto i_q = s.find('?');

        if (i_q != string::npos) {
            host = s.substr(0, i_q);
            s = s.substr(i_q + 1);
            cout << "s: " << s << endl;

            while(!s.empty()) {
                auto i_amp = s.find('&');

                string kv;
                if (i_amp == string::npos) {
                    kv = s;
                    s = "";
                } else {
                    kv = s.substr(0, i_amp);
                }
                cout << "kv: " << kv << endl;

                auto i_eq = kv.find('=');

                string key, value;
                if (i_eq != string::npos) {
                    key = kv.substr(0, i_eq);
                    value = kv.substr(i_eq + 1);
                } else {
                    key = kv;
                }

                if (key == "keepAlive") {
                    try {
                        auto v = stoul(value);
                        if (v > INT_MAX) {
                            throw invalid_argument("too big");
                        }
                        keep_alive = static_cast<int>(v);
                    } catch (invalid_argument &e) {
                        throw invalid_argument("Bad keepAlive argument, must be a number greater than zero.");
                    } catch (out_of_range &e) {
                        throw invalid_argument("Bad keepAlive argument, must be a number greater than zero.");
                    }
                } else if (key == "clientId") {
                    client_id = value;
                } else if (key == "cleanSession") {
                    clean_session = !(value == "0" || value == "no" || value == "false");
                } else {
                    // ignore unknown keys
                }

                if (i_amp == string::npos) {
                    break;
                }
                s = s.substr(i_amp + 1);
                cout << "s: " << s << endl;
            }
        } else {
            host = s;
        }
        cout << "host: " << host << endl;

        cout << "client_id: " << client_id << endl;
        cout << "keep_alive: " << keep_alive << endl;
        cout << "clean_session: " << clean_session << endl;

        return make_shared<MosquittoEngine>(participant, host, port, keep_alive, client_id, clean_session);
    } else if (boost::starts_with(url, "amqp://")) {
        return make_shared<AmqpEngine>(participant, url);
    }

    throw std::runtime_error("Unsupported URL scheme: " + url);
}

} // namespace msgflo
