#ifndef PLATFORM_3DS_H
#define PLATFORM_3DS_H

#ifdef __3DS__

#include <stdint.h>

#define N3DS_TOP_WIDTH 400
#define N3DS_TOP_HEIGHT 240
#define N3DS_BOTTOM_WIDTH 320
#define N3DS_BOTTOM_HEIGHT 240

#define N3DS_LOGICAL_WIDTH 640
#define N3DS_LOGICAL_HEIGHT 480
#define N3DS_PANORAMIC_WIDTH 962 // 800px City Viewport + 162px Sidebar for exact 2:1 (800x432 -> 400x216) Panoramic Zoom

typedef enum {
    N3DS_TOP_ZOOM_0_5X = 0, // Panoramic 0.5x: Exact 2:1 downscale (800x432 -> 400x216, 4x area, zero stripes)
    N3DS_TOP_ZOOM_1_0X = 1, // Standard 1.0x:  Exact 1:1 native pixels (400x216 -> 400x216, crystal clear)
    N3DS_TOP_ZOOM_2_0X = 2, // Close-Up 2.0x:  Exact 1:2 integer upscale (200x108 -> 400x216, 2x magnified)
    N3DS_TOP_ZOOM_MAX = 3
} n3ds_top_zoom;

#define PLATFORM_ENABLE_INIT_CALLBACK
void platform_init_callback(void);

#define PLATFORM_ENABLE_PER_FRAME_CALLBACK
void platform_per_frame_callback(void);

#define PLATFORM_USE_VIRTUAL_KEYBOARD
void platform_show_virtual_keyboard(void);
void platform_hide_virtual_keyboard(void);

#define PLATFORM_USE_SOFTWARE_CURSOR

void n3ds_early_init(void);
const char *n3ds_find_data_dir(const char *arg_dir);
void n3ds_show_missing_data_error(void);

void n3ds_render_dual_screens(const uint32_t *canvas, int canvas_w, int canvas_h);
void n3ds_wait_for_vblank(void);

#endif // __3DS__
#endif // PLATFORM_3DS_H
