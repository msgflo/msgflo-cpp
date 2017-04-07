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

class Message {
public:
    virtual ~Message() {};

    virtual json11::Json asJson() = 0;
    virtual std::string asString() = 0;

    virtual void data(const char **_data, uint64_t *len) = 0;

    virtual void ack() = 0;

    virtual void nack() = 0;

    virtual std::string port() = 0;
};

using MessageHandler = std::function<void(Message *)>;

class Engine;

class Participant {
public:
    virtual ~Participant() = default;

    virtual void send(std::string port, const json11::Json &json) = 0;

    virtual void send(std::string port, const std::string &string) = 0;

    virtual void send(std::string port, const char *data, uint64_t len) = 0;

    virtual void onMessage(const MessageHandler &handler) = 0;

private:
};

class Engine {
public:
    virtual Participant *registerParticipant(const Definition &definition) = 0;

    // API compatibility with existing signature
    Participant *registerParticipant(const Definition &definition, MessageHandler handler) {
        auto p = registerParticipant(definition);
        p->onMessage(handler);
        return p;
    }

    virtual void launch() = 0;
protected:
};

class EngineConfig {
public:
    EngineConfig() : _debugOutput(false) {
        _debugOutput = std::getenv("MSGFLO_CPP_DEBUG") ? true : false;
    }

    EngineConfig& debugOutput(bool on) {
        _debugOutput = on;
        return *this;
    };

    bool debugOutput() const {
        return _debugOutput;
    }

    EngineConfig& url(const std::string &url) {
        _url = url;
        return *this;
    };

    std::string url() const {
        return _url;
    }

private:
    bool _debugOutput;
    std::string _url;
};

std::shared_ptr<Engine> createEngine(const EngineConfig config);

} // namespace msgflo
