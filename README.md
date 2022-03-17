# i2s_example

Raspberry Pi Pico full-featured I2S PIO with system clock and bi-directional, double-buffered DMA support. Intended to
be used with all common codecs, but modular enough to support unidirectional DAC or ADC implementations as well.

# Included PIO Programs

This package includes multiple PIO programs which are intended to be used either alone or in combinations to accomplish
common I2S tasks. They comprise:

- i2s_sck : A system clock (or master clock) output-only module.
- i2s_out_master: An output module (for writing to a DAC) which generates BCK and LRCK synchronously with the i2s_sck
  module, if required, or can standalone if not.
- i2s_in_slave: An input (from ADC) module which synchronizes to externally generated BCK and LRCK. Designed to
  synchronize to the i2s_out_master block above for internal synchronization as a bi-directional master.
- i2s_bidi_slave: A bi-directional (in and out) codec interface in slave mode (requires externally generated BCK and
  LRCK, usually from the codec).

There is not a standalone output slave module nor input master, but one can easily be designed from the above options if
needed.

## API

For the two most common codec use cases (bi-directional master and bi-directional slave), two helper functions exist
which set up double-buffered DMA and initialize the state machines with proper clock ratios - if possible (see sections
below discussing clocks, especially if you require a system or master clock output in addition to the standard BCK/LRCK
clocks).

In order to process the audio data coming from either of these units, a dma_handler is necessary. See the example
included in `i2s_example.c`

### i2s_program_start_slaved

```c
void i2s_program_start_slaved(PIO pio, const i2s_config* config, void (*dma_handler)(void), pio_i2s* i2s);
```

This sets up the i2s_bidi_slave program with an input and output module sharing the same BCK and LRCK as inputs from the
codec. It can optionally generate SCK as a master **output**, since some codecs require this externally even if they are
generating the BCK/LRCK signals. If sck_enable is true in the config, it will generate SCK. If not, the pin is not used.

### is2_program_start_synched

```c
void i2s_program_start_synched(PIO pio, const i2s_config* config, void (*dma_handler)(void), pio_i2s* i2s);
```

This sets up the PIO as bi-directional master for the BCK and LRCK, and can optionally generate SCK as a master **
output** as well. The output and input modules are synchronized to the internally generated BCK/LRCK and these signals
are output on the appropriate pins to the codec as well.

# Understanding I2S

## `fs`, the sample frequency

The fundamental rate at which your audio system is clocked is called the sample frequency, also referred to as `fs`.
Usually this is 44.1kHz or 48kHz. I2S has other clocks that are related to this fundamental frequency by exact integer
multiples. So, keep in mind that all of the actual clock frequencies are relative to `fs` by the appropriate integer
multiples.

## I2S core signals

I2S has the following signals:

- SCK (system or master clock)    : **[optional]** usually 256 or 384 * fs
- BCK (bit clock)                 : fs * 2 * bit depth (either 24 or 32)
- LRCK (word, frame, or LR clock) : fs
- Data In (from the ADC)          : **[optional]** runs at BCK rate
- Data Out (to the DAC)           : **[optional]** runs at BCK rate

The constant multiple of 2 in the bit clock is because the I2S standard inherently supports stereo audio and divides the
audio frames into left and right words via the LRCK.

One of either Data In or Data Out are, of course, required for this to be useful, but not all peripherals have both -
you might be talking only to a DAC or only to an ADC, not a codec (which is just a DAC and an ADC in the same package,
possibly sharing clocks, possibly not).

Additionally, not all peripherals require master clock, some have a PLL which can generate it from the bit clock, in
that case the timing is made much easier for the Pico.

## Master Clock (SCK/System Clock)

**NOTE**: For peripherals which DO require master clock, the relationship between SCK and the BCK of the I2S stream must
be *exact* - that is, they must both *perfectly* divide into the word clock precisely. If you wish to hit traditional
sample rates like 44.1kHz or 48kHz, you need to choose a system clock that can evenly divide both the master and the bit
clock into the necessary multiples. Some combinations are therefore not possible, since the Pico PIO is only a divider
and does not incorporate a multiplier. For instance, with 24-bit audio there can end up being a factor of 3 involved
which can only be approximated, but not exactly reached with the divisors, so ratios of SCK and BCK that involve a
relationship of 1/3 cannot be correctly implemented. In addition, some system clock speeds will not produce clean
divisions for both simultaneously.

A table of system clocks and appropriate scaling values for some common I2S relationships are below:

fs     | Pico system clock   |  SCK (mult / Hz)  |   BCK (bits / Hz)    |
 -------|---------------------|-------------------|----------------------|
48,000 |       129,600,000   | 384 / 18,432,000  |    24 / 2,304,000    |
48,000 |       132,000,000   | 256 / 12,288,000  |    32 / 3,072,000    |

Note: These frequencies are *one half* of the frequency to set up the pio state machine for - since the PIO modules
output one bit per TWO clocks, so the sm clocks for the above table would be:

fs     | Pico system clock   | SM_SCK (mult / Hz) | SM_BCK (bits / Hz) |
-------|---------------------|--------------------|--------------------|
48,000 |       129,600,000   | 384 / 36,864,000   | 24 / 4,608,000     |
48,000 |       132,000,000   | 256 / 24,576,000   | 32 / 6,144,000     |

Note: These routines will NOT set up the system clock for you. If the system clock is off, these relationship may not
result in even divisions of the word clock and your peripheral may glitch.

## Recommended choice for codecs

The most robust synchronized (out and in share BCK/LRCK) setup is to use out_master and in_slave, setting the input
BCK/LRCK pins to the same as the output BCK/LRCK.

## Pinouts

Pin assignment is pretty straightforward:

- SCK: pretty much any pin you like
- DOUT: pretty much any pin you like
- DIN: the first of three consecutive, free pins
- Clock_Pin_Base: the first of the two pins consecutively above DIN, if using DIN. If not, any two consecutive pins.

For instance, a useful set of choices on a default Pico would be:

- SCK: 10
- DOUT: 6
- DIN: 7
- Clock_Pin_Base: 8

This consumes pins 6-10, inclusive, for a fully bidirectional codec requiring system clock. If the codec permits
synchronizing both the DAC and ADC to the same clocks, this works fine. If the codec requires independent clocks for
both units, just route the clock lines to both modules physically. No additional clocks or pins are needed.

## A note about bit depth

Most I2S peripherals are either 24-bit or 16-bit, however it is very common to see both of these bit depths being
transported over 32-bit I2S frames. Per the I2S standard, they are MSB-first. Some peripherals allow other protocols
which are similar to, but not exactly, I2S standard, such as left- or right-aligned 16, 24, or 32-bit frames.

This PIO module follows the Philips I2S standard and emits a 32-bit frame. The DMA data should be 32 bit words, and you
should **not** pack two stereo 16-bit frames into a single word. Alignment of the data within the frame is up to you -
whatever alignment it has in memory is what will be transmitted, MSB-first, on the I2S bus.

For the most common use case of traditional 24-bit codecs such as the TI PCM3060 or the WM8731, this PIO is already
perfectly configured. Use 32-bit signed integers (int32_t) for the buffers. Received data will be in the exact same
format.

To be very clear, if this is the I2S frame "on the wire" in 24-bit audio mode with a 32-bit frame word size:

```
LRCK    0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1  1
BCK    01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01
Dxx     - 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0  -  -  -  -  -  -  -  - 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0  -  -  -  -  -  -  - 
```

The bits for DXX will come from or be written to a 32-bit memory location as follows:

```
bit  MSB 31 30 29 28 27 26 25 242 3 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0 LSB
Dxx      23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0  -  -  -  -  -  -  -  - 
```

Note that the I2S format offsets the alignment of the MSB in the frame by one bit from the edge of LRCK. This is
intentional.

## License

The full text of the license should be found in LICENSE.txt, included as part of this repository.

This library and all accompanying code, examples, information and documentation is Copyright (C) 2022 Daniel
Collins

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License, version 3 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not,
see <https://www.gnu.org/licenses/>.
