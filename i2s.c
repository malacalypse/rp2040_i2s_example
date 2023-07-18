/* i2s.c
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

#include "i2s.h"
#include <math.h>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "i2s.pio.h"

const i2s_config i2s_config_default = {48000, 256, 32, 10, 6, 7, 8, true};

static float pio_div(float freq, uint16_t* div, uint8_t* frac) {
    float clk   = (float)clock_get_hz(clk_sys);
    float ratio = clk / freq;
    float d;
    float f = modff(ratio, &d);
    *div    = (uint16_t)d;
    *frac   = (uint8_t)(f * 256.0f);

    // Use post-converted values to get actual freq after any rounding
    float result = clk / ((float)*div + ((float)*frac / 256.0f));

    return result;
}

static void calc_clocks(const i2s_config* config, pio_i2s_clocks* clocks) {
    // Try to get a precise ratio between SCK and BCK regardless of how
    // perfect the system_clock divides. First, see what sck we can actually get:
    float sck_desired   = (float)config->fs * (float)config->sck_mult * (float)i2s_sck_program_pio_mult;
    float sck_attained  = pio_div(sck_desired, &clocks->sck_d, &clocks->sck_f);
    clocks->fs_attained = sck_attained / (float)config->sck_mult / (float)i2s_sck_program_pio_mult;

    // Now that we have the closest fs our dividers will give us, we can
    // re-calculate SCK and BCK as correct ratios of this adjusted fs:
    float sck_hz       = clocks->fs_attained * (float)config->sck_mult;
    clocks->sck_pio_hz = pio_div(sck_hz * (float)i2s_sck_program_pio_mult, &clocks->sck_d, &clocks->sck_f);
    float bck_hz       = clocks->fs_attained * (float)config->bit_depth * 2.0f;
    clocks->bck_pio_hz = pio_div(bck_hz * (float)i2s_out_master_program_pio_mult, &clocks->bck_d, &clocks->bck_f);
}

static bool validate_sck_bck_sync(pio_i2s_clocks* clocks) {
    float ratio      = clocks->sck_pio_hz / clocks->bck_pio_hz;
    float actual_sck = clocks->sck_pio_hz / (float)i2s_sck_program_pio_mult;
    float actual_bck = clocks->bck_pio_hz / (float)i2s_out_master_program_pio_mult;
    printf("Clock speed for SCK: %f (PIO %f Hz with divider %d.%d)\n", actual_sck, clocks->sck_pio_hz, clocks->sck_d, clocks->sck_f);
    printf("Clock speed for BCK: %f (PIO %f Hz with divider %d.%d)\n", actual_bck, clocks->bck_pio_hz, clocks->bck_d, clocks->bck_f);
    printf("Clock Ratio: %f\n", ratio);
    float whole_ratio;
    float fractional_ratio = modff(ratio, &whole_ratio);
    return (fractional_ratio == 0.0f);
}

static void dma_double_buffer_init(pio_i2s* i2s, void (*dma_handler)(void)) {
    // Set up DMA for PIO I2s - two channels, in and out
    i2s->dma_ch_in_ctrl  = dma_claim_unused_channel(true);
    i2s->dma_ch_out_ctrl = dma_claim_unused_channel(true);
    i2s->dma_ch_out_data = dma_claim_unused_channel(true);
    i2s->dma_ch_in_data  = dma_claim_unused_channel(true);

    // Control blocks support double-buffering with interrupts on buffer change
    i2s->in_ctrl_blocks[0]  = i2s->input_buffer;
    i2s->in_ctrl_blocks[1]  = &i2s->input_buffer[STEREO_BUFFER_SIZE];
    i2s->out_ctrl_blocks[0] = i2s->output_buffer;
    i2s->out_ctrl_blocks[1] = &i2s->output_buffer[STEREO_BUFFER_SIZE];

    // DMA I2S OUT control channel - wrap read address every 8 bytes (2 words)
    // Transfer 1 word at a time, to the out channel read address and trigger.
    dma_channel_config c = dma_channel_get_default_config(i2s->dma_ch_out_ctrl);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_ring(&c, false, 3);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    dma_channel_configure(i2s->dma_ch_out_ctrl, &c, &dma_hw->ch[i2s->dma_ch_out_data].al3_read_addr_trig, i2s->out_ctrl_blocks, 1, false);

    c = dma_channel_get_default_config(i2s->dma_ch_out_data);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_chain_to(&c, i2s->dma_ch_out_ctrl);
    channel_config_set_dreq(&c, pio_get_dreq(i2s->pio, i2s->sm_dout, true));

    dma_channel_configure(i2s->dma_ch_out_data,
                          &c,
                          &i2s->pio->txf[i2s->sm_dout],  // Destination pointer
                          NULL,                          // Source pointer, will be set by ctrl channel
                          STEREO_BUFFER_SIZE,            // Number of transfers
                          false                          // Start immediately
    );

    c = dma_channel_get_default_config(i2s->dma_ch_in_ctrl);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_ring(&c, false, 3);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    dma_channel_configure(i2s->dma_ch_in_ctrl, &c, &dma_hw->ch[i2s->dma_ch_in_data].al2_write_addr_trig, i2s->in_ctrl_blocks, 1, false);

    c = dma_channel_get_default_config(i2s->dma_ch_in_data);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_chain_to(&c, i2s->dma_ch_in_ctrl);
    channel_config_set_dreq(&c, pio_get_dreq(i2s->pio, i2s->sm_din, false));

    dma_channel_configure(i2s->dma_ch_in_data,
                          &c,
                          NULL,                         // Will be set by ctrl chan
                          &i2s->pio->rxf[i2s->sm_din],  // Source pointer
                          STEREO_BUFFER_SIZE,           // Number of transfers
                          false                         // Don't start yet
    );

    // Input channel triggers the DMA interrupt handler, hopefully these stay
    // in perfect sync with the output.
    dma_channel_set_irq0_enabled(i2s->dma_ch_in_data, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Enable all the dma channels
    dma_channel_start(i2s->dma_ch_out_ctrl);  // This will trigger-start the out chan
    dma_channel_start(i2s->dma_ch_in_ctrl);   // This will trigger-start the in chan
}

/* Initializes an I2S block (of 3 state machines) on the designated PIO.
 * NOTE! This does NOT START the PIO units. You must call i2s_program_start
 *       with the resulting i2s object!
 */
static void i2s_slave_program_init(PIO pio, const i2s_config* config, pio_i2s* i2s) {
    uint offset  = 0;
    i2s->pio     = pio;
    i2s->sm_mask = 0;

    pio_i2s_clocks clocks;
    calc_clocks(config, &clocks);

    if (config->sck_enable) {
        // SCK block
        i2s->sm_sck = pio_claim_unused_sm(pio, true);
        i2s->sm_mask |= (1u << i2s->sm_sck);
        offset = pio_add_program(pio, &i2s_sck_program);
        i2s_sck_program_init(pio, i2s->sm_sck, offset, config->sck_pin);
        pio_sm_set_clkdiv_int_frac(pio, i2s->sm_sck, clocks.sck_d, clocks.sck_f);
    }

    // Bi-Di I2S block, clocked with SCK
    i2s->sm_din  = pio_claim_unused_sm(pio, true);
    i2s->sm_dout = i2s->sm_din;
    i2s->sm_mask |= (1u << i2s->sm_din);
    offset = pio_add_program(pio, &i2s_bidi_slave_program);
    i2s_bidi_slave_program_init(pio, i2s->sm_din, offset, config->dout_pin, config->din_pin);
    pio_sm_set_clkdiv_int_frac(pio, i2s->sm_din, clocks.sck_d, clocks.sck_f);
}

/* Initializes an I2S block (of 3 state machines) on the designated PIO.
 * NOTE! This does NOT START the PIO units. You must call i2s_program_start
 *       with the resulting i2s object!
 */
static void i2s_sync_program_init(PIO pio, const i2s_config* config, pio_i2s* i2s) {
    uint offset  = 0;
    i2s->pio     = pio;
    i2s->sm_mask = 0;

    pio_i2s_clocks clocks;
    calc_clocks(config, &clocks);

    if (config->sck_enable) {
        // Check that SCK and BCK are in perfect whole ratio
        if (!validate_sck_bck_sync(&clocks)) {
            /* There are lots of possible causes for this, a few are:
             *  - You are running a system clock frequency that doesn't divide well at all into SCK or BCK
             *  - You are running a 24-bit I2S with a 256x SCK multiplier (RP2040 cannot support this)
             *  - You have mucked with the PIO ratios or done something silly.
             */
            panic("SCK and BCK are not in sync.");
        }

        // SCK block
        i2s->sm_sck = pio_claim_unused_sm(pio, true);
        i2s->sm_mask |= (1u << i2s->sm_sck);
        offset = pio_add_program(pio, &i2s_sck_program);
        i2s_sck_program_init(pio, i2s->sm_sck, offset, config->sck_pin);
        pio_sm_set_clkdiv_int_frac(pio, i2s->sm_sck, clocks.sck_d, clocks.sck_f);
    }

    // In block, clocked with SCK
    i2s->sm_din = pio_claim_unused_sm(pio, true);
    i2s->sm_mask |= (1u << i2s->sm_din);
    offset = pio_add_program(pio, &i2s_in_slave_program);
    i2s_in_slave_program_init(pio, i2s->sm_din, offset, config->din_pin);
    pio_sm_set_clkdiv_int_frac(pio, i2s->sm_din, clocks.sck_d, clocks.sck_f);

    // Out block, clocked with BCK
    i2s->sm_dout = pio_claim_unused_sm(pio, true);
    i2s->sm_mask |= (1u << i2s->sm_dout);
    offset = pio_add_program(pio, &i2s_out_master_program);
    i2s_out_master_program_init(pio, i2s->sm_dout, offset, config->bit_depth, config->dout_pin, config->clock_pin_base);
    pio_sm_set_clkdiv_int_frac(pio, i2s->sm_dout, clocks.bck_d, clocks.bck_f);
}

void i2s_program_start_slaved(PIO pio, const i2s_config* config, void (*dma_handler)(void), pio_i2s* i2s) {
    if (((uint32_t)i2s & 0x7) != 0) {
        panic("pio_i2s argument must be 8-byte aligned!");
    }
    i2s_slave_program_init(pio, config, i2s);
    dma_double_buffer_init(i2s, dma_handler);
    pio_enable_sm_mask_in_sync(i2s->pio, i2s->sm_mask);
}

void i2s_program_start_synched(PIO pio, const i2s_config* config, void (*dma_handler)(void), pio_i2s* i2s) {
    if (((uint32_t)i2s & 0x7) != 0) {
        panic("pio_i2s argument must be 8-byte aligned!");
    }
    i2s_sync_program_init(pio, config, i2s);
    dma_double_buffer_init(i2s, dma_handler);
    pio_enable_sm_mask_in_sync(i2s->pio, i2s->sm_mask);
}
