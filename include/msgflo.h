#pragma once

#include <string>
#include <memory>

#include "json11.hpp"

namespace msgflo {

struct Definition {

    struct Port {
        std::string id;
        std::string type;
        std::string queue;

        json11::Json to_json() const {
            return json11::Json::object {
                    {"id",    id},
                    {"type",  type},
                    {"queue", queue}
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
        p.queue = role + "." + boost::to_upper_copy<std::string>(p.id);
        return p;
    }

    static Definition instantiate(Definition &d, std::string role) {
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
    Definition() :
            inports{
                    Port{"in", "any", ""}},
            outports{
                    Port{"out", "any", ""},
                    Port{"error", "error", ""}
            } {}

    json11::Json ports_to_json(const std::vector<Port> &ports) const {
        using namespace json11;
        auto objects = std::vector<Json>();
        for (const auto &p : ports) {
            objects.push_back(p.to_json());
        }
        return Json::array(objects);
    }

    json11::Json to_json() const {
        using namespace json11;

        auto ins = ports_to_json(inports);
        return Json::object {
                {"id",        id},
                {"role",      role},
                {"component", component},
                {"label",     label},
                {"icon",      icon},
                {"inports",   ins},
                {"outports",  ports_to_json(outports)}
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
    enum Type {
        Json,
        // Binary, // TODO: support binary type also
    };

    uint64_t deliveryTag;
    json11::Json json;
    Type type = Json;
};

class Engine;

class Participant {
    friend class Engine;

public:
    Participant(std::string _role, Definition _def)
            : role(_role), _definition(Definition::instantiate(_def, role)) {}

    const Definition *definition() const {
        return &_definition;
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
    const std::string role;
    const Definition _definition;
    std::shared_ptr<Engine> _engine{};
};

class Engine {
public:
    virtual void send(std::string port, Message &msg) = 0;

    virtual void ack(const Message &msg) = 0;

    virtual void nack(const Message &msg) = 0;

    virtual bool connected() = 0;
protected:
    void setEngine(Participant *participant, std::shared_ptr<Engine> e) {
        participant->_engine = e;
    }

    void process(Participant *participant, std::string port, Message msg) {
        participant->process(port, msg);
    }
};

std::shared_ptr<Engine> createEngine(Participant *participant, const std::string &url);

} // namespace msgflo
