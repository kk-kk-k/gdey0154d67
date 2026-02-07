/**
 * @file gdey0154d67_private.h
 * @author Jakub Kral (jakub6kral@centrum.cz)
 * @brief Internal GDEY0154D67 API declaration.
 * @details The driver works as follows:
 *   - esp_lcd_new_panel_gdey0154d67() returns a handle to a structure
 *       containing several function pointers (e.g., reset, init, draw_bitmap)
 *       which are assigned to the appropriate functions (e.g., gdey0154d67_reset, gdey0154d67_init, gdey0154d67_draw_bitmap).
 *   - Whenever user calls some function from esp_lcd library,
 *       the esp_lcd internally calls display-specific functions (given by the function pointers)
 *       (etc. gdey0154d67_reset, gdey0154d67_init, gdey0154d67_draw_bitmap)
 *       which perform the middle level communication (etc. sending appropriate commands).
 *   - Display-specific functions call low-level functions
 *       (etc. esp_lcd_panel_io_tx_param, esp_lcd_panel_io_tx_color)
 *       which handle the low-level SPI communication.
 *       The low-level SPI communication parameters (e.g., pins, clock speed...) are given by the user
 *       when initializing SPI handles (esp_lcd_panel_dev_config_t and esp_lcd_panel_io_handle_t).
 * @note This header should be included ONLY in gdey0154d67.c.
 * @version 0.1
 * @date 2026-01-30
 * @copyright 2026 Jakub Kral (jakub6kral@centrum.cz)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 * 
 */

#include <stdbool.h>                    // bool
#include "esp_err.h"                    // esp_err_t
#include "driver/gpio.h"                // gpio_num_t
#include "esp_lcd_panel_interface.h"    // esp_lcd_panel_t, esp_lcd_panel_handle_t
#include "freertos/FreeRTOS.h"          // SemaphoreHandle_t
#include "gdey0154d67.h"                // esp_lcd_gdey0154d67_vendor_config_t

#pragma once

/**
 * @brief ESP-IDF logging tag
 * 
 */
static const char * const gdey0154d67_tag = "GDEY0154D67_driver";


// Display dimensions
#define GDEY0154D67_H_RES 200                  // Display width
#define GDEY0154D67_V_RES 200                  // Display height

// ---------------------
// Panel command codes
// ---------------------

// Power & Reset
#define GDEY0154D67_CMD_SW_RST 0x12             // Software reset command
#define GDEY0154D67_CMD_DEEP_SLEEP 0x10         // Deep sleep control

// RAM access
#define GDEY0154D67_CMD_RAM_X_RANGE_SETUP 0x44  // X range setup
#define GDEY0154D67_CMD_RAM_Y_RANGE_SETUP 0x45  // Y range setup
#define GDEY0154D67_CMD_SET_RAM_X_ADDR 0x4e     // Setup RAM X address
#define GDEY0154D67_CMD_SET_RAM_Y_ADDR 0x4f     // Setup RAM Y address
#define GDEY0154D67_CMD_WRITE_BW_RAM 0x24       // Write BW RAM
#define GDEY0154D67_CMD_WRITE_RED_RAM 0x26      // Write Red RAM. Used for setting whole red RAM to white (Needed for fast updates).

// Display control
#define GDEY0154D67_CMD_DISP_UPDATE_CTRL_1 0x21 // Display update control 1
#define GDEY0154D67_CMD_DISP_UPDATE_CTRL_2 0x22 // Display update control 2
#define GDEY0154D67_CMD_MASTER_ACTIVATION 0x20  // Master activation command (Screen update)
#define GDEY0154D67_CMD_DRIVER_OUTPUT_CTRL 0x01 // Output control
#define GDEY0154D67_CMD_DATA_ENTRY_MODE_SETUP 0x11  // Data entry mode setup
#define GDEY0154D67_CMD_BORDER_CTRL 0x3c        // Border control setup

// Temperature control
#define GDEY0154D67_CMD_TEMP_SENS_CTRL 0x18     // Temperature sensor control
#define GDEY0154D67_CMD_TEMP_REG_WRITE 0x1A     // Write to the temperature register

/**
 * @brief Private GDEY0154D67 structure.
 * 
 */
typedef struct {
    esp_lcd_panel_t base;               // Pointer to the parent generic LCD display. This allows extraction of gdey0154d67_display_t from esp_lcd_panel_t.
    esp_lcd_panel_io_handle_t io;       // Low-level IO device handle. Used with esp_lcd_panel_io_tx_param() and esp_lcd_panel_io_tx_color().
    gpio_num_t reset_pin;               // GPIO reset pin
    esp_lcd_gdey0154d67_vendor_config_t vendor_cfg; // Vendor configuration copy.
    esp_lcd_gdey0154d67_update_mode_t update_mode;  // Update type needed
    SemaphoreHandle_t busy_semaphore;   // Busy semaphore used for awaiting screen update.
                                        // The semaphore given in gdey0154d67_busy_ISR() and taken in esp_lcd_gdey0154d67_await_busy().
} gdey0154d67_display_t;

/**
 * @brief Extract gdey0154d67_display_t from esp_lcd_panel_t via the __containerof macro.
 * @note This works ONLY when esp_lcd_panel_t base is embedded into gdey0154d67_display_t.
 * 
 * @param[in] panel Generic esp-idf lcd display child object.
 * @return gdey0154d67_display_t* Extracted parent object.
 */
static inline gdey0154d67_display_t * gdey0154d67_extract_display(esp_lcd_panel_handle_t panel) {
    ESP_RETURN_ON_FALSE(panel != NULL, NULL, gdey0154d67_tag, "Invalid panel arg");
    return __containerof(panel, gdey0154d67_display_t, base);
}

/**
 * @brief Busy ISR callback function.
 *   This ISR only signals that the display became ready.
 * @note This function gives the semaphore
 *   which is awaited by esp_lcd_gdey0154d67_await_busy().
 * 
 * @param[in] self gdey0154d67_display_t * to the corresponding device.
 */
static IRAM_ATTR void gdey0154d67_busy_isr(void * self);

/**
 * @brief Wait until the display becomes available.
 * @note The display is not available while performing screen refresh.
 *   This function is automatically called in gdey0154d67_draw_bitmap().
 * 
 * @param[in] self Corresponding device handle.
 * @return esp_err_t Error code.
 */
static esp_err_t esp_lcd_gdey0154d67_await_busy(esp_lcd_panel_handle_t self);

/**
 * @brief Reset LCD display
 * 
 * @param[in] self Corresponding display handle.
 * @return esp_err_t Error code.
 */
static esp_err_t gdey0154d67_reset(esp_lcd_panel_t * self);

/**
 * @brief Initialize LCD panel.
 * 
 * @param[in] self Corresponding display handle.
 * @return esp_err_t Error code.
 */
static esp_err_t gdey0154d67_init(esp_lcd_panel_t * self);

/**
 * @brief Deinitialize the LCD panel.
 * 
 * @param[in] self Corresponding display handle.
 * @return esp_err_t Error code.
 */
static esp_err_t gdey0154d67_del(esp_lcd_panel_t * self);

/**
 * @brief Draw bitmap on the LCD panel.
 * 
 * @param[in] self Corresponding display handle.
 * @param[in] x_start X-Axis start pixel index. The x_start is included.
 * @param[in] y_start Y-Axis start pixel index. The y_start is included.
 * @param[in] x_end X-Axis end pixel index. The x_end is NOT included.
 * @param[in] y_end Y-Axis end pixel index. The y_end is NOT included.
 * @param[in] color_data BW color data (1 = white, 0 = black, 8 pixels per byte, horizontal arrangement).
 * @return esp_err_t Error code.
 */
static esp_err_t gdey0154d67_draw_bitmap(esp_lcd_panel_t *self, int x_start, int y_start, int x_end, int y_end, const void *color_data);

/**
 * @brief Invert the color (bit-wise invert the color data line).
 * @note Sends invert color command.
 *   The changes are applied on the next screen redraw (see gdey0154d67_draw_bitmap).
 * 
 * @param[in] self Corresponding display handle.
 * @param[in] invert_color_data Whether to invert the color data
 * @return esp_err_t 
 */
static esp_err_t gdey0154d67_invert_color(esp_lcd_panel_t *self, bool invert_color_data);

/**
 * @brief Enter or exit sleep mode.
 * @note Entering the deep sleep mode is recommended
 *   when the screen is not used for a while.
 * @note In the deep sleep mode the screen does not react to SPI.
 *   In order to use it, call
 *   ```c   
 *          gdey0154d67_disp_sleep(disp, false);
 *   ```
 *   or 
 *   ```c   
 *          gdey0154d67_reset(disp);
 *          gdey0154d67_init(disp);
 *   ```
 * @note If the display vendor configuration
 *   parameter esp_lcd_gdey0154d67_vendor_config_t::retain_ram is false,
 *   the display RAM content becomes random after wake-up, requiring a full screen redraw.
 * 
 * @param[in] self Corresponding display handle.
 * @param[in] sleep True ~ Enter the sleep mode; False ~ wake up.
 * @return esp_err_t Error code.
 */
static esp_err_t gdey0154d67_disp_sleep(esp_lcd_panel_t *self, bool sleep);
