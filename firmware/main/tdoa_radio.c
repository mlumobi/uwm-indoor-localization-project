#include "tdoa_radio.h"

#include <string.h>

#include "deca_device_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { PACKET_BYTES = 20, FCS_BYTES = 2, TIMESTAMP_BYTES = 5, TX_WAIT_US = 40000 };

// Separate PAN and marker from TWR. The sequence in the payload is 32 bits.
static const uint8_t HEADER[] = {0x41, 0x88, 0, 0x56, 0x57, 0xff, 0xff};
static const uint32_t RX_FLAGS = SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO;
static const uint32_t RX_TERMINAL = DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO;

void tdoa_write_le(uint8_t *bytes, uint64_t value, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        bytes[i] = value >> (8 * i);
    }
}

uint64_t tdoa_read_le(const uint8_t *bytes, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        value |= (uint64_t)bytes[i] << (8 * i);
    }
    return value;
}

static bool start_receiving(void)
{
    dwt_forcetrxoff();
    dwt_writesysstatuslo(RX_FLAGS);
    dwt_setrxtimeout(0);
    return dwt_rxenable(DWT_START_RX_IMMEDIATE) == DWT_SUCCESS;
}

bool tdoa_radio_transmit(tdoa_frame_t *frame)
{
    uint8_t packet[PACKET_BYTES];
    memcpy(packet, HEADER, sizeof(HEADER));
    packet[2] = frame->sequence;
    tdoa_write_le(packet + 7, frame->source, 2);
    packet[9] = 'D';
    packet[10] = 1;
    packet[11] = frame->kind;
    tdoa_write_le(packet + 12, frame->boot, 4);
    tdoa_write_le(packet + 16, frame->sequence, 4);

    dwt_forcetrxoff();
    dwt_writesysstatuslo(RX_FLAGS | DWT_INT_TXFRS_BIT_MASK);
    dwt_setrxtimeout(0);
    if (dwt_writetxdata(sizeof(packet), packet, 0) != DWT_SUCCESS) {
        return false;
    }
    dwt_writetxfctrl(sizeof(packet) + FCS_BYTES, 0, 1);
    if (dwt_starttx(frame->kind == TDOA_SYNC ? DWT_RESPONSE_EXPECTED : DWT_START_TX_IMMEDIATE) != DWT_SUCCESS) {
        return false;
    }
    const int64_t deadline = esp_timer_get_time() + TX_WAIT_US;
    do {
        if (dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK) {
            uint8_t bytes[TIMESTAMP_BYTES];
            dwt_readtxtimestamp(bytes);
            frame->ticks = tdoa_read_le(bytes, sizeof(bytes));
            return true;
        }
        vTaskDelay(1);
    } while (esp_timer_get_time() <= deadline);
    return false;
}

bool tdoa_radio_receive(tdoa_frame_t *frame, bool *listening)
{
    if (!*listening) {
        *listening = start_receiving();
    }
    const uint32_t status = dwt_readsysstatuslo();
    if (!(status & RX_TERMINAL)) {
        return false;
    }
    uint8_t packet[PACKET_BYTES];
    uint8_t timestamp[TIMESTAMP_BYTES];
    uint8_t ranging;
    bool good = (status & DWT_INT_RXFCG_BIT_MASK) && !(status & DWT_INT_CIAERR_BIT_MASK) &&
                dwt_getframelength(&ranging) == sizeof(packet) + FCS_BYTES;
    if (good) {
        dwt_readrxdata(packet, sizeof(packet), 0);
        dwt_readrxtimestamp(timestamp, DWT_COMPAT_NONE);
    }
    *listening = start_receiving();
    if (!good || memcmp(packet, HEADER, 2) != 0 || memcmp(packet + 3, HEADER + 3, 4) != 0 ||
        packet[9] != 'D' || packet[10] != 1 || (packet[11] != TDOA_SYNC && packet[11] != TDOA_BLINK)) {
        return false;
    }
    uint16_t source = tdoa_read_le(packet + 7, 2);
    if (source == 0 || source == UINT16_MAX) {
        return false;
    }
    *frame = (tdoa_frame_t){.source = source, .kind = packet[11], .boot = tdoa_read_le(packet + 12, 4),
                            .sequence = tdoa_read_le(packet + 16, 4), .ticks = tdoa_read_le(timestamp, sizeof(timestamp))};
    return true;
}
