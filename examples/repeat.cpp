#include <atomic>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <boost/algorithm/string.hpp>

#include "msgflo.h"

using namespace std;

class Repeat : public msgflo::Participant
{
    struct Def : public msgflo::Definition {
        Def(void) : msgflo::Definition()
        {
            component = "C++Repeat";
            label = "Repeats input on outport unchanged";
            outports = {
                { "out", "any", "" }
            };
        }
    };

public:
    Repeat(std::string role)
        : msgflo::Participant(role, Def())
    {
    }

private:
    virtual void process(std::string port, msgflo::Message *msg)
    {
        std::cout << "Repeat.process()" << std::endl;
        _engine->send("out", msg->asJson());
        msg->ack();
    }
};

atomic_bool run(true);

int main(int argc, char **argv)
{
    std::string role = "repeat";
    if (argc >= 2) {
        role = argv[1];
    }

    Repeat repeater(role);
    msgflo::EngineConfig config(&repeater);
    if (argc >= 3) {
        config.url(argv[2]);
    }
    config.debugOutput(false);

    auto engine = msgflo::createEngine(config);

    while (run) {
        if (engine->connected()) {
            cout << "[*] Connected, waiting for messages. To exit press CTRL-C " << endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!engine->connected()) {
            cout << "Not connected, retrying. To exit press CTRL-C " << endl;
        }
    }

    return EXIT_SUCCESS;
}
