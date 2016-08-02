#include "mqtt_support.h"

namespace trygvis {
namespace mqtt_support {

using namespace std;

int mqtt_lib::version_major;
int mqtt_lib::version_minor;
int mqtt_lib::version_revision;

atomic_int mqtt_lib::mqtt_client_instance_count(0);
mutex mqtt_lib::mqtt_client_mutex_;

}
}
