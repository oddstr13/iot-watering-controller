#include <Arduino.h>
#include "openshell_utils.h"

const unsigned long MAXULONG = 0xffffffff;
unsigned long now;
unsigned long getTimeSince(unsigned long ___start) {
    unsigned long interval;
    now = millis();
    if (___start > now) {
        interval = MAXULONG - ___start + now;
    } else {
        interval = now - ___start;
    }
    return interval;
}
