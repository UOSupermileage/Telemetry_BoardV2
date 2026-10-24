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

/*-------------------------------------------------------
 * 1. Sensor reading status
 *------------------------------------------------------*/

typedef enum {
    SENSOR_UNAVAILABLE = 0,  /* No usable reading received yet */
    SENSOR_VALID,            /* Reading is usable */
    SENSOR_INVALID           /* Reading failed validation */
} SensorStatus;

/* ------------------------------------------------------
 * 2. Raw sensor readings
 *
 * Each reading has its own timestamp and validity status.
 * Timestamps are UTC epoch milliseconds when available.
 * ----------------------------------------------------- */

typedef struct {
    double value;
    int64_t timestamp_ms;
    SensorStatus status;
} TelemetryDoubleReading;

typedef struct {
    int value;
    int64_t timestamp_ms;
    SensorStatus status;
} TelemetryIntReading;

typedef struct {
    bool value;
    int64_t timestamp_ms;
    SensorStatus status;
} TelemetryBoolReading;

/* -------------------------------------------------------
 * 3. TelemetryData
 *
 * Holds the latest readings from the physical sensors.
 * These are internal application names, not MQTT field names.
 * ----------------------------------------------------------- */

typedef struct {
   /* GPS */
    TelemetryDoubleReading latitude_deg;
    TelemetryDoubleReading longitude_deg;
    TelemetryDoubleReading altitude_m;
    TelemetryDoubleReading ground_speed_kmh;
    TelemetryDoubleReading gps_fix_quality;

    /* Accelerometer: acceleration in g */
    TelemetryDoubleReading acceleration_x_g;
    TelemetryDoubleReading acceleration_y_g;
    TelemetryDoubleReading acceleration_z_g;

    /* Hall-effect speedometer */
    TelemetryDoubleReading hall_speed_kmh;

    /* Electrical sensors */
    TelemetryDoubleReading voltage_v;
    TelemetryDoubleReading current_a;
    TelemetryDoubleReading throttle_percent;

    /* CAN measurements */
    TelemetryDoubleReading air_pressure_kpa;
    TelemetryDoubleReading load_1_kg;
    TelemetryDoubleReading load_2_kg;

    /* Dashboard switches and lights */
    TelemetryBoolReading headlights;
    TelemetryBoolReading running_lights;
    TelemetryBoolReading left_turn_signal;
    TelemetryBoolReading right_turn_signal;
    TelemetryBoolReading wiper;

    /* Optional raw hall sensor count for distance calculation */
    uint64_t hall_pulse_count;
    int64_t hall_pulse_timestamp_ms;
    bool hall_pulse_count_valid;

} TelemetryData;

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
} TelemetryOptionalDouble;

typedef struct {
    int value;
    bool present;
} TelemetryOptionalInt;

typedef struct {
    bool value;
    bool present;
} TelemetryOptionalBool;

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
    TelemetryOptionalDouble lat;  /* Degrees */
    TelemetryOptionalDouble lon;  /* Degrees */
    TelemetryOptionalDouble alt;  /* Metres */
    TelemetryOptionalDouble gs;   /* Ground speed, km/h */
    TelemetryOptionalInt fix;     /* 0, 1, 2, 4, or 5 */

    /* Mean acceleration over the output interval, in g */
    TelemetryOptionalDouble ax;
    TelemetryOptionalDouble ay;
    TelemetryOptionalDouble az;

    /* Hall speed and cumulative distance */
    TelemetryOptionalDouble spd;  /* km/h */
    TelemetryOptionalDouble dist; /* Metres since start */

    /* Electrical measurements */
    TelemetryOptionalDouble v;    /* Mean voltage, V */
    TelemetryOptionalDouble i;    /* Mean current, A */
    TelemetryOptionalDouble e;    /* Energy during interval, J */

    /* Dashboard */
    TelemetryOptionalDouble thr;  /* Throttle, 0-100% */

    /*
     * Exactly five characters:
     * headlight, running lights, left turn, right turn, wiper.
     * Example: "11000".
     */
    char lts[TELEMETRY_LIGHTS_STRING_LENGTH];
    bool lts_present;

    /* CAN measurements */
    TelemetryOptionalDouble ap;   /* Air pressure, kPa */
    TelemetryOptionalDouble l1;   /* Load gauge 1, kg */
    TelemetryOptionalDouble l2;   /* Load gauge 2, kg */

} TelemetrySnapshot;

#endif /* TELEMETRY_H */