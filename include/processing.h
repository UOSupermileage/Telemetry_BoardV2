//
// Created by crdup on 2026-10-25.
//


#ifndef PROCESSING_H
#define PROCESSING_H

#include <stdbool.h>
#include <stddef.h>
#include "telemetry.h"

TelemetrySnapshot processing_build_snapshot(
    const SensorData *data
);

bool processing_to_json(
    const TelemetrySnapshot *snapshot,
    char *buffer,
    size_t buffer_size
);

#endif
