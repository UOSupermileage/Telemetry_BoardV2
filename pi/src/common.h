#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

int serial_open(const char *dev);              /* raw 8N1 @ 115200, blocking writes */
int write_all(int fd, const void *buf, size_t n);
const char *env_str(const char *name, const char *def);
double now(void);                              /* monotonic seconds */

#endif
