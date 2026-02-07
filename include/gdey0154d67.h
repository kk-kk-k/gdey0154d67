/**
 * @file gdey0154d67.h
 * @author Jakub Kral (jakub6kral@centrum.cz)
 * @brief Public GDEY0154D67 API declaration
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

#pragma once

#include "esp_lcd_types.h"          // esp_lcd_panel_handle_t
#include "esp_lcd_panel_vendor.h"   // esp_lcd_panel_dev_config_t
#include "esp_err.h"                // esp_err_t
#include "driver/gpio.h"            // gpio_num_t

/**
 * @brief GDEY0154D67 vendor-specific device configuration.
 * @note Enabling retain_ram increases power consumption a little (~ 3 uA).
 * @note If retain_ram is disabled, whole screen must be resent after waking up.
 *   Otherwise randomized garbage would appear on the non-updated part of the screen.
 * 
 */
typedef struct {
    gpio_num_t busy_pin;    // Busy GPIO pin
    bool retain_ram;        // Keep panel RAM content in the deep sleep mode.
} esp_lcd_gdey0154d67_vendor_config_t;

/**
 * @brief GDEY0154D67 update type.
 * 
 */
typedef enum {
    esp_lcd_gdey0154d67_full_update = 0xf7,     // Full update,     ~2.0 s
    esp_lcd_gdey0154d67_partial_update = 0xff,  // Partial update,  ~0.5 s
    esp_lcd_gdey0154d67_fast_update = 0xc7,     // Fast update,     ~1.5 s
} esp_lcd_gdey0154d67_update_mode_t;

/**
 * @brief Construct new panel handle.
 * @note ISR service is installed (If it is already installed, the invalid state error is omitted)
 * 
 * @param[in] io GDEY0154D67 IO handle used for low-level communication.
 * @param[in] panel_dev_config GDEY0154D67 device configuration.
 * @param[out] ret_panel New panel handle.
 * @return esp_err_t Error code.
 */
extern esp_err_t esp_lcd_new_panel_gdey0154d67(const esp_lcd_panel_io_handle_t io,
    const esp_lcd_panel_dev_config_t * const panel_dev_config, esp_lcd_panel_handle_t * const ret_panel);

/**
 * @brief Setup e-ink update type.
 *  The update type determines the update duration.
 *  The new update type is used when new image is sent to the display.
 * 
 * @param[in] self GDEY0154D67 device handle.
 * @param[in] update_mode Update type (full, fast, partial).
 * @return esp_err_t Error code.
 */
extern esp_err_t esp_lcd_gdey0154d67_set_update_mode(esp_lcd_panel_handle_t self, const esp_lcd_gdey0154d67_update_mode_t update_mode);

/**
 * @brief Clear whole GDEY0154D67 screen.
 *  Recommended when the e-ink is not used for longer period of time.
 *  This function does not change the e-ink update type.
 * 
 * @param[in] self GDEY0154D67 device handle.
 * @return esp_err_t Error code
 */
extern esp_err_t esp_lcd_gdey0154d67_whitescreen(esp_lcd_panel_handle_t self);
