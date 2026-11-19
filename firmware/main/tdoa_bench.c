#include "tdoa_bench.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "tdoa_radio.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { SYNC_INTERVAL_US = 200000 };

_Static_assert(UWM_TDOA_MAIN_ID > 0 && UWM_TDOA_MAIN_ID < 65535, "Invalid main anchor ID");
_Static_assert(UWM_TDOA_TAG_ID > 0 && UWM_TDOA_TAG_ID < 65535, "Invalid tag ID");
_Static_assert(UWM_TDOA_MAIN_ID != UWM_TDOA_TAG_ID, "Bench IDs must be distinct");

void tdoa_bench_run(uint16_t node_id)
{
    if (node_id == 0) {
        puts("TDOA_BENCH missing saved Node ID");
        return;
    }
    const bool tag = node_id == UWM_TDOA_TAG_ID;
    const bool main = node_id == UWM_TDOA_MAIN_ID;
    const uint8_t kind = tag ? TDOA_BLINK : TDOA_SYNC;
    const uint32_t boot = esp_random();
    uint32_t sequence = 0;
    int64_t next = 0;
    bool listening = false;
    printf("TDOA_BENCH ready %s %u %" PRIu32 "\n", tag ? "tag" : main ? "main" : "anchor", node_id, boot);
    while (true) {
        tdoa_frame_t received;
        if (!tag && tdoa_radio_receive(&received, &listening) &&
            ((received.kind == TDOA_BLINK && received.source == UWM_TDOA_TAG_ID) ||
             (received.kind == TDOA_SYNC && received.source == UWM_TDOA_MAIN_ID))) {
            printf("TDOA_BENCH rx %s %u %" PRIu32 " %u %" PRIu32 " %" PRIu32 " %" PRIu64 "\n",
                   received.kind == TDOA_SYNC ? "sync" : "blink", node_id, boot, received.source,
                   received.boot, received.sequence, received.ticks);
        }
        const int64_t now = esp_timer_get_time();
        if ((tag || main) && now >= next) {
            tdoa_frame_t frame = {.source = node_id, .kind = kind, .boot = boot, .sequence = sequence++};
            const bool sent = tdoa_radio_transmit(&frame);
            next = now + (tag ? 80000 + esp_random() % 40001 : SYNC_INTERVAL_US);
            listening = main && sent;
            if (sent) {
                printf("TDOA_BENCH tx %s %u %" PRIu32 " %" PRIu32 " %" PRIu64 "\n",
                       tag ? "blink" : "sync", node_id, boot, frame.sequence, frame.ticks);
            } else {
                printf("TDOA_BENCH tx_failed %s %u %" PRIu32 " %" PRIu32 "\n",
                       tag ? "blink" : "sync", node_id, boot, frame.sequence);
            }
        }
        vTaskDelay(1);
    }
}
