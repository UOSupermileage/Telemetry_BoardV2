/*
  Connects to Mosquitto and publishes the payload.
*/


#include "mqtt_client.h"

#include <mosquitto.h>
#include <stdio.h>
#include <string.h>

#define MQTT_HOST "localhost"
#define MQTT_PORT 1883
#define MQTT_KEEPALIVE 30
#define MQTT_QOS 1

static struct mosquitto *client = NULL;
static char topic[128];

bool mqtt_client_init(const char *car_id)
{
    if (car_id == NULL || car_id[0] == '\0')
        return false;

    if (strlen(car_id) > 100)
        return false;

    /* Initialize the Mosquitto library. */
    int rc = mosquitto_lib_init();

    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Mosquitto init failed: %s\n",
                mosquitto_strerror(rc));
        return false;
    }

    /* Create the MQTT client. */
    client = mosquitto_new(NULL, true, NULL);

    if (client == NULL) {
        fprintf(stderr, "Failed to create MQTT client.\n");
        mosquitto_lib_cleanup();
        return false;
    }

    /* Build the V2 topic: uosm/<car_id>/tlm */
    int written = snprintf(
        topic,
        sizeof(topic),
        "uosm/%s/tlm",
        car_id
    );

    if (written < 0 || (size_t)written >= sizeof(topic)) {
        fprintf(stderr, "MQTT topic is too long.\n");
        mqtt_client_cleanup();
        return false;
    }

    /* Connect to Mosquitto running on this Pi. */
    rc = mosquitto_connect(
        client,
        MQTT_HOST,
        MQTT_PORT,
        MQTT_KEEPALIVE
    );

    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTT connection failed: %s\n",
                mosquitto_strerror(rc));
        mqtt_client_cleanup();
        return false;
    }

    /* Start the network loop. */
    rc = mosquitto_loop_start(client);

    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTT loop failed: %s\n",
                mosquitto_strerror(rc));
        mqtt_client_cleanup();
        return false;
    }

    printf("Connected to MQTT broker at %s:%d\n",
           MQTT_HOST, MQTT_PORT);
    printf("Publishing to topic: %s\n", topic);

    return true;
}

bool mqtt_client_publish(const char *json)
{
    if (client == NULL || json == NULL)
        return false;

    int rc = mosquitto_publish(
        client,
        NULL,                   /* Message ID assigned internally */
        topic,
        (int)strlen(json),
        json,
        MQTT_QOS,
        true                    /* Retain latest message */
    );

    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTT publish failed: %s\n",
                mosquitto_strerror(rc));
        return false;
    }

    return true;
}

void mqtt_client_cleanup(void)
{
    if (client != NULL) {
        mosquitto_disconnect(client);

        /* Stop the background network loop. */
        mosquitto_loop_stop(client, true);

        mosquitto_destroy(client);
        client = NULL;

        mosquitto_lib_cleanup();
    }
}
