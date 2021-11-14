#ifndef GLOBAL_BUFFERS_H
#define GLOBAL_BUFFERS_H

#include <ArduinoJson.h>
#include <Buffer.h>

#define uploadbuf_LEN 1500
extern Buffer uploadbuf;
extern uint8_t __uploadbuf[uploadbuf_LEN];

void init_global_buffers();

#endif
