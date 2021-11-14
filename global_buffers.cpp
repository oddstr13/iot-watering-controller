#include <EEPROM.h>
#include "global_buffers.h"

Buffer uploadbuf;
uint8_t __uploadbuf[uploadbuf_LEN];

void init_global_buffers() {
    if (not uploadbuf.size) {
        uploadbuf.setBuffer(__uploadbuf, uploadbuf_LEN);
    }
}
