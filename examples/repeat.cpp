#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <boost/algorithm/string.hpp>

#include "msgflo.h"

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
    virtual void process(std::string port, msgflo::Message msg)
    {
        std::cout << "Repeat.process()" << std::endl;
        msgflo::Message out;
        out.json = msg.json;
        send("out", out);
        ack(msg);
    }
};

int main(void)
{
    // TODO: allow to set broker info, name on commandline or with envvar
    Repeat repeater("repeat");
    msgflo::ConnectionOptions o;
    msgflo::Engine engine(&repeater, o);

    std::cout << " [*] Waiting for messages. To exit press CTRL-C" << std::endl;
    engine.start();
    return 0;
}
