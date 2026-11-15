#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "deca_device_api.h"
#include "deca_interface.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_dashboard.h"
#include "uwb_ranging.h"

enum {
    RADIO_MISO = GPIO_NUM_4,
    RADIO_CS = GPIO_NUM_5,
    RADIO_SCLK = GPIO_NUM_6,
    RADIO_MOSI = GPIO_NUM_7,
    RADIO_RESET = GPIO_NUM_20,
    SPI_BUFFER_SIZE = 1028,
    ANTENNA_DELAY = 16385, // Nominal bench value. Calibrate each assembled board.
    RX_TIMEOUT_UUS = 30000,
    MAX_INTERVAL_TICKS = 2200000000U, // ~34 ms, within one low-32-bit timestamp period.
    BROADCAST_ADDRESS = 0xFFFF,
    FRAME_CONTROL = 0x8841, // Data, compressed PAN ID, 16-bit source and destination.
    PAN_ID = 0x5755,
    FCS_SIZE = 2,
    HEADER_SIZE = 12,
    FRAME_SIZE = HEADER_SIZE + 8,
};

typedef enum {
    POLL = 1,
    RESPONSE,
    FINAL,
    REPORT,
} message_t;

typedef struct {
    uint16_t source;
    uint16_t destination;
    uint8_t sequence;
    message_t type;
    uint32_t round_trip;
    uint32_t reply;
} frame_t;

static const char *const TAG = "uwb";
static const uint32_t RX_EVENTS = SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_ERR |
                                  SYS_STATUS_ALL_RX_TO;
static spi_device_handle_t radio_spi;
static DMA_ATTR uint8_t spi_tx[SPI_BUFFER_SIZE];
static DMA_ATTR uint8_t spi_rx[SPI_BUFFER_SIZE];

static int32_t spi_read(uint16_t header_size, uint8_t *header, uint16_t size, uint8_t *data)
{
    if ((size_t)header_size + size > sizeof(spi_tx)) {
        return DWT_ERROR;
    }
    memcpy(spi_tx, header, header_size);
    memset(spi_tx + header_size, 0, size);
    spi_transaction_t transfer = {
        .length = (header_size + size) * 8,
        .tx_buffer = spi_tx,
        .rx_buffer = spi_rx,
    };
    if (spi_device_polling_transmit(radio_spi, &transfer) != ESP_OK) {
        memset(data, 0, size);
        return DWT_ERROR;
    }
    memcpy(data, spi_rx + header_size, size);
    return DWT_SUCCESS;
}

static int32_t spi_write(uint16_t header_size, const uint8_t *header,
                         uint16_t size, const uint8_t *data)
{
    if ((size_t)header_size + size > sizeof(spi_tx)) {
        return DWT_ERROR;
    }
    memcpy(spi_tx, header, header_size);
    if (size != 0) {
        memcpy(spi_tx + header_size, data, size);
    }
    spi_transaction_t transfer = {
        .length = (header_size + size) * 8,
        .tx_buffer = spi_tx,
    };
    return spi_device_polling_transmit(radio_spi, &transfer) == ESP_OK ? DWT_SUCCESS : DWT_ERROR;
}

// The radio task owns the SPI device and polls status without an IRQ handler.
decaIrqStatus_t decamutexon(void) { return 0; }
void decamutexoff(decaIrqStatus_t state) { (void)state; }
void deca_usleep(unsigned long us) { esp_rom_delay_us(us); }
void deca_sleep(unsigned int ms) { vTaskDelay(pdMS_TO_TICKS(ms) + 1); }
static void radio_awake(void) {} // The radio has just been reset and never enters sleep.

static esp_err_t init_radio(void)
{
    const spi_bus_config_t bus = {
        .mosi_io_num = RADIO_MOSI, .miso_io_num = RADIO_MISO, .sclk_io_num = RADIO_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = SPI_BUFFER_SIZE,
    };
    const spi_device_interface_config_t device = {
        .mode = 0, .clock_speed_hz = 2000000, .spics_io_num = RADIO_CS, .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI init");
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &device, &radio_spi), TAG, "SPI device");

    const gpio_config_t reset = {
        .pin_bit_mask = UINT64_C(1) << RADIO_RESET, .mode = GPIO_MODE_OUTPUT_OD,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "configure reset GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(RADIO_RESET, 0), TAG, "assert radio reset");
    deca_sleep(20);
    ESP_RETURN_ON_ERROR(gpio_set_level(RADIO_RESET, 1), TAG, "release radio reset");
    deca_sleep(100);

    uint8_t header = 0;
    uint8_t id_bytes[4] = {0};
    int32_t result = spi_read(1, &header, sizeof(id_bytes), id_bytes);
    uint32_t id = (uint32_t)id_bytes[0] | ((uint32_t)id_bytes[1] << 8) |
                  ((uint32_t)id_bytes[2] << 16) | ((uint32_t)id_bytes[3] << 24);
    node_dashboard_set_radio_id(id);
    ESP_LOGI(TAG, "DWM3000 SPI device ID 0x%08" PRIX32, id);
    ESP_RETURN_ON_FALSE(result == DWT_SUCCESS && id == UINT32_C(0xDECA0302),
                        ESP_ERR_NOT_FOUND, TAG, "DW3110 device ID missing");

    static struct dwt_spi_s spi = { .readfromspi = spi_read, .writetospi = spi_write };
    static const struct dwt_driver_s *drivers[] = { &dw3000_driver };
    struct dwt_probe_s probe = {
        .spi = &spi, .wakeup_device_with_io = radio_awake,
        .driver_list = (struct dwt_driver_s **)drivers, .dw_driver_num = 1,
    };
    ESP_RETURN_ON_FALSE(dwt_probe(&probe) == DWT_SUCCESS, ESP_FAIL, TAG, "select DW3110 driver");
    ESP_RETURN_ON_FALSE(dwt_initialise(DWT_DW_INIT) == DWT_SUCCESS, ESP_FAIL, TAG, "radio init");

    dwt_config_t phy = {
        .chan = 5, .txPreambLength = DWT_PLEN_128, .rxPAC = DWT_PAC8,
        .txCode = 9, .rxCode = 9, .sfdType = DWT_SFD_DW_8, .dataRate = DWT_BR_6M8,
        .phrMode = DWT_PHRMODE_STD, .phrRate = DWT_PHRRATE_STD, .sfdTO = 129,
        .stsMode = DWT_STS_MODE_OFF, .stsLength = DWT_STS_LEN_64, .pdoaMode = DWT_PDOA_M0,
    };
    dwt_txconfig_t tx = { .PGdly = 0x34, .power = UINT32_C(0xFDFDFDFD), .PGcount = 0 };
    ESP_RETURN_ON_FALSE(dwt_configure(&phy) == DWT_SUCCESS, ESP_FAIL, TAG, "configure channel 5");
    dwt_configuretxrf(&tx);
    dwt_settxantennadelay(ANTENNA_DELAY);
    dwt_setrxantennadelay(ANTENNA_DELAY);
    dwt_setrxaftertxdelay(0);
    ESP_LOGI(TAG, "DW3110 ready for DS-TWR on channel 5");
    return ESP_OK;
}

static void put_le(uint8_t *data, uint32_t value, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        data[i] = value >> (8 * i);
    }
}

static uint32_t get_le(const uint8_t *data, size_t size)
{
    uint32_t value = 0;
    for (size_t i = 0; i < size; i++) {
        value |= (uint32_t)data[i] << (8 * i);
    }
    return value;
}

static uint32_t wait_status(uint32_t mask)
{
    const int64_t deadline = esp_timer_get_time() + 55000;
    do {
        uint32_t status = dwt_readsysstatuslo();
        if (status & mask) {
            return status;
        }
        vTaskDelay(1);
    } while (esp_timer_get_time() < deadline);
    return 0;
}

static bool send_frame(const frame_t *frame, bool expect_reply)
{
    uint8_t bytes[FRAME_SIZE] = {0};
    put_le(bytes, FRAME_CONTROL, 2);
    bytes[2] = frame->sequence;
    put_le(bytes + 3, PAN_ID, 2);
    put_le(bytes + 5, frame->destination, 2);
    put_le(bytes + 7, frame->source, 2);
    bytes[9] = 'U';
    bytes[10] = 1; // Protocol version.
    bytes[11] = frame->type;
    size_t length = HEADER_SIZE;
    if (frame->type == REPORT) {
        put_le(bytes + 12, frame->round_trip, 4);
        put_le(bytes + 16, frame->reply, 4);
        length = FRAME_SIZE;
    }

    dwt_writesysstatuslo(RX_EVENTS | DWT_INT_TXFRS_BIT_MASK);
    dwt_setrxtimeout(RX_TIMEOUT_UUS);
    if (dwt_writetxdata(length, bytes, 0) != DWT_SUCCESS) {
        return false;
    }
    dwt_writetxfctrl(length + FCS_SIZE, 0, 1);
    uint8_t mode = DWT_START_TX_IMMEDIATE | (expect_reply ? DWT_RESPONSE_EXPECTED : 0);
    return dwt_starttx(mode) == DWT_SUCCESS && wait_status(DWT_INT_TXFRS_BIT_MASK);
}

static bool read_frame(frame_t *frame, uint32_t *timestamp, uint32_t *rx_status)
{
    uint32_t status = wait_status(DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO);
    if (rx_status != NULL) {
        *rx_status = status;
    }
    if (!(status & DWT_INT_RXFCG_BIT_MASK)) {
        if (status != 0 && !(status & DWT_INT_RXFTO_BIT_MASK)) {
            ESP_LOGD(TAG, "RX status 0x%08" PRIX32, status);
        }
        return false;
    }
    uint8_t ranging;
    uint16_t length = dwt_getframelength(&ranging);
    if (length != HEADER_SIZE + FCS_SIZE && length != FRAME_SIZE + FCS_SIZE) {
        return false;
    }
    uint8_t bytes[FRAME_SIZE];
    dwt_readrxdata(bytes, length - FCS_SIZE, 0);
    if (get_le(bytes, 2) != FRAME_CONTROL || get_le(bytes + 3, 2) != PAN_ID ||
        bytes[9] != 'U' || bytes[10] != 1 || bytes[11] < POLL || bytes[11] > REPORT ||
        (length == FRAME_SIZE + FCS_SIZE) != (bytes[11] == REPORT)) {
        return false;
    }
    *frame = (frame_t){
        .source = get_le(bytes + 7, 2), .destination = get_le(bytes + 5, 2),
        .sequence = bytes[2], .type = bytes[11],
    };
    if (frame->source == 0 || frame->source == BROADCAST_ADDRESS) {
        return false;
    }
    if (frame->type == REPORT) {
        frame->round_trip = get_le(bytes + 12, 4);
        frame->reply = get_le(bytes + 16, 4);
    }
    *timestamp = dwt_readrxtimestamplo32(DWT_COMPAT_NONE);
    return true;
}

static bool matches(const frame_t *incoming, const frame_t *outgoing, message_t type)
{
    return incoming->type == type && incoming->sequence == outgoing->sequence &&
           incoming->destination == outgoing->source && incoming->source != outgoing->source &&
           (outgoing->destination == BROADCAST_ADDRESS || incoming->source == outgoing->destination);
}

static bool range_tag(uint16_t id, uint8_t sequence)
{
    frame_t poll = {
        .source = id, .destination = BROADCAST_ADDRESS, .sequence = sequence, .type = POLL,
    };
    if (!send_frame(&poll, true)) {
        ESP_LOGW(TAG, "Poll TX did not finish");
        return false;
    }
    uint32_t poll_tx = dwt_readtxtimestamplo32();
    frame_t response;
    uint32_t response_rx;
    if (!read_frame(&response, &response_rx, NULL) || !matches(&response, &poll, RESPONSE)) {
        return false;
    }

    frame_t final = poll;
    final.destination = response.source;
    final.type = FINAL;
    // The REPORT carries the responder's intervals. We use the actual FINAL TX
    // timestamp locally, so neither peer predicts a transmit timestamp.
    if (!send_frame(&final, true)) {
        return false;
    }
    uint32_t final_tx = dwt_readtxtimestamplo32();
    frame_t report;
    uint32_t report_rx;
    if (!read_frame(&report, &report_rx, NULL) || !matches(&report, &final, REPORT)) {
        return false;
    }

    uint32_t round_a = response_rx - poll_tx;
    uint32_t reply_a = final_tx - response_rx;
    uint32_t round_b = report.round_trip;
    uint32_t reply_b = report.reply;
    if (round_a == 0 || round_b == 0 || reply_a == 0 || reply_b == 0 ||
        round_a > MAX_INTERVAL_TICKS || round_b > MAX_INTERVAL_TICKS ||
        reply_a > MAX_INTERVAL_TICKS || reply_b > MAX_INTERVAL_TICKS) {
        return false;
    }
    int64_t numerator = (int64_t)round_a * round_b - (int64_t)reply_a * reply_b;
    double tof_ticks = numerator / ((double)round_a + round_b + reply_a + reply_b);
    double meters = tof_ticks * DWT_TIME_UNITS * 299702547.0;
    ESP_LOGI(TAG, "TWR tag %u -> anchor %u seq %u: %.3f m",
             id, report.source, sequence, meters);
    return true;
}

static void range_anchor(uint16_t id, bool *listening)
{
    if (!*listening) {
        dwt_writesysstatuslo(RX_EVENTS);
        dwt_setrxtimeout(0);
        if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
            return;
        }
        *listening = true;
    }
    frame_t poll;
    uint32_t poll_rx;
    uint32_t status;
    bool received = read_frame(&poll, &poll_rx, &status);
    if (!status) {
        return; // Keep RX enabled across host poll timeouts.
    }
    *listening = false;
    if (!received || poll.type != POLL || poll.source == id ||
        (poll.destination != BROADCAST_ADDRESS && poll.destination != id)) {
        return;
    }

    frame_t response = {
        .source = id, .destination = poll.source, .sequence = poll.sequence, .type = RESPONSE,
    };
    if (!send_frame(&response, true)) {
        return;
    }
    uint32_t response_tx = dwt_readtxtimestamplo32();
    frame_t final;
    uint32_t final_rx;
    if (!read_frame(&final, &final_rx, NULL) || !matches(&final, &response, FINAL)) {
        return;
    }

    frame_t report = response;
    report.type = REPORT;
    report.round_trip = final_rx - response_tx;
    report.reply = response_tx - poll_rx;
    if (send_frame(&report, false)) {
        ESP_LOGI(TAG, "TWR anchor %u replied to tag %u seq %u", id, poll.source, poll.sequence);
    }
}

static void ranging_task(void *arg)
{
    (void)arg;
    if (init_radio() != ESP_OK) {
        ESP_LOGE(TAG, "Ranging unavailable");
        vTaskDelete(NULL);
        return;
    }

    uint8_t sequence = 0;
    uint32_t previous_role = UINT32_MAX;
    bool listening = false;
    while (true) {
        uint16_t id;
        bool anchor;
        node_dashboard_get_ranging_config(&id, &anchor);
        if (id == 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        uint32_t role = ((uint32_t)id << 1) | anchor;
        if (role != previous_role) {
            ESP_LOGI(TAG, "Ranging as %s %u", anchor ? "anchor" : "tag", id);
            previous_role = role;
            dwt_forcetrxoff();
            listening = false;
        }
        if (anchor) {
            range_anchor(id, &listening);
            if (!listening) {
                dwt_forcetrxoff();
            }
        } else {
            if (!range_tag(id, sequence)) {
                ESP_LOGW(TAG, "TWR tag %u seq %u: exchange incomplete", id, sequence);
            }
            sequence++;
            dwt_forcetrxoff();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

esp_err_t uwb_ranging_start(void)
{
    return xTaskCreate(ranging_task, "uwb_ranging", 6144, NULL, 6, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
