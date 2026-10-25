//
// Created by crdup on 2026-10-24.
//

#include "sensors.h"
#include <math.h>

static double mock_time = 0.0;

bool sensors_init(void)
{
    mock_time = 0.0;
    return true;
}

SensorData sensors_read(void)
{
    SensorData data = {0};

    /* Simulate changing acceleration, in g. */
    data.acceleration_x_g.value = 0.05 * sin(mock_time);
    data.acceleration_x_g.valid = true;

    data.acceleration_y_g.value = 0.03 * cos(mock_time);
    data.acceleration_y_g.valid = true;

    data.acceleration_z_g.value = 1.0;
    data.acceleration_z_g.valid = true;

    /* Simulate voltage, in volts. */
    data.voltage_v.value = 12.0 + 0.2 * sin(mock_time);
    data.voltage_v.valid = true;

    /* Simulate current, in amps. */
    data.current_a.value = 5.0 + 0.5 * cos(mock_time);
    data.current_a.valid = true;

    /* Simulate throttle, as a percentage. */
    data.throttle_percent.value = 40.0 + 10.0 * sin(mock_time);
    data.throttle_percent.valid = true;

    mock_time += 0.1;

    return data;
}

void sensors_cleanup(void)
{
    /* No hardware resources to release for mock sensors. */
}