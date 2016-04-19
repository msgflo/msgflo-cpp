
// cd thirdparty/amqpcpp && make install PREFIX=./install; cd -
// g++ -std=c++11 -o ./spec/fixtures/repeat-cpp ./spec/fixtures/repeat.cpp -I./thirdparty/json11/ -I./thirdparty/amqpcpp/install/include/ -I./thirdparty/amqpcpp/examples/rabbitmq_tutorials/  ./thirdparty/amqpcpp/install/lib/libamqpcpp.a.2.2.0 -lboost_system -pthread

#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <boost/algorithm/string.hpp>

#include "json11.cpp"
#include "asiohandler.cpp"

namespace msgflo {

class Engine;
class Participant;

struct Definition {

    struct Port {
        std::string id;
        std::string type;
        std::string queue;

        json11::Json to_json() const
        {
            return json11::Json::object {
                { "id", id },
                { "type", type },
                { "queue", queue }
            };
        }
    };

public:
    static std::string queueForPort(const std::vector<Port> &ports, std::string id) {
        for (const auto &p : ports) {
            if (p.id == id) {
                return p.queue;
            }
        }
        return "";
    }

    static Port &addDefaultQueue(Port &p, std::string role) {
        if (!p.queue.empty()) {
            return p;
        }
        p.queue = role+"."+boost::to_upper_copy<std::string>(p.id);
        return p;
    }

    static Definition instantiate(Definition &d, std::string role)
    {
        d.id = role + std::to_string(rand());
        d.role = role;

        for (auto &p : d.inports) {
            addDefaultQueue(p, role);
        }
        for (auto &p : d.outports) {
            addDefaultQueue(p, role);
        }
        return d;
    }

public:
    Definition() 
        : inports {
            Port{ "in", "any", "" }
        },
        outports {
            Port{ "out", "any", "" },
            Port{ "error", "error", "" }
        }
    {
    }

    json11::Json ports_to_json(const std::vector<Port> &ports) const
    {
        using namespace json11;
        auto objects = std::vector<Json>();
        for (const auto &p : ports) {
            objects.push_back(p.to_json());
        }
        return Json::array(objects);
    }

    json11::Json to_json() const
    {
        using namespace json11;

        auto ins = ports_to_json(inports);
        return Json::object {
            { "id", id },
            { "role", role },
            { "component", component },
            { "label", label },
            { "icon", icon },
            { "inports", ins },
            { "outports", ports_to_json(outports) }
        };
    }

public:
    std::string id;
    std::string role;
    std::string component;
    std::string label = "";
    std::string icon = "file-word-o";
    std::vector<Port> inports;
    std::vector<Port> outports;
};


struct Message {
    // TODO: support binary type also
    enum Type {
        Json,
        Binary
    };

    uint64_t deliveryTag;
    json11::Json json;
    Type type = Json;
};

class Participant {
    friend class Engine;

public:
    Participant(std::string _role, Definition _def)
        : role(_role)
    {
        definition = Definition::instantiate(_def, role);
    }

protected:
    /* Receiving messages. Override in subclass */
    virtual void process(std::string port, Message msg) = 0;

    /* Sending messages */
    void send(std::string port, Message &msg);

    // ACK/NACK
    void ack(Message msg);
    void nack(Message msg);

private:
    std::string role;
    Engine* _engine = nullptr;
    Definition definition;
};


struct ConnectionOptions {
    ConnectionOptions()
        : port(5672)
        , host("localhost")
        , user("guest")
        , password("guest")
    {
    }

    // TODO: add code for parsing from string

    int port;
    std::string host;
    std::string user;
    std::string password;
};

class Engine {
    friend class Participant;

public:
    Engine(Participant *p, ConnectionOptions o)
        : handler(ioService)
        , connection(&handler, AMQP::Login(o.user, o.password), "/")
        , channel(&connection)
        , participant(p)
    {
        handler.connect(o.host, o.port);
        channel.setQos(1); // TODO: is this prefech?
        participant->_engine = this;
    }

    void start() {

        for (const auto &p : participant->definition.inports ) {
            setupInport(p);
        }
        for (const auto &p : participant->definition.outports ) {
            setupOutport(p);
        }

        sendParticipant();

        ioService.run();
    }

    void stop() {
        
    }

private:
    void sendParticipant() {
        std::string data = json11::Json(participant->definition).dump();
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
                this->participant->process(p.id, msg);
            });

    }

private:
    // Interfaces used by Participant
    void send(std::string port, const Message &msg) {
        const std::string exchange = Definition::queueForPort(participant->definition.outports, port);
        const std::string data = msg.json.dump(); 
        AMQP::Envelope env(data);
        std::cout <<" Sending on " << exchange << " :" << data << std::endl;
        channel.publish(exchange, "", env);
    }

    void ack(const Message &msg) {
        channel.ack(msg.deliveryTag);
    }
    void nack(const Message &msg) {
        // channel.nack(msg.deliveryTag); // FIXME: implement
    }


private:
    boost::asio::io_service ioService;
    AsioHandler handler;
    AMQP::Connection connection;
    AMQP::Channel channel;
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


} // namespace msgflo;


