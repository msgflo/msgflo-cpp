#include <atomic>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <boost/algorithm/string.hpp>

#include "msgflo.h"

using namespace std;

atomic_bool run(true);

int main(int argc, char **argv)
{
    std::string role = "repeat";
    if (argc >= 2) {
        role = argv[1];
    }

    msgflo::Definition def;
    def.component = "C++Repeat";
    def.label = "Repeats input on outport unchanged";
    def.outports = {
        { "out", "any", "" }
    };
    def.role = role;

    msgflo::EngineConfig config;
    if (argc >= 3) {
        config.url(argv[2]);
    }
    config.debugOutput(true);

    auto engine = msgflo::createEngine(config);

    msgflo::Participant *participant = engine->registerParticipant(def, [&](msgflo::Message *msg) {
        auto payload = msg->asJson();
        std::cout << "Got message:" << endl << payload.dump() << std::endl;
        participant->send("out", payload);
        msg->ack();
    });

    std::cout << "C++Repeat started" << endl;
    engine->launch();

    return EXIT_SUCCESS;
}
