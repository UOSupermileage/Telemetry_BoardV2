/*
 * Connect to the local Mosquitto broker and publish JSON
 */


#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>

bool mqtt_client_init(const char *car_id);
bool mqtt_client_publish(const char *json);
void mqtt_client_cleanup(void);

#endif
