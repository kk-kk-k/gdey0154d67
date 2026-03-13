// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file gdey0154d67.c
 * @author Jakub Kral (jakub6kral@centrum.cz)
 * @brief GDEY0154D67 implementation.
 * @details Driver functionality is described in gdey0154d67_private.h
 * @version 0.1
 * @date 2026-02-01
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

#include "esp_err.h"                // esp_err_t, ESP-IDF error codes
#include "esp_check.h"              // ESP_RETURN_ON_ERROR(), ESP_RETURN_ON_FALSE(), ESP_RETURN_VOID_ON_FALSE_ISR(), ESP_GOTO_ON_ERROR()
#include "esp_lcd_panel_io.h"       // esp_lcd_panel_io_tx_param(), esp_lcd_panel_io_tx_color()
#include "gdey0154d67_private.h"    // Internal API declaration
#include "gdey0154d67.h"            // Public API declaration

//------------------
// Public API implementation
//------------------

esp_err_t esp_lcd_new_panel_gdey0154d67(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t * const panel_dev_config,
    esp_lcd_panel_handle_t * const ret_panel) {
    ESP_RETURN_ON_FALSE(io != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: io cannot be NULL", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_FALSE(panel_dev_config != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: panel_dev_config cannot be NULL", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_FALSE(ret_panel != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: ret_panel cannot be NULL", __FILE__, __FUNCTION__, __LINE__);

    // ret needed for macro ESP_GOTO_ON_ERROR() - it sets `ret` and goes to the cleanup label
    esp_err_t ret = ESP_OK;

    // Extract vendor configuration
    const esp_lcd_gdey0154d67_vendor_config_t * const p_vendor_cfg = (esp_lcd_gdey0154d67_vendor_config_t *) panel_dev_config->vendor_config;
    ESP_RETURN_ON_FALSE(p_vendor_cfg != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: panel_dev_config->vendor_config cannot be NULL", __FILE__, __FUNCTION__, __LINE__);

    // Allocate new `gdey0154d67_display_t`.
    // Display functions (e.g., gdey0154d67_draw_bitmap) can obtain it later with gdey0154d67_extract_display.
    gdey0154d67_display_t * const p_eink_display = (gdey0154d67_display_t *) malloc(sizeof(gdey0154d67_display_t));
    ESP_RETURN_ON_FALSE(p_eink_display != NULL, ESP_ERR_NO_MEM, gdey0154d67_tag, "%s:%s:%d: Malloc returned NULL", __FILE__, __FUNCTION__, __LINE__);

    // Assign the base member appropiate function pointers
    // The esp_lcd component API (e.g. esp_lcd_panel_draw_bitmap) internally call these display-specific functions
    // It might be described as function overriding in OOP:
    // parent esp_lcd_panel_t has some virtual functions which are overriden by gdey0154d67_display_t.
    p_eink_display->base.del            = &gdey0154d67_del;
    p_eink_display->base.reset          = &gdey0154d67_reset;
    p_eink_display->base.init           = &gdey0154d67_init;
    p_eink_display->base.draw_bitmap    = &gdey0154d67_draw_bitmap;
    p_eink_display->base.invert_color   = &gdey0154d67_invert_color;
    p_eink_display->base.set_gap        = NULL; // Setting gap is not supported
    p_eink_display->base.mirror         = NULL; // Mirroring is not supported
    p_eink_display->base.swap_xy        = NULL; // Swapping axes is not supported
    p_eink_display->base.disp_on_off    = NULL; // Shutting off is not supported
    p_eink_display->base.disp_sleep     = &gdey0154d67_disp_sleep;

    // Copy the vendor config and fill the rest of the p_eink_display
    p_eink_display->vendor_cfg          = *p_vendor_cfg;
    p_eink_display->reset_pin           = panel_dev_config->reset_gpio_num;
    p_eink_display->io                  = io;

    // Create new semaphore for busy waiting
    p_eink_display->busy_semaphore      = xSemaphoreCreateBinary();

    // Install ISR service. Report errors. If 
    esp_err_t isr_service_errno = gpio_install_isr_service(0);
    ESP_GOTO_ON_FALSE(isr_service_errno == ESP_OK || isr_service_errno == ESP_ERR_INVALID_STATE, isr_service_errno, cleanup, gdey0154d67_tag, "%s:%s:%d: gpio_install_isr_service(0) failed with %d", __FILE__, __FUNCTION__, __LINE__, isr_service_errno);
        
    // Configure the busy pin as input with ISR triggered on negedge (signal change 1->0)
    const gpio_config_t busy_pin_cfg = {
        .pin_bit_mask = 1 << p_eink_display->vendor_cfg.busy_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&busy_pin_cfg), cleanup, gdey0154d67_tag, "%s:%s:%d: Cannot configure busy pin HW", __FILE__, __FUNCTION__, __LINE__);
    ESP_GOTO_ON_ERROR(gpio_isr_handler_add(p_vendor_cfg->busy_pin, &gdey0154d67_busy_isr, (void *) p_eink_display), cleanup, gdey0154d67_tag, "%s:%s:%d: Cannot configure busy pin HW", __FILE__, __FUNCTION__, __LINE__);

    // Configure the reset pin as output
    const gpio_config_t reset_pin_cfg = {
        .pin_bit_mask = 1 << p_eink_display->reset_pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&reset_pin_cfg), cleanup, gdey0154d67_tag, "%s:%s:%d: Cannot configure reset pin HW", __FILE__, __FUNCTION__, __LINE__);

    *ret_panel = (esp_lcd_panel_handle_t) &(p_eink_display->base);
    return ESP_OK;

// Cleanup label. The program goes here when ESP_GOTO_ON_ERROR() detects error
// Error code is stored in the ret variable (note: it must be called ret - the macro needs it)
cleanup:
    // Give GPIO pins to their default state
    gpio_reset_pin(panel_dev_config->reset_gpio_num);
    gpio_reset_pin(p_vendor_cfg->busy_pin);

    // Release allocated resources
    if (p_eink_display != NULL) {
        if (p_eink_display->busy_semaphore != NULL)
            vSemaphoreDelete(p_eink_display->busy_semaphore);

        free(p_eink_display);
    }

    // Prevent user from using the display
    *ret_panel = NULL;

    // Return error code
    return ret;
}

esp_err_t esp_lcd_gdey0154d67_set_update_mode(esp_lcd_panel_handle_t _self, const esp_lcd_gdey0154d67_update_mode_t update_mode) {
    ESP_RETURN_ON_FALSE(_self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg", __FILE__, __FUNCTION__, __LINE__);

    // Get the underlying display structure
    gdey0154d67_display_t * const self = gdey0154d67_extract_display(_self);
    ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg (couldn't extract gdey0154d67_display_t)", __FILE__, __FUNCTION__, __LINE__);

    self->update_mode = update_mode;

    return ESP_OK;
}

esp_err_t esp_lcd_gdey0154d67_whitescreen(esp_lcd_panel_handle_t _self) {
    ESP_RETURN_ON_FALSE(_self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg");

    // Get the underlying display structure
    const gdey0154d67_display_t * const self = gdey0154d67_extract_display(_self);
    ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg (couldn't extract gdey0154d67_display_t)");

    // Setup RAM range to whole display
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_RAM_X_RANGE_SETUP, (uint8_t[]) {0, GDEY0154D67_H_RES / 8}, 2), gdey0154d67_tag, "%s:%s:%d: Cannot send RAM x range setup command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_RAM_Y_RANGE_SETUP, (uint8_t[]) {0, 0, GDEY0154D67_V_RES, 0}, 4), gdey0154d67_tag, "%s:%s:%d: Cannot send RAM y range setup command", __FILE__, __FUNCTION__, __LINE__);

    // Setup RAM address to [0, 0]
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_SET_RAM_X_ADDR, (uint8_t[]) {0}, 1), gdey0154d67_tag, "%s:%s:%d: Cannot send x setup command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_SET_RAM_Y_ADDR, (uint8_t[]) {0, 0}, 2), gdey0154d67_tag, "%s:%s:%d: Cannot send y setup command", __FILE__, __FUNCTION__, __LINE__);

    // Send white color for whole screen.
    // Byte after byte is transferred. The performance overhead is only a little
    // compared to sending 5000 (GDEY0154D67_H_RES * GDEY0154D67_V_RES / 8 = 5000)
    // byte array of 0xff - the driver saves RAM rather than performance
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_WRITE_BW_RAM, NULL, 0), gdey0154d67_tag, "%s:%s:%d: Cannot send write BW VRAM command", __FILE__, __FUNCTION__, __LINE__);
    for (uint32_t i = 0; i < GDEY0154D67_H_RES * GDEY0154D67_V_RES / 8; i++)
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(self->io, -1, (uint8_t []) {0xff}, 1), gdey0154d67_tag, "%s:%s:%d: Cannot send data", __FILE__, __FUNCTION__, __LINE__);

    // Refresh the display
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_MASTER_ACTIVATION, NULL, 0), gdey0154d67_tag, "%s:%s:%d: Cannot send master activation command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_gdey0154d67_await_busy(_self), gdey0154d67_tag, "%s:%s:%d: Cannot wait for busy", __FILE__, __FUNCTION__, __LINE__);

    return ESP_OK;
}

//------------------
// Busy signal handlers
//------------------

static void gdey0154d67_busy_isr(void * _self) {
    const gdey0154d67_display_t * const self = (gdey0154d67_display_t *) _self;
    portBASE_TYPE higher_priority_task_woken = pdFALSE;

    // Give the semaphore
    ESP_RETURN_VOID_ON_FALSE_ISR(xSemaphoreGiveFromISR(self->busy_semaphore, &higher_priority_task_woken) == pdTRUE, gdey0154d67_tag, "%s:%s:%d: Cannot give busy semaphore from ISR", __FILE__, __FUNCTION__, __LINE__);

    // Force a content switch if needed
    if (higher_priority_task_woken != pdFALSE)
        portYIELD_FROM_ISR();
}

static esp_err_t esp_lcd_gdey0154d67_await_busy(esp_lcd_panel_handle_t _self) {
    ESP_RETURN_ON_FALSE(_self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid _self arg", __FILE__, __FUNCTION__, __LINE__);

    // Get the underlying display structure
    const gdey0154d67_display_t * const self = gdey0154d67_extract_display(_self);
    ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid _self arg (couldn't extract gdey0154d67_display_t)", __FILE__, __FUNCTION__, __LINE__);

    // Note:
    // - The driver first checks the busy pin level manually.
    //   This prevents the code from blocking indefinitely if an interrupt edge never occurs.
    // - The busy pin interrupt is enabled only during the wait.
    //   If the interrupt were enabled all the time and it triggered before `esp_lcd_gdey0154d67_await_busy()` was waiting,
    //   the semaphore would be missed. As a result, `esp_lcd_gdey0154d67_await_busy()` could return immediately
    //   without actually waiting for the display to become ready, causing incorrect behavior.


    // Check that the display is in busy state
    ESP_RETURN_ON_FALSE(gpio_get_level(self->vendor_cfg.busy_pin) == 1, ESP_ERR_INVALID_STATE, gdey0154d67_tag, "%s:%s:%d: Busy pin is in the LOW state", __FILE__, __FUNCTION__, __LINE__);

    // Enable the busy interrupt
    ESP_RETURN_ON_ERROR(gpio_intr_enable(self->vendor_cfg.busy_pin), gdey0154d67_tag, "%s:%s:%d: Couldn't enable INTR", __FILE__, __FUNCTION__, __LINE__);

    // Wait for the busy semaphore (the semaphore is given by `gdey0154d67_busy_isr()` when the display becomes ready)
    ESP_RETURN_ON_FALSE(xSemaphoreTake(self->busy_semaphore, portMAX_DELAY) == pdTRUE, ESP_ERR_TIMEOUT, gdey0154d67_tag, "%s:%s:%d: xSemaphoreTake timeout reached!", __FILE__, __FUNCTION__, __LINE__);

    // Disable the busy interrupt
    ESP_RETURN_ON_ERROR(gpio_intr_disable(self->vendor_cfg.busy_pin), gdey0154d67_tag, "%s:%s:%d: Couldn't disable INTR", __FILE__, __FUNCTION__, __LINE__);

    return ESP_OK;
}

//------------------
// esp_lcd_panel_t implementation
//------------------

static esp_err_t gdey0154d67_reset(esp_lcd_panel_t * _self) {
    ESP_RETURN_ON_FALSE(_self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg", __FILE__, __FUNCTION__, __LINE__);

    // Get the underlying display structure
    const gdey0154d67_display_t * const self = gdey0154d67_extract_display(_self);
    ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg (couldn't extract gdey0154d67_display_t)", __FILE__, __FUNCTION__, __LINE__);

    // Reset the display by writing the RES pin and wait until the display becomes ready
    ESP_RETURN_ON_ERROR(gpio_set_level(self->reset_pin, 0), gdey0154d67_tag, "%s:%s:%d: Cannot set reset pin low", __FILE__, __FUNCTION__, __LINE__);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(self->reset_pin, 1), gdey0154d67_tag, "%s:%s:%d: Cannot set reset pin high", __FILE__, __FUNCTION__, __LINE__);
	vTaskDelay(pdMS_TO_TICKS(10));

    // Send software reset command and wait until the display becomes ready
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_SW_RST, NULL, 0), gdey0154d67_tag, "%s:%s:%d: Cannot send software reset command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_gdey0154d67_await_busy(_self), gdey0154d67_tag, "%s:%s:%d: Cannot perform busy wait", __FILE__, __FUNCTION__, __LINE__);
	vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}


static esp_err_t gdey0154d67_init(esp_lcd_panel_t * _self) {
    ESP_RETURN_ON_FALSE(_self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg", __FILE__, __FUNCTION__, __LINE__);

    // Get the underlying display structure
    const gdey0154d67_display_t * const self = gdey0154d67_extract_display(_self);
    ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg (couldn't extract gdey0154d67_display_t)", __FILE__, __FUNCTION__, __LINE__);

    // Setup display properties - gate properties, RAM address increments, temperature sensor
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_DRIVER_OUTPUT_CTRL, (uint8_t[]) {199, 0, 0} /* 199 gate lines, G0 first, interlaced gate scanning, incremental scan */, 3), gdey0154d67_tag, "%s:%s:%d: Cannot send driver output control command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_DATA_ENTRY_MODE_SETUP, (uint8_t[]) {0b011} /* X increment, Y increment */, 1), gdey0154d67_tag, "%s:%s:%d: Cannot send data entry mode command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_TEMP_SENS_CTRL, (uint8_t[]) {0x80} /* Internal temperature sensor */, 1), gdey0154d67_tag, "%s:%s:%d: Cannot send temperature sensor setup command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_gdey0154d67_set_update_mode(_self, esp_lcd_gdey0154d67_full_update), gdey0154d67_tag, "%s:%s:%d: Cannot set full update mode", __FILE__, __FUNCTION__, __LINE__);

    return ESP_OK;
}

static esp_err_t gdey0154d67_del(esp_lcd_panel_t * _self) {
    ESP_RETURN_ON_FALSE(_self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg", __FILE__, __FUNCTION__, __LINE__);

    // Get the underlying display structure
    gdey0154d67_display_t * const self = gdey0154d67_extract_display(_self);
    ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg (couldn't extract gdey0154d67_display_t)", __FILE__, __FUNCTION__, __LINE__);

    // Reset behavior of GPIO pins to their default states
    ESP_RETURN_ON_ERROR(gpio_reset_pin(self->reset_pin), gdey0154d67_tag, "%s:%s:%d: Cannot reset 'reset pin'", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(gpio_reset_pin(self->vendor_cfg.busy_pin), gdey0154d67_tag, "%s:%s:%d: Cannot reset 'busy pin'", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(gpio_isr_handler_remove(self->vendor_cfg.busy_pin), gdey0154d67_tag, "%s:%s:%d: Cannot reset 'busy pin' ISR handler", __FILE__, __FUNCTION__, __LINE__);

    // Free resources
    vSemaphoreDelete(self->busy_semaphore);
    self->busy_semaphore = NULL;
    free(self);

    return ESP_OK;
}

static esp_err_t gdey0154d67_draw_bitmap(esp_lcd_panel_t *_self, int x_start, int y_start, int x_end, int y_end, const void *color_data) {
    ESP_RETURN_ON_FALSE(_self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_FALSE(x_start >= 0, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: x_start must be >= 0", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_FALSE(y_start >= 0, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: y_start must be >= 0", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_FALSE(x_end <= GDEY0154D67_H_RES, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: x_end must be <= %d", __FILE__, __FUNCTION__, __LINE__, GDEY0154D67_H_RES);
    ESP_RETURN_ON_FALSE(y_end <= GDEY0154D67_V_RES, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: y_end must be <= %d", __FILE__, __FUNCTION__, __LINE__, GDEY0154D67_V_RES);
    ESP_RETURN_ON_FALSE(x_end > x_start, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: x_end must be > x_start", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_FALSE(y_end > y_start, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: y_end must be > y_start", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_FALSE((x_start & 0b111) == 0, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: x_start must be dividable by 8", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_FALSE((x_end & 0b111) == 0, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: x_end must be dividable by 8", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_FALSE(color_data != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid color_data arg", __FILE__, __FUNCTION__, __LINE__);

    // Get the underlying display structure
    const gdey0154d67_display_t * const self = gdey0154d67_extract_display(_self);
    ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg (couldn't extract gdey0154d67_display_t)", __FILE__, __FUNCTION__, __LINE__);

    // X dimensions should be aligned to bytes (input is bit-aligned)
    x_start /= 8;
    x_end /= 8;

    // Compute the size of the color_data
    const uint32_t size = (y_end - y_start) * (x_end - x_start);

    // HW reset is needed for partial update
    if (self->update_mode == esp_lcd_gdey0154d67_partial_update) {
        ESP_RETURN_ON_ERROR(gpio_set_level(self->reset_pin, 0), gdey0154d67_tag, "%s:%s:%d: Cannot set reset pin low", __FILE__, __FUNCTION__, __LINE__);
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(self->reset_pin, 1), gdey0154d67_tag, "%s:%s:%d: Cannot set reset pin high", __FILE__, __FUNCTION__, __LINE__);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Setup RAM address range. This allows drawing pictures over part of the screen without any extra computing.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_RAM_X_RANGE_SETUP, (uint8_t[]) {x_start, x_end - 1}, 2), gdey0154d67_tag, "%s:%s:%d: Cannot send RAM x range setup command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_RAM_Y_RANGE_SETUP, (uint8_t[]) {y_start, 0, y_end - 1, 0}, 4), gdey0154d67_tag, "%s:%s:%d: Cannot send RAM y range setup command", __FILE__, __FUNCTION__, __LINE__);

    // Setup RAM address
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_SET_RAM_X_ADDR, (uint8_t[]) {x_start}, 1), gdey0154d67_tag, "%s:%s:%d: Cannot send x setup command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_SET_RAM_Y_ADDR, (uint8_t[]) {y_start, 0}, 2), gdey0154d67_tag, "%s:%s:%d: Cannot send y setup command", __FILE__, __FUNCTION__, __LINE__);

    // Send the picture
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(self->io, GDEY0154D67_CMD_WRITE_BW_RAM, color_data, size), gdey0154d67_tag, "%s:%s:%d: Cannot send BW RAM content", __FILE__, __FUNCTION__, __LINE__);

    const uint8_t border_param = self->update_mode == esp_lcd_gdey0154d67_partial_update ? 0b11000000 : 0x05;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_BORDER_CTRL, &border_param, 1), gdey0154d67_tag, "%s:%s:%d: Cannot send border control command", __FILE__, __FUNCTION__, __LINE__);

    // When full or fast update is required, setup red RAM content as well.
    // This sets basemap for partial update.
    if (self->update_mode != esp_lcd_gdey0154d67_partial_update) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(self->io, GDEY0154D67_CMD_WRITE_RED_RAM, color_data, size), gdey0154d67_tag, "%s:%s:%d: Cannot send write BW RAM command", __FILE__, __FUNCTION__, __LINE__);
    }

    // Fast update requires manual temperature setup.
    // The corresponding LUT is loaded
    if (self->update_mode == esp_lcd_gdey0154d67_fast_update) {
        // Set manual temperature
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_TEMP_REG_WRITE, (uint8_t[]) {0x64, 0x00}, 2), gdey0154d67_tag, "%s:%s:%d: Cannot send write temp register", __FILE__, __FUNCTION__, __LINE__);
        
        // Load LUT
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_DISP_UPDATE_CTRL_2, (uint8_t[]) {0x91}, 1), gdey0154d67_tag, "%s:%s:%d: Cannot send display update control command", __FILE__, __FUNCTION__, __LINE__);
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_MASTER_ACTIVATION, NULL, 0), gdey0154d67_tag, "%s:%s:%d: Cannot send master activation command", __FILE__, __FUNCTION__, __LINE__);
        ESP_RETURN_ON_ERROR(esp_lcd_gdey0154d67_await_busy(_self), gdey0154d67_tag, "%s:%s:%d: Cannot await busy", __FILE__, __FUNCTION__, __LINE__);
    }
    
    // Set update mode in the display.
    // All possible values are hard-coded into the enum esp_lcd_gdey0154d67_update_mode_t.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_DISP_UPDATE_CTRL_2, &self->update_mode, 1), gdey0154d67_tag, "%s:%s:%d: Cannot send update control 2 command", __FILE__, __FUNCTION__, __LINE__);

    // Trigger screen update and wait until it finishes
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_MASTER_ACTIVATION, NULL, 0), gdey0154d67_tag, "%s:%s:%d: Cannot send master activation command", __FILE__, __FUNCTION__, __LINE__);
    ESP_RETURN_ON_ERROR(esp_lcd_gdey0154d67_await_busy(_self), gdey0154d67_tag, "Cannot wait for busy");

    return ESP_OK;
}

static esp_err_t gdey0154d67_invert_color(esp_lcd_panel_t *_self, bool invert_color_data) {
    ESP_RETURN_ON_FALSE(_self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg", __FILE__, __FUNCTION__, __LINE__);

    // Get the underlying display structure
    const gdey0154d67_display_t * const self = gdey0154d67_extract_display(_self);
    ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg (couldn't extract gdey0154d67_display_t)", __FILE__, __FUNCTION__, __LINE__);

    // Send color inversion command
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_DISP_UPDATE_CTRL_1, (uint8_t[]) {(invert_color_data == true) ? 0b10001000 : 0}, 1), gdey0154d67_tag, "%s:%s:%d: Cannot send update control 1 command", __FILE__, __FUNCTION__, __LINE__);

    return ESP_OK;
}

static esp_err_t gdey0154d67_disp_sleep(esp_lcd_panel_t *_self, bool sleep) {
    ESP_RETURN_ON_FALSE(_self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg", __FILE__, __FUNCTION__, __LINE__);

    // Get the underlying display structure
    const gdey0154d67_display_t * const self = gdey0154d67_extract_display(_self);
    ESP_RETURN_ON_FALSE(self != NULL, ESP_ERR_INVALID_ARG, gdey0154d67_tag, "%s:%s:%d: Invalid self arg (couldn't extract gdey0154d67_display_t)", __FILE__, __FUNCTION__, __LINE__);

    // Enter/leave the deep seep mode
    switch (sleep) {
        case true:
            // Entering the sleep mode is done by sending appropiate command
            ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(self->io, GDEY0154D67_CMD_DEEP_SLEEP, (uint8_t[]) {self->vendor_cfg.retain_ram ? 0x10 : 0x11}, 1), gdey0154d67_tag, "%s:%s:%d Cannot send deep sleep command", __FILE__, __FUNCTION__, __LINE__);
            break;
        case false:
            // Exiting the sleep mode is possible only by reinitializing the display
            ESP_RETURN_ON_ERROR(gdey0154d67_reset(_self), gdey0154d67_tag, "%s:%s:%d: Cannot reset display", __FILE__, __FUNCTION__, __LINE__);
            ESP_RETURN_ON_ERROR(gdey0154d67_init(_self), gdey0154d67_tag, "%s:%s:%d: Cannot init display", __FILE__, __FUNCTION__, __LINE__);
            break;
    }

    return ESP_OK;
}
