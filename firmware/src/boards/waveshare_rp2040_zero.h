// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

// Waveshare RP2040-Zero. Based on the Pico SDK's waveshare_rp2040_zero.h, which this
// file replaces (src/boards is searched first), plus what hid-remapper needs.

// pico_cmake_set PICO_PLATFORM=rp2040

#ifndef _BOARDS_WAVESHARE_RP2040_ZERO_H
#define _BOARDS_WAVESHARE_RP2040_ZERO_H

// For board detection
#define WAVESHARE_RP2040_ZERO
#define WAVESHARE_RP2040_ZERO_BOARD

// USB host (PIO-USB) on GP0 (D+) and GP1 (D-)
#define PICO_DEFAULT_PIO_USB_DP_PIN 0

// GPIOs usable for mapping: GP2-11, GP14, GP15, GP26-29 (edge pins) and GP17-25 (bottom pads).
// Not GP0/1 (USB host), GP12/13 (UART), GP16 (WS2812 LED).
#define GPIO_VALID_PINS_BASE 0b00111111111111101100111111111100

// --- UART ---
// UART0 on its alternate pins GP12/GP13 (Waveshare's default GP0/GP1 is the USB host)
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 12
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 13
#endif

// --- WS2812 ---
#ifndef PICO_DEFAULT_WS2812_PIN
#define PICO_DEFAULT_WS2812_PIN 16
#endif

// --- FLASH ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 4
#endif

// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (2 * 1024 * 1024)
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif
// All boards have B1 RP2040
#ifndef PICO_RP2040_B0_SUPPORTED
#define PICO_RP2040_B0_SUPPORTED 0
#endif

#endif
