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
    if (argc != 2) {
        cerr << "usage: " << argv[0] << " [<ampq url> | <mqtt url>]" << endl;
        return EXIT_FAILURE;
    }

    Repeat repeater("repeat");
    msgflo::EngineConfig config(&repeater);
    config.url(argv[1]);
    auto engine = msgflo::createEngine(config);

    std::cout << " [*] Waiting for messages. To exit press CTRL-C" << std::endl;

    while (run) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        cout << "Connected: " << (engine->connected() ? "yes" : "no") << endl;
    }

    return EXIT_SUCCESS;
}
