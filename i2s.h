/* i2s.h
 *
 * Author: Daniel Collins
 * Date:   2022-02-25
 *
 * Copyright (c) 2022 Daniel Collins
 *
 * This file is part of rp2040_i2s_example.
 *
 * rp2040_i2s_example is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3 as published by the
 * Free Software Foundation.
 *
 * rp2040_i2s_example is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * rp2040_i2s_example. If not, see <https://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include "hardware/pio.h"

#ifndef I2S_TEST_I2S_H
#define I2S_TEST_I2S_H

#define AUDIO_BUFFER_FRAMES 48
#define STEREO_BUFFER_SIZE  AUDIO_BUFFER_FRAMES * 2  // roughly 1ms, 48 L + R words

typedef struct i2s_config {
    uint32_t fs;
    uint32_t sck_mult;
    uint8_t  bit_depth;
    uint8_t  sck_pin;
    uint8_t  dout_pin;
    uint8_t  din_pin;
    uint8_t  clock_pin_base;
    bool     sck_enable;
} i2s_config;

typedef struct pio_i2s_clocks {
    // Clock computation results
    float fs_attained;
    float sck_pio_hz;
    float bck_pio_hz;

    // PIO divider ratios to obtain the computed clocks above
    uint16_t sck_d;
    uint8_t  sck_f;
    uint16_t bck_d;
    uint8_t  bck_f;
} pio_i2s_clocks;

// NOTE: Use __attribute__ ((aligned(8))) on this struct or the DMA wrap won't work!
typedef struct pio_i2s {
    PIO        pio;
    uint8_t    sm_mask;
    uint8_t    sm_sck;
    uint8_t    sm_dout;
    uint8_t    sm_din;
    uint       dma_ch_in_ctrl;
    uint       dma_ch_in_data;
    uint       dma_ch_out_ctrl;
    uint       dma_ch_out_data;
    int32_t*   in_ctrl_blocks[2];  // Control blocks MUST have 8-byte alignment.
    int32_t*   out_ctrl_blocks[2];
    int32_t    input_buffer[STEREO_BUFFER_SIZE * 2];
    int32_t    output_buffer[STEREO_BUFFER_SIZE * 2];
    i2s_config config;
} pio_i2s;

extern const i2s_config i2s_config_default;

void i2s_program_start_slaved(PIO pio, const i2s_config* config, void (*dma_handler)(void), pio_i2s* i2s);
void i2s_program_start_synched(PIO pio, const i2s_config* config, void (*dma_handler)(void), pio_i2s* i2s);

#endif  // I2S_TEST_I2S_H
