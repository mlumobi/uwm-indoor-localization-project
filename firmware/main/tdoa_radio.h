#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { TDOA_SYNC = 1, TDOA_BLINK = 2 } tdoa_frame_kind_t;

typedef struct {
    uint16_t source;
    uint8_t kind;
    uint32_t boot;
    uint32_t sequence;
    uint64_t ticks;
} tdoa_frame_t;

void tdoa_write_le(uint8_t *bytes, uint64_t value, size_t count);
uint64_t tdoa_read_le(const uint8_t *bytes, size_t count);
bool tdoa_radio_transmit(tdoa_frame_t *frame);
bool tdoa_radio_receive(tdoa_frame_t *frame, bool *listening);
