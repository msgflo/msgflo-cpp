#include "msgflo.h"

#include <iostream>
#include <thread>
#include "amqpcpp.h"
#include "amqpcpp/libev.h"
#include "mqtt_support.h"

using namespace std;
using namespace trygvis::mqtt_support;

namespace msgflo {

std::string random_string( size_t length )
{
    auto randchar = []() -> char
    {
        const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[ rand() % max_index ];
    };
    std::string str(length,0);
    std::generate_n( str.begin(), length, randchar );
    return str;
}

int64_t millis_monotonic(void)
{
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    const int64_t ms = (spec.tv_sec*1000) + round(spec.tv_nsec / 1.0e6);
    return ms;
}

std::string string_to_upper_copy(const std::string &str) {
    std::string ret;
    ret.resize(str.size());
    for (uint i=0; i<str.size(); i++) {
        ret[i] = toupper(str[i]);
    }
    return ret;
}

bool string_starts_with(const std::string &str, const std::string &prefix) {
    return str.substr(0, prefix.size()) == prefix;
}

class DiscoveryMessage {
public:
    DiscoveryMessage(const Definition &def)
        : definition(def) {
    }

    json11::Json to_json() const {
        using namespace json11;

        return Json::object {
                {"protocol",  "discovery"},
                {"command",  "participant"},
                {"payload", definition.to_json() },
        };
    }

private:
    Definition definition;
};

void defaultMessageHandler(msgflo::Message *msg) {
    cout << "Warning: No message handler defined for msgflo::Participant" << endl;
}

template<typename Engine_t>
struct ParticipantRegistrationT : public Participant {
    Engine_t *engine;
    const std::vector<Definition::Port> inports;
    const std::vector<Definition::Port> outports;
    const string id;
    MessageHandler handler;
    const DiscoveryMessage discoveryMessage;

    ParticipantRegistrationT(Engine_t *engine, const Definition &definition)
        : engine(engine)
        , inports(definition.inports)
        , outports(definition.outports)
        , id(generateId(definition))
        , handler(defaultMessageHandler)
        , discoveryMessage(definition)
    {}

    void onMessage(const MessageHandler &h) {
        handler = h;
    }

    virtual void send(std::string port, const json11::Json &json) override {
        send(port, json.dump());
    }

    virtual void send(std::string port, const std::string &string) override {
        send(port, string.c_str(), string.size());
    }

    virtual void send(std::string port, const char *data, uint64_t len) override {
        engine->send(this, port, data, len);
    }

    const Definition::Port *findOutPort(const string &id) const {
        for (auto &p: outports) {
            if (p.id == id) {
                return &p;
            }
        }
        return nullptr;
    }

    static string generateId(const Definition &d) {
        return d.role + std::to_string(rand());
    }
};

class AbstractMessage : public Message {
protected:
    AbstractMessage(const char *data, const uint64_t len, const std::string &port)
        : _data(data)
        , _len(len)
        , _port(port)
    {}

    virtual ~AbstractMessage() {};

    const char *_data;
    const uint64_t _len;
    const std::string _port;

public:
    virtual void data(const char **data, uint64_t *len) override {
        *data = this->_data;
        *len = this->_len;
    }

    virtual std::string port() override {
        return _port;
    }

    virtual std::string asString() override {
        string str(_data, _len);
        return str;
    }

    virtual json11::Json asJson() override {
        string err;
        string str(_data, _len);
        auto x = json11::Json::parse(str, err);
        if (!err.empty()) {
            cerr << "_data=" << _data << endl;
            cerr << "_len=" << _len << endl;
            throw domain_error("Could not parse JSON body: " + err + ", payload: " + str);
        }
        return x;
    }
};

template<typename EngineType>
class AbstractEngine {
protected:
    using ParticipantRegistration = ParticipantRegistrationT<EngineType>;

    Definition validateDefinitionFromUser(const Definition &definition) {
        Definition d(definition);

        if (d.id.size() == 0) {
            d.id = d.role + "-" + random_string(8);
        }

        for (auto &port : d.inports) {
            if (port.queue.empty()) {
                port.queue = generateQueueName(definition, port);
            }
        }
        for (auto &port : d.outports) {
            if (port.queue.empty()) {
                port.queue = generateQueueName(definition, port);
            }
        }

        return d;
    }

    virtual string generateQueueName(const Definition &d, const Definition::Port &) = 0;

    vector<ParticipantRegistration> registrations;
};

// C-style subclassing
// used to pass context for libev timer callback
struct EvTimerWrapper {

public:
    struct ev_timer timer;
    std::function<void (void)> callback;
};

static void timeout_cb(struct ev_loop *loop, ev_timer *timer, int revent) {
    EvTimerWrapper *wrapper = (EvTimerWrapper *)timer;
    if (wrapper->callback) {
        wrapper->callback();
    }
}

class AmqpEngine final : public Engine, protected AbstractEngine<AmqpEngine> {

    struct AmqpMessage final : public AbstractMessage {
        AmqpMessage(AMQP::Channel &channel, uint64_t deliveryTag, const AMQP::Message &m, const std::string &p)
            : AbstractMessage(m.body(), m.bodySize(), p)
            , _deliveryTag(deliveryTag)
            ,channel(channel)
        {
        }

        uint64_t _deliveryTag;
        AMQP::Channel &channel;

        virtual void ack() override {
            channel.ack(_deliveryTag);
        }

        virtual void nack() override {
            channel.reject(_deliveryTag);
        }
    };

public:
    AmqpEngine(const string &url, EngineConfig config)
        : Engine()
        , loop(EV_DEFAULT)
        , handler(loop)
        , connection(&handler, AMQP::Address(url))
        , channel(&connection)
        , discoveryPeriod(config.discoveryPeriod/3)
    {
        channel.setQos(1); // TODO: is this prefech?

        channel.onReady([&]() {
            connected = true;
            for(auto &r: registrations) {
                for (const auto &port : r.inports) {
                    setupInPort(r, port);
                }
                for (const auto &port : r.outports) {
                    setupOutPort(port);
                }

                sendDiscoveryMessage(r);
            }
        });
    }

    virtual Participant *registerParticipant(const Definition &definition) override {
        Definition d = validateDefinitionFromUser(definition);
        registrations.emplace_back(this, d);
        return &registrations[registrations.size() - 1];
    }

    virtual void launch() override {
        discoveryTimer.callback = [this]() {
            if (not connected) {
                return;
            }
            for(auto &r: registrations) {
                this->sendDiscoveryMessage(r);
            }
        };
        ev_timer_init(&discoveryTimer.timer, timeout_cb, discoveryPeriod, discoveryPeriod);
        ev_timer_start(loop, &discoveryTimer.timer);
        ev_run(loop, 0);
    }

protected:
    string generateQueueName(const Definition &d, const Definition::Port &port) override {
        return d.role + "." + string_to_upper_copy(port.id);
    }

private:
    void sendDiscoveryMessage(const ParticipantRegistration &r) {
        string data = json11::Json(r.discoveryMessage).dump();
        AMQP::Envelope env(data);
        channel.publish("", "fbp", env);
    }

    void setupOutPort(const Definition::Port &p) {
        channel.declareExchange(p.queue, AMQP::fanout);
    }

    void setupInPort(const ParticipantRegistration &r, const Definition::Port &port) {
        channel.declareQueue(port.queue, AMQP::durable);
        channel.consume(port.queue).onReceived(
            [r, this, port](const AMQP::Message &message,
                      uint64_t deliveryTag,
                      bool redelivered) {
                AmqpMessage msg(channel, deliveryTag, message, port.id);
                r.handler(&msg);
            });
    }

public:

    void send(const ParticipantRegistration *r, const string &portName, const char *data, uint64_t size) {
        auto p = r->findOutPort(portName);

        if (p == nullptr) {
            throw domain_error("Unknown out port: " + portName);
        }

        AMQP::Envelope env(data, size);
        cout << " Sending on id=" << p->id << ", queue=" << p->queue << endl;
        channel.publish(p->queue, "", env);
    }

private:
    struct ev_loop *loop;
    AMQP::LibEvHandler handler;
    AMQP::TcpConnection connection;
    AMQP::TcpChannel channel;
    int64_t discoveryPeriod;
    EvTimerWrapper discoveryTimer;
    bool connected = false;
};

using msg_flo_mqtt_client = mqtt_client<trygvis::mqtt_support::mqtt_client_personality::polling>;

class MosquittoEngine final : public Engine, protected mqtt_event_listener, protected AbstractEngine<MosquittoEngine> {

    struct MosquittoMessage final : public AbstractMessage {
        MosquittoMessage(const struct mosquitto_message *m, bool d, const std::string &p)
            : AbstractMessage(static_cast<char *>(m->payload), static_cast<uint64_t>(m->payloadlen), p)
            , _mid(m->mid)
            , _debugOutput(d)
        {

        }

        int _mid;
        bool _debugOutput = false;

        virtual void ack() override {
            if (_debugOutput) {
                cerr << "MosquittoMessage.ack() is currently a no-op" << endl;
            }
        }

        virtual void nack() override {
            if (_debugOutput) {
                cerr << "MosquittoMessage.nack() is currently a no-op" << endl;
            }
        }
    };

public:
    MosquittoEngine(const EngineConfig config, const string &host, const int port,
                    const int keep_alive, const string &client_id, const bool clean_session, const std::string &user, const std::string &pw)
        : _debugOutput(config.debugOutput())
        , client(this, host, port, keep_alive, client_id, clean_session)
        , discoveryLastSent(0)
        , discoveryPeriod(config.discoveryPeriod/3)
    {
        if (user.size()) {
            client.setUsernamePassword(user, pw);
        }
        client.connect();
    }

    virtual ~MosquittoEngine() {
    }

    virtual Participant *registerParticipant(const Definition &definition) override {
        Definition d = validateDefinitionFromUser(definition);
        registrations.emplace_back(this, d);
        return &registrations[registrations.size() - 1];
    }

    void send(const ParticipantRegistration *r, const string &portName, const char *data, uint64_t len) {
        auto port = r->findOutPort(portName);

        if (port == nullptr) {
            throw domain_error("No such port: " + portName);
        }

        client.publish(nullptr, port->queue, 0, false, static_cast<int>(len), data);
    }

    virtual void launch() override {
        run = true;

        while (run) {
            const auto t = millis_monotonic()/1000; 
            if (connected and (t - discoveryLastSent) > discoveryPeriod) {
                for(auto &r: registrations) {
                    sendDiscoveryMessage(r);
                }
                discoveryLastSent = t;
            }

            client.poll();
        }
    }

protected:
    string generateQueueName(const Definition &d, const Definition::Port &port) override {
        return d.role + "." + string_to_upper_copy(port.id);
    }

    virtual void on_msg(const string &msg) override {
        if (!_debugOutput) {
            return;
        }

        cout << "mqtt: " << msg << endl;
    }

    virtual void on_message(const struct mosquitto_message *message) override {
        string topic = message->topic;
        for (auto &r : registrations) {
            for (auto &p : r.inports) {
                if (p.queue == topic) {
                    MosquittoMessage m(message, _debugOutput, p.id);

                    r.handler(&m);
                }
            }
        }
    }

    virtual void on_connect(int rc) override {
        connected = true;
        for(auto &r: registrations) {
            for (auto &p : r.inports) {
                on_msg("Connecting port " + p.id + " to mqtt topic " + p.queue);
                client.subscribe(nullptr, p.queue, 0);
            }
            sendDiscoveryMessage(r);
        }
        discoveryLastSent = millis_monotonic()/1000;
    }

private:
    void sendDiscoveryMessage(const ParticipantRegistration &r) {
        const string data = json11::Json(r.discoveryMessage).dump();
        client.publish(nullptr, "fbp", 0, false, data);
    }

private:
    const bool _debugOutput;
    atomic_bool run;
    msg_flo_mqtt_client client;
    vector<ParticipantRegistration> registrations;
    bool connected;
    int64_t discoveryLastSent;
    const int64_t discoveryPeriod;
};

shared_ptr<Engine> createEngine(const EngineConfig config) {

    string url = config.url();

    if (url.empty()) {
        const char* broker = std::getenv("MSGFLO_BROKER");
        if (broker) {
            url = broker;
        }
    }

    if (url.empty()) {
        throw invalid_argument("Missing msgflo url and MSGFLO_BROKER is not set.");
    }

    if (string_starts_with(url, "mqtt://")) {
        string host, username, password;
        int port = 1883;
        int keep_alive = 180;
        string client_id;
        bool clean_session = true;

        string s = url.substr(7);
        auto i_up = s.find('@');

        if (i_up != string::npos) {
            string up = s.substr(0, i_up);
            // cout << "up: " << up << endl;

            auto i_u = up.find(':');

            if (i_u != string::npos) {
                username = up.substr(0, i_u);
                password = up.substr(i_u + 1);
            } else {
                username = up;
            }
            // cout << "username: " << username << endl;
            // cout << "password: " << password << endl;

            s = s.substr(i_up + 1);
            // cout << "s: " << s << endl;
        }

        auto i_q = s.find('?');

        if (i_q != string::npos) {
            host = s.substr(0, i_q);
            s = s.substr(i_q + 1);
            // cout << "s: " << s << endl;

            while (!s.empty()) {
                auto i_amp = s.find('&');

                string kv;
                if (i_amp == string::npos) {
                    kv = s;
                    s = "";
                } else {
                    kv = s.substr(0, i_amp);
                }
                // cout << "kv: " << kv << endl;

                auto i_eq = kv.find('=');

                string key, value;
                if (i_eq != string::npos) {
                    key = kv.substr(0, i_eq);
                    value = kv.substr(i_eq + 1);
                } else {
                    key = kv;
                }
                // cout << "key: " << key << endl;

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
                // cout << "s: " << s << endl;
            }
        } else {
            host = s;
        }

        if (config.debugOutput()) {
            cout << "host: " << host << endl;
            cout << "client_id: " << client_id << endl;
            cout << "keep_alive: " << keep_alive << endl;
            cout << "clean_session: " << clean_session << endl;
        }
        return make_shared<MosquittoEngine>(config, host, port, keep_alive, client_id, clean_session, username, password);
    } else if (string_starts_with(url, "amqp://")) {
        return make_shared<AmqpEngine>(url, config);
    }

    throw std::runtime_error("Unsupported URL scheme: " + url);
}

} // namespace msgflo
