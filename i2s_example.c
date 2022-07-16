/* i2s_examples.c
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

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "i2s.h"
#include "pico/stdlib.h"

// I2C defines
// This example uses I2C0 on GPIO4 (SDA) and GPIO5 (SCL) running at 100KHz.
// Connect the codec I2C control to this. (Codec-specific customization is
// not part of this example.)
#define I2C_PORT i2c0
#define I2C_SDA  4
#define I2C_SCL  5

#ifndef PICO_DEFAULT_LED_PIN
#warning blink example requires a board with a regular LED
#else
const uint LED_PIN = PICO_DEFAULT_LED_PIN;
#endif

static __attribute__((aligned(8))) pio_i2s i2s;

static void process_audio(const int32_t* input, int32_t* output, size_t num_frames) {
    // Just copy the input to the output
    for (size_t i = 0; i < num_frames * 2; i++) {
        output[i] = input[i];
    }
}

static void dma_i2s_in_handler(void) {
    /* We're double buffering using chained TCBs. By checking which buffer the
     * DMA is currently reading from, we can identify which buffer it has just
     * finished reading (the completion of which has triggered this interrupt).
     */
    if (*(int32_t**)dma_hw->ch[i2s.dma_ch_in_ctrl].read_addr == i2s.input_buffer) {
        // It is inputting to the second buffer so we can overwrite the first
        process_audio(i2s.input_buffer, i2s.output_buffer, AUDIO_BUFFER_FRAMES);
    } else {
        // It is currently inputting the first buffer, so we write to the second
        process_audio(&i2s.input_buffer[STEREO_BUFFER_SIZE], &i2s.output_buffer[STEREO_BUFFER_SIZE], AUDIO_BUFFER_FRAMES);
    }
    dma_hw->ints0 = 1u << i2s.dma_ch_in_data;  // clear the IRQ
}

int main() {
    // Set a 132.000 MHz system clock to more evenly divide the audio frequencies
    set_sys_clock_khz(132000, true);
    stdio_init_all();

    printf("System Clock: %lu\n", clock_get_hz(clk_sys));

    // Init GPIO LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // I2C Initialisation. Using it at 100Khz.
    i2c_init(I2C_PORT, 100 * 1000);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_set_pulls(I2C_SDA, true, false);
    gpio_set_pulls(I2C_SCL, true, false);
    gpio_set_drive_strength(I2C_SDA, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(I2C_SCL, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(I2C_SDA, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(I2C_SCL, GPIO_SLEW_RATE_FAST);

    // Here, do whatever you need to set up your codec for proper operation.
    // Some codecs require register configuration over I2C, for example.

    // Note: it is usually best to configure the codec here, and then enable it
    //       after starting the I2S clocks, below.

    i2s_program_start_synched(pio0, &i2s_config_default, dma_i2s_in_handler, &i2s);

    // Enable the (already configured) codec here.

    puts("i2s_example started.");

    // Blink the LED so we know we started everything correctly.

    while (true) {
        gpio_put(LED_PIN, 1);
        sleep_ms(250);
        gpio_put(LED_PIN, 0);
        sleep_ms(250);
    }

    return 0;
}
