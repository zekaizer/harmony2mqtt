#include <stdio.h>
#include <iostream>
#include "MQTTClient.h"

static MQTTClient client;

int mqtt_init(void) {
	int rc;

	rc = MQTTClient_create(&client, "192.168.1.2:1883", "HarmonyCompanionRemote", MQTTCLIENT_PERSISTENCE_NONE, NULL);

    return rc;
}

int mqtt_connect(void) {
    int rc;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

	conn_opts.keepAliveInterval = 10;
	conn_opts.cleansession = 1;

	rc = MQTTClient_connect(client, &conn_opts);

    return rc;
}

int mqtt_send(const char *topic, const char *data, unsigned int len) {
    int rc;
    MQTTClient_deliveryToken dt;

    while (true) {
        MQTTClient_message msg = MQTTClient_message_initializer;
        msg.payload = (void *)data;
        msg.payloadlen = len;
        msg.qos = 0;
        msg.retained = 0;
        rc = MQTTClient_publishMessage(client, topic, &msg, &dt);
        if (rc < 0) {
            std::cout << "return error! " << rc << ", reconnect" << std::endl;
            MQTTClient_disconnect(client, 10000);
            mqtt_connect();
        } else {
            break;
        }
    }
    rc = MQTTClient_waitForCompletion(client, dt, 10000);

    return rc;
}