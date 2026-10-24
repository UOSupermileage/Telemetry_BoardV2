/*
 * Declares functions used to initialize sensors and obtain readings.
 * The main responsibility is to have sensors_init(),
 * sensors_read(), sensors_cleanup()
 */

// This is also where we are going to have our
// getter functions defined. The following has an
// example of how we would define get_accelerometer.
// sensors.c would then contain the actual hardware-specific
// implementation of get_accelerometer().


#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>

typedef struct {
    double x;
    double y;
    double z;
    bool valid;
} AccelerometerData;

bool sensors_init(void);

AccelerometerData get_accelerometer(void);

void sensors_cleanup(void);

#endif
