#ifndef TRYGVIS_MQTT_SUPPORT_H
#define TRYGVIS_MQTT_SUPPORT_H

/*
The MIT License (MIT)

Copyright (c) 2015-2016 Trygve Laugstøl <trygvis@inamo.no>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
 */

#include <mutex>
#include <string>
#include <exception>
#include <cstring>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <limits.h>
#include <unistd.h>
#include "mosquitto.h"

namespace trygvis {
namespace mqtt_support {

using namespace std;

static inline
string error_to_string(int rc) {
    if (rc == MOSQ_ERR_ERRNO) {
        return string(strerror(errno));
    }
    return string(mosquitto_strerror(rc));
}

vector<string> mqtt_tokenize_topic(string path);

class waitable {
protected:
    std::condition_variable cv;
    std::mutex mutex;

    waitable() {
    }

    virtual ~waitable() {
    }

public:
    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock);
    }

    template<class Predicate>
    void wait(Predicate predicate) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, predicate);
    }

    template<class Rep, class Period>
    std::cv_status wait_for(const std::chrono::duration<Rep, Period> &rel_time) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, rel_time);
    }

    template<class Rep, class Period, class Predicate>
    bool wait_for(const std::chrono::duration<Rep, Period> &rel_time, Predicate predicate) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, rel_time, predicate);
    }

    template<class Clock, class Duration>
    std::cv_status wait_until(const std::chrono::time_point<Clock, Duration> &timeout_time) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_until(lock, timeout_time);
    };

    template<class Clock, class Duration, class Predicate>
    bool wait_until(const std::chrono::time_point<Clock, Duration> &timeout_time,
                    Predicate predicate) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_until(lock, timeout_time, predicate);
    }
};

class mqtt_error : public std::runtime_error {

public:
    const int error;

    mqtt_error(const string &what, int rc) : runtime_error(what), error(rc) {
    }
};

class mqtt_lib {
public:
    mqtt_lib() {
        if (mqtt_client_instance_count++ == 0) {
            lock_guard<mutex> l(mqtt_client_mutex_);
            int rc = mosquitto_lib_init();

            if (rc != MOSQ_ERR_SUCCESS) {
                throw mqtt_error("Unable to initialize mosquitto: " + error_to_string(rc), rc);
            }

            mosquitto_lib_version(&version_major, &version_minor, &version_revision);
        }
    }

    virtual ~mqtt_lib() {
        if (--mqtt_client_instance_count == 0) {
            lock_guard<mutex> l(mqtt_client_mutex_);

            mosquitto_lib_cleanup();
        }
    }

    static int version_major;
    static int version_minor;
    static int version_revision;

private:
    static atomic_int mqtt_client_instance_count;
    static mutex mqtt_client_mutex_;
//    static string hostname;
};

enum mqtt_client_personality {
    threaded,
    polling
};

class mqtt_event_listener {
public:
    virtual void on_msg(const string &str) = 0;

    virtual ~mqtt_event_listener() = default;
};

template<mqtt_client_personality personality>
class mqtt_client : public waitable, private mqtt_lib {
    using guard = lock_guard<recursive_mutex>;

    template<bool>
    struct personality_tag {
    };

    typedef personality_tag<mqtt_client_personality::threaded> threaded_tag;
    typedef personality_tag<mqtt_client_personality::polling> polling_tag;
    const personality_tag<personality> p_tag{};

    mqtt_event_listener *event_listener;

    const string host;
    const int port;
    bool connecting_, connected_;
    const int keep_alive;
    int unacked_messages_;

    recursive_mutex this_mutex;

    struct mosquitto *mosquitto;

    void assert_success(const string &function, int rc) {
        if (rc != MOSQ_ERR_SUCCESS) {
            throw mqtt_error(function + ": " + error_to_string(rc), rc);
        }
    }

public:
    mqtt_client(mqtt_event_listener *event_listener, const string &host, const int port, const int keep_alive,
                const string &client_id, const bool clean_session) :
            event_listener(event_listener), host(host), port(port), connecting_(false), connected_(false),
            keep_alive(keep_alive), unacked_messages_(0) {
        const char *id = nullptr;

        if (!client_id.empty()) {
            id = client_id.c_str();
        } else {
            if (!clean_session) {
                throw mqtt_error("If client id is not specified, clean session must be true", MOSQ_ERR_INVAL);
            }
        }

        mosquitto = mosquitto_new(id, clean_session, this);
        if (!mosquitto) {
            string err = strerror(errno);
            throw runtime_error("Could not initialize mosquitto instance: " + err);
        }
        mosquitto_connect_callback_set(mosquitto, on_connect_cb);
        mosquitto_disconnect_callback_set(mosquitto, on_disconnect_cb);
        mosquitto_publish_callback_set(mosquitto, on_publish_cb);
        mosquitto_message_callback_set(mosquitto, on_message_cb);
        mosquitto_subscribe_callback_set(mosquitto, on_subscribe_cb);
        mosquitto_unsubscribe_callback_set(mosquitto, on_unsubscribe_cb);
        mosquitto_log_callback_set(mosquitto, on_log_cb);

        post_construct(p_tag);
    }

private:
    void post_construct(threaded_tag) {
        event_listener->on_msg("mosquitto_loop_start");
        int rc = mosquitto_loop_start(mosquitto);
        assert_success("mosquitto_loop_start", rc);
    }

    void post_construct(polling_tag) {
    }

public:

    virtual ~mqtt_client() {
//        should_reconnect_ = false;
        pre_destruct(p_tag);

        disconnect();
    }

private:
    void pre_destruct(threaded_tag) {
        int rc = mosquitto_loop_stop(mosquitto, true);
        if (rc) {
            event_listener->on_msg("mosquitto_loop_stop: " + error_to_string(rc));
        }
    }

    void pre_destruct(polling_tag) {
    }

public:
    int unacked_messages() {
        guard lock(this_mutex);
        return unacked_messages_;
    }

    bool connected() {
        guard lock(this_mutex);

        return connected_;
    }

    bool connecting() {
        guard lock(this_mutex);

        return connecting_;
    }

    void connect() {
        guard lock(this_mutex);

        event_listener->on_msg(
                "Connecting to " + host + ":" + to_string(port) + ", keep_alive=" + to_string(keep_alive));

        if (connecting_ || connected_) {
            disconnect();
        }

        connect(p_tag);
    }

private:
    void connect(threaded_tag) {
        connecting_ = true;
        connected_ = false;

        event_listener->on_msg("mosquitto_connect_async");
        int rc = mosquitto_connect_async(mosquitto, host.c_str(), port, keep_alive);
        assert_success("mosquitto_connect_async", rc);
    }

    void connect(polling_tag) {
        connecting_ = false;
        connected_ = true;

        event_listener->on_msg("mosquitto_connect");
        int rc = mosquitto_connect(mosquitto, host.c_str(), port, keep_alive);
        assert_success("mosquitto_connect", rc);
    }

private:
    void on_connect_wrapper(int rc) {
        guard lock(this_mutex);

        connected_ = rc == MOSQ_ERR_SUCCESS;
        connecting_ = false;

        if (connected_) {
            event_listener->on_msg("Connected");
        } else {
            event_listener->on_msg("Could not connect: " + error_to_string(rc));
        }
        on_connect(rc);

        cv.notify_all();
    }

    void on_disconnect_wrapper(int rc) {
        guard lock(this_mutex);

        event_listener->on_msg("Disconnected, rc=" + error_to_string(rc));

        bool was_connecting = connecting_, was_connected = connected_;
        connecting_ = connected_ = false;
        unacked_messages_ = 0;

        on_disconnect(was_connecting, was_connected, rc);

        cv.notify_all();
    }

    void on_publish_wrapper(int message_id) {
        guard lock(this_mutex);

        event_listener->on_msg("message ACKed, message id=" + message_id);
        unacked_messages_--;

        on_publish(message_id);

        cv.notify_all();
    }

    void on_message_wrapper(const struct mosquitto_message *message) {
        guard lock(this_mutex);
        on_message(message);
    }

    void on_subscribe_wrapper(int mid, int qos_count, const int *granted_qos) {
        static_cast<void>(qos_count);
        guard lock(this_mutex);
        on_subscribe(mid, mid, granted_qos);
    }

    void on_unsubscribe_wrapper(int mid) {
        guard lock(this_mutex);
        on_unsubscribe(mid);
    }

    void on_log_wrapper(int level, const char *str) {
        guard lock(this_mutex);

        on_log(level, str);
    }

public:
    void disconnect() {
        event_listener->on_msg("Disconnecting, connected: " + string(connected() ? "yes" : "no"));
        int rc = mosquitto_disconnect(mosquitto);
        event_listener->on_msg("mosquitto_disconnect: " + error_to_string(rc));
    }

    void subscribe(int *mid, const string &topic, int qos) {
        int rc = mosquitto_subscribe(mosquitto, mid, topic.c_str(), qos);
        assert_success("mosquitto_subscribe", rc);
    }

    void publish(int *mid, const string &topic, int qos, bool retain, const string &s) {
        auto len = s.length();

        auto int_max = std::numeric_limits<int>::max();

        if (len > int_max) {
            len = static_cast<decltype(len)>(int_max);
        }

        publish(mid, topic, qos, retain, static_cast<int>(len), s.c_str());
    }

    void publish(int *mid, const string &topic, int qos, bool retain, int payload_len, const void *payload) {
//        if (!connected_) {
//            throw mqtt_error("not connected", MOSQ_ERR_NO_CONN);
//        }

        event_listener->on_msg("Publishing " + to_string(payload_len) + " bytes to " + topic);

        int rc = mosquitto_publish(mosquitto, mid, topic.c_str(), payload_len, payload, qos, retain);

        if (rc == MOSQ_ERR_SUCCESS) {
            guard lock(this_mutex);
            unacked_messages_++;
        }

        assert_success("mosquitto_publish", rc);
    }

//    void set_should_reconnect(bool should_reconnect) {
//        this->should_reconnect_ = should_reconnect;
//    }

public:
    void poll() {
        poll(p_tag);
    }

private:
    void poll(threaded_tag) {
    }

    void poll(polling_tag) {
        int rc = mosquitto_loop(mosquitto, 100, 1);
        assert_success("mosquitto_loop", rc);
    }

    // -------------------------------------------
    // Callbacks
    // -------------------------------------------

protected:
    virtual void on_connect(int rc) {
        static_cast<void>(rc);
    }

    virtual void on_disconnect(bool was_connecting, bool was_connected, int rc) {
        static_cast<void>(was_connecting);
        static_cast<void>(was_connected);
        static_cast<void>(rc);
    }

    virtual void on_publish(int mid) {
        static_cast<void>(mid);
    }

    virtual void on_message(const struct mosquitto_message *message) {
        static_cast<void>(message);
    }

    virtual void on_subscribe(int mid, int qos_count, const int *granted_qos) {
        static_cast<void>(mid);
        static_cast<void>(qos_count);
        static_cast<void>(granted_qos);
    }

    virtual void on_unsubscribe(int mid) {
        static_cast<void>(mid);
    }

    virtual void on_log(int level, const char *str) {
        static_cast<void>(level);
        static_cast<void>(str);
    }

private:
    static void on_connect_cb(struct mosquitto *, void *self, int rc) {
        static_cast<mqtt_client *>(self)->on_connect_wrapper(rc);
    }

    static void on_disconnect_cb(struct mosquitto *, void *self, int rc) {
        static_cast<mqtt_client *>(self)->on_disconnect_wrapper(rc);
    }

    static void on_publish_cb(struct mosquitto *, void *self, int rc) {
        static_cast<mqtt_client *>(self)->on_publish_wrapper(rc);
    }

    static void on_message_cb(struct mosquitto *, void *self, const mosquitto_message *message) {
        static_cast<mqtt_client *>(self)->on_message_wrapper(message);
    }

    static void on_subscribe_cb(struct mosquitto *, void *self, int mid, int qos_count, const int *granted_qos) {
        static_cast<mqtt_client *>(self)->on_subscribe_wrapper(mid, qos_count, granted_qos);
    }

    static void on_unsubscribe_cb(struct mosquitto *, void *self, int mid) {
        static_cast<mqtt_client *>(self)->on_unsubscribe_wrapper(mid);
    }

    static void on_log_cb(struct mosquitto *, void *self, int level, const char *str) {
        static_cast<mqtt_client *>(self)->on_log_wrapper(level, str);
    }
};

}
}

#endif
