#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>

#include "telemetry.h"
#include "sensors.h"
#include "processing.h"
#include "csv_logger.h"
#include "mqtt_client.h"

/* Configuration */
#define CAR_ID "car1"
#define SAMPLE_INTERVAL_MS 100
#define PUBLISH_INTERVAL_MS 1000

/*
 * DESCRIPTION: This is the main function that will be
 * coordinating everything.
 *
 * Notes:
 * This assumes the header files provide the functions shown below.
 *
 * With this configuration, the following metrics have been set:
 *
 * Read mock sensors: 10 times/sec to simulate incoming sensor data
 * Write CSV:         10 times/sec to preserve local samples
 * Publish MQTT:      1 time/sec to send telemetry to the broker
 * Stop and clean up: On Ctrl+C to close resources safely
 *
 * For example, at one second, the program could have sampled ten sets
 * of mock readings but publish only the latest snapshot. If we need
 * the current to represent the average across that full second, the
 * processing layer must aggregate those readings before publishing.
 */

/* Allows the program to stop cleanly with Ctrl+C. */
static volatile sig_atomic_t running = 1;

static void handle_signal(int signal_number)
{
   (void)signal_number;
   running = 0;
}

/* Wait for a specified number of milliseconds. */
static void sleep_ms(long milliseconds)
{
   struct timespec delay;

   delay.tv_sec = milliseconds / 1000;
   delay.tv_nsec = (milliseconds % 1000) * 1000000L;

   // Note: nanosleep() requires the appropriate POSIX environment
   while (nanosleep(&delay, &delay) == -1 && running) {
      /* Resume waiting if interrupted by a signal. */
   }
}

int main(void)
{
   signal(SIGINT, handle_signal);

   printf("Starting Supermileage telemetry system...\n");

   /* 1. Initialize the sensors. */
   if (!sensors_init()) {
      fprintf(stderr, "Failed to initialize sensors.\n");
      return 1;
   }

   /* 2. Open the local CSV log. */
   if (!csv_logger_init("telemetry.csv")) {
      fprintf(stderr, "Failed to open CSV logger.\n");
      sensors_cleanup();
      return 1;
   }

   /* 3. Initialize MQTT. */
   if (!mqtt_client_init(CAR_ID)) {
      fprintf(stderr, "Failed to initialize MQTT client.\n");
      csv_logger_close();
      sensors_cleanup();
      return 1;
   }

   printf("Telemetry system running. Press Ctrl+C to stop.\n");

   long elapsed_ms = 0;

   /* 4. Main telemetry loop. */
   while (running) {
      /* Read the current sensor values. */
      SensorData raw_data = sensors_read();

      /* Convert raw readings into the V2 telemetry format. */
      TelemetrySnapshot snapshot =
           processing_build_snapshot(&raw_data);

      /* Save every processed sample locally. */
      csv_logger_write(&snapshot);

      /*
       * Publish once per second.
       * Sensor sampling and network publishing have
       * separate rates.
       */

      if (elapsed_ms % PUBLISH_INTERVAL_MS == 0) {
         char json[1024];

         if (processing_to_json(&snapshot, json, sizeof(json))) {
            if (!mqtt_client_publish(json)) {
               fprintf(stderr, "Could not publish telemetry.\n");
            }
         }
      }

      sleep_ms(SAMPLE_INTERVAL_MS);
      elapsed_ms += SAMPLE_INTERVAL_MS;
   }

   /* 5. Clean up resources before exiting. */
   printf("\nStopping telemetry system...\n");

   mqtt_client_cleanup();
   csv_logger_close();
   sensors_cleanup();

   printf("Telemetry system stopped.\n");
   return 0;
}