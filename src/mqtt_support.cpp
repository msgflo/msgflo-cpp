#include "mqtt_support.h"

namespace trygvis {
namespace mqtt_support {

using namespace std;

//static
//string find_hostname();

int mqtt_lib::version_major;
int mqtt_lib::version_minor;
int mqtt_lib::version_revision;
//string mqtt_lib::hostname = find_hostname();

atomic_int mqtt_lib::mqtt_client_instance_count(0);
mutex mqtt_lib::mqtt_client_mutex_;

//static
//string find_hostname() {
//    char hostname[1000];
//    auto result = gethostname(hostname, sizeof(hostname));
//    if (result) {
//        return "unknown";
//    }
//
//    return string(hostname);
//}

vector<string> mqtt_tokenize_topic(string path) {
    char **topics;
    int topic_count;
    int i;

    mosquitto_sub_topic_tokenise(path.c_str(), &topics, &topic_count);

    vector<string> res;
    for (i = 0; i < topic_count; i++) {
        if (topics[i] != NULL) {
            res.emplace_back(topics[i]);
        }
    }

    mosquitto_sub_topic_tokens_free(&topics, topic_count);

    return res;
}

}
}
