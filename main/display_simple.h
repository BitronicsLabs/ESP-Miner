#ifndef DISPLAY_SIMPLE_H
#define DISPLAY_SIMPLE_H

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

// Simple framebuffer-based display for low memory mode (no LVGL)
// Supports monochrome OLED displays (SSD1306, SSD1309, SH1107)

typedef struct {
    esp_lcd_panel_handle_t panel_handle;
    uint16_t width;
    uint16_t height;
    uint8_t *framebuffer;  // 1 bit per pixel
    bool initialized;
} simple_display_t;

// Initialize simple display (no LVGL)
esp_err_t simple_display_init(simple_display_t *display, esp_lcd_panel_handle_t panel, uint16_t width, uint16_t height);

// Clear the framebuffer
void simple_display_clear(simple_display_t *display);

// Draw text at specified position (x, y in pixels)
void simple_display_draw_text(simple_display_t *display, const char *text, int x, int y);

// Update physical display with framebuffer contents
esp_err_t simple_display_update(simple_display_t *display);

// Free resources
void simple_display_deinit(simple_display_t *display);

// Set pixel (for direct drawing)
void simple_display_set_pixel(simple_display_t *display, int x, int y, bool on);

#endif // DISPLAY_SIMPLE_H
