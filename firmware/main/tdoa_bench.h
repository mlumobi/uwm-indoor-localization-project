#pragma once

#include <stdint.h>

// Runs in the existing radio task after the DW3110 is initialized.
void tdoa_bench_run(uint16_t node_id);
