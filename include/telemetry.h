// Defines Telemetry data, measurement types, and validity flags.
#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * telemetry.h
 *
 * Shared data structures for the Supermileage telemetry.
 *
 * Sensor drivers populate TelemetryData.
 * Processing produces TelemetrySnapshot.
 * The MQTT serializer uses the snapshot to build the
 * flat Telemetry V2 JSON payload.
 */

/* ------------------------------------------------------
 * 1. Raw sensor readings
 *
 * Each reading has its own timestamp and validity status.
 * Timestamps are UTC epoch milliseconds when available.
 * ----------------------------------------------------- */

typedef struct {
   double value;
   bool valid;
} SensorReading;

/* -------------------------------------------------------
 * 2. TelemetryData
 *
 * Holds the latest readings from the physical sensors.
 * These are internal application names, not MQTT field names.
 * ----------------------------------------------------------- */

typedef struct {
    bool headlights;
    bool running_lights;
    bool left_turn_signal;
    bool right_turn_signal;
    bool wiper;
    bool valid;
} DashboardData;

typedef struct {
    /* GPS */
    SensorReading latitude_deg;
    SensorReading longitude_deg;
    SensorReading altitude_m;
    SensorReading ground_speed_kmh;
    SensorReading gps_fix;
    int64_t gps_timestamp_ms;
    bool gps_timestamp_valid;

    /* Accelerometer: acceleration in g */
    SensorReading acceleration_x_g;
    SensorReading acceleration_y_g;
    SensorReading acceleration_z_g;

    /* Hall-effect speed sensor */
    SensorReading hall_speed_kmh;

    /* Electrical sensors */
    SensorReading voltage_v;
    SensorReading current_a;
    SensorReading throttle_percent;

    /* CAN measurements */
    SensorReading air_pressure_kpa;
    SensorReading load_1_kg;
    SensorReading load_2_kg;

    /* Dashboard switches */
    DashboardData dashboard;

    /* Optional raw hall sensor count for distance calculation */
    uint64_t hall_pulse_count;
    int64_t hall_pulse_timestamp_ms;
    bool hall_pulse_count_valid;

} SensorData;

/* ---------------------------------------------------------
 * 4. Optional output values
 *
 * C does not have std::optional, so each value has a
 * corresponding 'present' flag.
 *
 * If present == false, the JSON serializer must omit
 * that field rather than outputting zero or null.
 * --------------------------------------------------------- */

typedef struct {
    double value;
    bool present;
} OptionalDouble;

typedef struct {
    int value;
    bool present;
} OptionalInt;

#define TELEMETRY_LIGHTS_STRING_LENGTH 6
/* Five characters plus the terminating '\0'. */

/* ---------------------------------------------------------
 * 5. TelemetrySnapshot
 *
 * One consolidated telemetry output interval.
 *
 * Field names and declaration order match the Telemetry V2
 * MQTT specification. Values use the specified units.
 * --------------------------------------------------------- */

typedef struct {
    /* ts: UTC epoch timestamp in milliseconds from GPS */
    int64_t ts;
    bool ts_present;

    /* GPS */
    OptionalDouble lat;  /* Degrees */
    OptionalDouble lon;  /* Degrees */
    OptionalDouble alt;  /* Metres */
    OptionalDouble gs;   /* Ground speed, km/h */
    OptionalInt fix;     /* 0, 1, 2, 4, or 5 */

    /* Mean acceleration over the output interval, in g */
    OptionalDouble ax;
    OptionalDouble ay;
    OptionalDouble az;

    /* Hall speed and cumulative distance */
    OptionalDouble spd;  /* km/h */
    OptionalDouble dist; /* Metres since start */

    /* Electrical measurements */
    OptionalDouble v;    /* Mean voltage, V */
    OptionalDouble i;    /* Mean current, A */
    OptionalDouble e;    /* Energy during interval, J */

    /* Dashboard */
    OptionalDouble thr;  /* Throttle, 0-100% */

    /*
     * Exactly five characters:
     * headlight, running lights, left turn, right turn, wiper.
     * Example: "11000".
     */
    char lts[TELEMETRY_LIGHTS_STRING_LENGTH];
    bool lts_present;

    /* CAN measurements */
    OptionalDouble ap;   /* Air pressure, kPa */
    OptionalDouble l1;   /* Load gauge 1, kg */
    OptionalDouble l2;   /* Load gauge 2, kg */

} TelemetrySnapshot;

#endif /* TELEMETRY_H */