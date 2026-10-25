/*
 * Aggregates readings, calculates derived metrics, and
 * produces the MQTT JSON payload.
 *
 *
 */


#include "processing.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Copy a sensor reading only if it is valid. */
static OptionalDouble make_optional(SensorReading reading)
{
    OptionalDouble result = {0};

    if (reading.valid) {
        result.value = reading.value;
        result.present = true;
    }

    return result;
}

/* GPS fix quality must be an integer in the V2 allowed set. */
static OptionalInt make_fix(SensorReading reading)
{
    OptionalInt result = {0};

    if (!reading.valid)
        return result;

    int fix = (int)reading.value;

    if (reading.value != (double)fix)
        return result;

    if (fix != 0 && fix != 1 && fix != 2 &&
        fix != 4 && fix != 5)
        return result;

    result.value = fix;
    result.present = true;

    return result;
}

TelemetrySnapshot processing_build_snapshot(
    const SensorData *data)
{
    TelemetrySnapshot out = {0};

    if (data == NULL)
        return out;

    /* V2 timestamp: GPS UTC epoch milliseconds. */
    if (data->gps_timestamp_valid) {
        out.ts = data->gps_timestamp_ms;
        out.ts_present = true;
    }

    /* GPS */
    out.lat = make_optional(data->latitude_deg);
    out.lon = make_optional(data->longitude_deg);
    out.alt = make_optional(data->altitude_m);
    out.gs  = make_optional(data->ground_speed_kmh);
    out.fix = make_fix(data->gps_fix);

    /* Accelerometer */
    out.ax = make_optional(data->acceleration_x_g);
    out.ay = make_optional(data->acceleration_y_g);
    out.az = make_optional(data->acceleration_z_g);

    /* Hall speed */
    out.spd = make_optional(data->hall_speed_kmh);

    /*
     * TODO:
     * Calculate dist from hall pulses.
     * Calculate interval means for ax, ay, az, v and i.
     * Calculate energy e over the output interval.
     *
     * Leave these fields absent until implemented.
     */

    /* Electrical measurements */
    out.v = make_optional(data->voltage_v);
    out.i = make_optional(data->current_a);

    /* Dashboard throttle */
    out.thr = make_optional(data->throttle_percent);

    /* V2 lights string: headlight, running, left, right, wiper. */
    if (data->dashboard.valid) {
        out.lts[0] = data->dashboard.headlights ? '1' : '0';
        out.lts[1] = data->dashboard.running_lights ? '1' : '0';
        out.lts[2] = data->dashboard.left_turn_signal ? '1' : '0';
        out.lts[3] = data->dashboard.right_turn_signal ? '1' : '0';
        out.lts[4] = data->dashboard.wiper ? '1' : '0';
        out.lts[5] = '\0';

        out.lts_present = true;
    }

    /* CAN measurements */
    out.ap = make_optional(data->air_pressure_kpa);
    out.l1 = make_optional(data->load_1_kg);
    out.l2 = make_optional(data->load_2_kg);

    return out;
}

/* Append one JSON fragment safely. */
static bool append_text(
    char *buffer,
    size_t size,
    size_t *used,
    const char *text)
{
    if (*used >= size)
        return false;

    int written = snprintf(
        buffer + *used,
        size - *used,
        "%s",
        text
    );

    if (written < 0 || (size_t)written >= size - *used)
        return false;

    *used += (size_t)written;
    return true;
}

static bool append_double(
    char *buffer,
    size_t size,
    size_t *used,
    bool *first,
    const char *key,
    OptionalDouble value)
{
    if (!value.present)
        return true;

    if (*used >= size)
        return false;

    int written = snprintf(
        buffer + *used,
        size - *used,
        "%s\"%s\":%.10g",
        *first ? "" : ",",
        key,
        value.value
    );

    if (written < 0 || (size_t)written >= size - *used)
        return false;

    *used += (size_t)written;
    *first = false;

    return true;
}

static bool append_int(
    char *buffer,
    size_t size,
    size_t *used,
    bool *first,
    const char *key,
    OptionalInt value)
{
    if (!value.present)
        return true;

    if (*used >= size)
        return false;

    int written = snprintf(
        buffer + *used,
        size - *used,
        "%s\"%s\":%d",
        *first ? "" : ",",
        key,
        value.value
    );

    if (written < 0 || (size_t)written >= size - *used)
        return false;

    *used += (size_t)written;
    *first = false;

    return true;
}

bool processing_to_json(
    const TelemetrySnapshot *s,
    char *buffer,
    size_t buffer_size)
{
    if (s == NULL || buffer == NULL || buffer_size == 0)
        return false;

    size_t used = 0;
    bool first = true;

    buffer[0] = '\0';

    if (!append_text(buffer, buffer_size, &used, "{"))
        return false;

    /* Keep fields in the exact V2 order. */
    if (s->ts_present) {
        char timestamp[64];

        snprintf(timestamp, sizeof(timestamp),
                 "\"ts\":%lld",
                 (long long)s->ts);

        if (!append_text(buffer, buffer_size, &used,
                         timestamp))
            return false;

        first = false;
    }

    if (!append_double(buffer, buffer_size, &used, &first,
                       "lat", s->lat)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "lon", s->lon)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "alt", s->alt)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "gs", s->gs)) return false;
    if (!append_int(buffer, buffer_size, &used, &first,
                    "fix", s->fix)) return false;

    if (!append_double(buffer, buffer_size, &used, &first,
                       "ax", s->ax)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "ay", s->ay)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "az", s->az)) return false;

    if (!append_double(buffer, buffer_size, &used, &first,
                       "spd", s->spd)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "dist", s->dist)) return false;

    if (!append_double(buffer, buffer_size, &used, &first,
                       "v", s->v)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "i", s->i)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "e", s->e)) return false;

    if (!append_double(buffer, buffer_size, &used, &first,
                       "thr", s->thr)) return false;

    if (s->lts_present) {
        char lights[32];

        /* The lights string is five '0'/'1' characters. */
        if (strlen(s->lts) != 5)
            return false;

        for (int i = 0; i < 5; i++) {
            if (s->lts[i] != '0' && s->lts[i] != '1')
                return false;
        }

        snprintf(lights, sizeof(lights),
                 "%s\"lts\":\"%s\"",
                 first ? "" : ",",
                 s->lts);

        if (!append_text(buffer, buffer_size, &used, lights))
            return false;

        first = false;
    }

    if (!append_double(buffer, buffer_size, &used, &first,
                       "ap", s->ap)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "l1", s->l1)) return false;
    if (!append_double(buffer, buffer_size, &used, &first,
                       "l2", s->l2)) return false;

    return append_text(buffer, buffer_size, &used, "}");
}
