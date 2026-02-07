# GDEY0154D67 e-Paper Driver for ESP-IDF

This is a clean, efficient ESP-IDF component for the **GoodDisplay GDEY0154D67** (1.54" 200x200 B/W E-ink) display. It is built directly on the **`esp_lcd`** panel framework.

## ⚠️ Important Implementation Notes

### Blocking Behavior

Unlike standard TFT/LCD drivers where `draw_bitmap` might return immediately after starting a DMA transfer, **`esp_lcd_panel_draw_bitmap()` in this driver is a blocking function.** 
The function will wait for the hardware **BUSY** signal to go low before returning.

-   Since e-Paper refresh cycles can take between **0.5s to 2.0s**, ensure this function is called properly (e.g., from its own task) task to avoid blocking your main application logic or watchdog timers.
    

### Unsupported esp_lcd Functions

Due to the hardware limitations of the GDEY0154D67 e-Paper controller, the following standard `esp_lcd` APIs are **not supported** and will return `ESP_ERR_NOT_SUPPORTED`:

-   `esp_lcd_panel_mirror()`
    
-   `esp_lcd_panel_swap_xy()`
    
-   `esp_lcd_panel_set_gap()`
    

> Note: Display rotation/inversion should be handled at the graphics library level (e.g., LVGL) or via the `esp_lcd_gdey0154d67_invert_color()` function for simple bit-inversion.

## Hardware Connection

| E-ink Pin | ESP32 Pin (Typical) | Description |
|-----------|---------------------|-------------|
| **BUSY**  | GPIO 4              | Busy signal (Active High) |
| **RES**   | GPIO 2              | Reset (Active Low) |
| **D/C**   | GPIO 5              | Data / Command Selection |
| **CS**    | GPIO 15             | SPI Chip Select |
| **SCLK**  | GPIO 18             | SPI Clock |
| **SDI**   | gPIO 23             | SPI MOSI |

## Quick Start

### 1. Initialize the Panel

```c
	// initialize SPI interface
	const spi_bus_config_t buscfg = {
		.sclk_io_num = ...,
		.mosi_io_num = ...,
		.miso_io_num = -1,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE
	};
	ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

	// create new LCD panel IO handle derived from SPI
	const esp_lcd_panel_io_spi_config_t io_config = {
		.dc_gpio_num = ...,
		.cs_gpio_num = ...,
		.pclk_hz = ...,
		.lcd_cmd_bits = 8,
		.lcd_param_bits = 8,
		.spi_mode = 0,
		.trans_queue_depth = 10,
		.on_color_trans_done = NULL
	};
	esp_lcd_panel_io_handle_t io_handle = NULL;
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) SPI2_HOST, &io_config, &io_handle));

	// create LCD panel handle
	esp_lcd_gdey0154d67_vendor_config_t esp_lcd_gdey0154d67_cfg = {
		.busy_pin = ...,
        .retain_ram = ...,
	};
	const esp_lcd_panel_dev_config_t panel_config = {
		.reset_gpio_num = ...,
		.flags.reset_active_high = false, // Not needed to set, reset is always active high
		.vendor_config = &esp_lcd_gdey0154d67_cfg,
	};
	esp_lcd_panel_handle_t panel_handle = NULL;
	ESP_ERROR_CHECK(esp_lcd_new_panel_gdey0154d67(io_handle, &panel_config, &panel_handle));
```

### 2. Update Modes

You can switch update modes to optimize for speed (Partial), balance (Fast) or quality (Full):

```c
// Change to Partial Update for fast refreshes (~0.5s)
esp_lcd_gdey0154d67_set_update_mode(panel_handle, esp_lcd_gdey0154d67_partial_update);

// This call will BLOCK until the refresh is complete
esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 200, 200, my_buffer);

```

> Note: For details, please check esp_lcd_gdey0154d67_update_mode_t located in gdey0154d67.h
> Note: Screen flickering is normal for e-ink displays.

## API Reference Extension

### `esp_lcd_gdey0154d67_whitescreen(panel_handle)`

A helper function to perform a clean white-out of the display RAM and physical screen. Highly recommended during initial setup or when switching between complex images to prevent ghosting.

### `esp_lcd_panel_disp_sleep(panel_handle, bool sleep)`

Puts the display into deep sleep.

-   If `retain_ram` was set to `true` in config, the display keeps its current image in internal buffers.
    
-   If `false`, you must resend the full frame buffer after waking up to avoid visual artifacts.
