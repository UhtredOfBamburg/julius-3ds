#include "platform/3ds/3ds.h"

#ifdef __3DS__

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "SDL.h"

#include "building/construction.h"
#include "building/menu.h"
#include "city/view.h"
#include "core/calc.h"
#include "core/config.h"
#include "core/encoding.h"
#include "core/string.h"
#include "empire/empire.h"
#include "game/orientation.h"
#include "game/state.h"
#include "game/system.h"
#include "graphics/screen.h"
#include "graphics/window.h"
#include "input/cursor.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "input/touch.h"
#include "platform/cursor.h"
#include "window/advisors.h"
#include "window/build_menu.h"
#include "window/city.h"
#include "window/overlay_menu.h"

// Give Julius 512 KB of stack on 3DS (leave __ctru_heap_size/linear_heap_size default so heap stays < 96 MB OS_HEAP_AREA_END)
u32 __stacksize__ = 512 * 1024;

#define MAX_VKBD_TEXT_SIZE 600

static struct {
    uint8_t text[MAX_VKBD_TEXT_SIZE];
    char utf8_text[MAX_VKBD_TEXT_SIZE];
    int requested;
} vkbd;

typedef enum {
    TOUCH_ACTION_NONE = 0,
    TOUCH_ACTION_LEFT_CLICK = 1,
    TOUCH_ACTION_RIGHT_CLICK = 2,
    TOUCH_ACTION_CITY_EXPLORE = 3,
    TOUCH_ACTION_CITY_BUILD = 4,
    TOUCH_ACTION_EMPIRE_MAP = 5,
    TOUCH_ACTION_CUSTOM_BUTTON = 6
} n3ds_touch_action;

static struct {
    n3ds_top_zoom top_zoom; // Default: N3DS_TOP_ZOOM_1_0X (Crystal-clear 1:1 native)
    int bot_zoom;           // 0 = 1.0X Native 1:1 (142x200), 1 = 0.5X Wide 2:1 (284x400 -> 142x200)
    int left_map_mode;      // 0 = City Construction Viewport, 1 = Minimap + Overlays
    int advisor_zoom_1to1;
    int cam_center_x;
    int cam_center_y;

    int touch_was_down;
    n3ds_touch_action touch_action;
    int start_bx;
    int start_by;
    int last_bx;
    int last_by;
    int is_dragging;
    int scroll_accum_x;
    int scroll_accum_y;
    int drag_accum_x;
    int drag_accum_y;
} n3ds_state = {
    N3DS_TOP_ZOOM_1_0X,
    0,
    0,
    0,
    N3DS_LOGICAL_WIDTH / 2,
    N3DS_LOGICAL_HEIGHT / 2,
    0,
    TOUCH_ACTION_NONE,
    0, 0, 0, 0, 0, 0, 0, 0, 0
};

void n3ds_early_init(void)
{
    // Unlock New 3DS 804 MHz CPU clock and 2MB L2 cache
    osSetSpeedupEnable(true);
}

static int has_c3_files_in_dir(const char *dir)
{
    if (!dir || !*dir) {
        return 0;
    }
    char path[512];
    struct stat st;
    const char *candidates[] = {
        "c3.eng", "C3.ENG", "c3.sg2", "C3.SG2", "C3_mm.555", "c3_mm.555"
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, candidates[i]);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            return 1;
        }
    }
    return 0;
}

const char *n3ds_find_data_dir(const char *arg_dir)
{
    if (arg_dir && has_c3_files_in_dir(arg_dir)) {
        return arg_dir;
    }
    static const char *search_paths[] = {
        ".",
        "sdmc:/3ds/julius",
        "sdmc:/3ds/caesar3",
        "sdmc:/caesar3",
        "sdmc:/Caesar3",
        "sdmc:/3ds/Julius"
    };
    for (size_t i = 0; i < sizeof(search_paths) / sizeof(search_paths[0]); i++) {
        if (has_c3_files_in_dir(search_paths[i])) {
            return search_paths[i];
        }
    }
    return arg_dir ? arg_dir : "sdmc:/3ds/julius";
}

void n3ds_show_missing_data_error(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("\x1b[2J");
    printf("\x1b[2;2H==============================================");
    printf("\x1b[3;2H        JULIUS (CAESAR III) - NEW 3DS         ");
    printf("\x1b[4;2H==============================================");
    printf("\x1b[6;2HArquivos originais do Caesar III nao encontrados!");
    printf("\x1b[8;2HCopie os arquivos da sua instalacao do Caesar 3");
    printf("\x1b[9;2H(c3.eng, c3.sg2, c3.555, pastas wavs/, etc.)");
    printf("\x1b[10;2Hpara uma destas pastas no cartao SD do 3DS:");
    printf("\x1b[12;4H* sdmc:/3ds/julius/");
    printf("\x1b[13;4H* sdmc:/caesar3/");
    printf("\x1b[16;2HPressione START para sair.");

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
}

static void center_mouse_cursor(void)
{
    int x = screen_width() / 2;
    int y = screen_height() / 2;
    system_set_mouse_position(&x, &y);
    mouse_set_position(x, y);
    n3ds_state.cam_center_x = x;
    n3ds_state.cam_center_y = y;
}

static const uint8_t *n3ds_keyboard_get(const uint8_t *text, int max_length)
{
    max_length = calc_bound(max_length, 0, MAX_VKBD_TEXT_SIZE - 1);
    string_copy(text, vkbd.text, max_length);
    encoding_to_utf8(text, vkbd.utf8_text, max_length, 0);

    SwkbdState swkbd;
    char input_buf[MAX_VKBD_TEXT_SIZE];
    memset(input_buf, 0, sizeof(input_buf));

    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, max_length > 0 ? max_length : 32);
    swkbdSetInitialText(&swkbd, vkbd.utf8_text);
    swkbdSetHintText(&swkbd, "Digite o texto");
    SwkbdButton button = swkbdInputText(&swkbd, input_buf, sizeof(input_buf));
    if (button == SWKBD_BUTTON_CONFIRM) {
        encoding_from_utf8(input_buf, vkbd.text, max_length);
    }
    return vkbd.text;
}

static void n3ds_start_text_input(void)
{
    if (!keyboard_is_capturing()) {
        return;
    }
    const uint8_t *text = n3ds_keyboard_get(keyboard_get_text(), keyboard_get_max_text_length());
    keyboard_set_text(text);
}

void platform_init_callback(void)
{
    config_set(CONFIG_SCREEN_CURSOR_SCALE, 100);
    config_set(CONFIG_UI_SMOOTH_SCROLLING, 1);
    config_set(CONFIG_UI_DISABLE_MOUSE_EDGE_SCROLLING, 1);
    center_mouse_cursor();
}

void platform_show_virtual_keyboard(void)
{
    vkbd.requested = 1;
}

void platform_hide_virtual_keyboard(void)
{
}

// ============================================================================
// CONTEXT & GEOMETRY HELPERS
// ============================================================================

static int is_city_context(void)
{
    return window_is(WINDOW_CITY) ||
           window_is(WINDOW_CITY_MILITARY) ||
           window_is(WINDOW_BUILD_MENU) ||
           window_is(WINDOW_OVERLAY_MENU) ||
           window_is(WINDOW_TOP_MENU);
}

// Returns 1 if a build category is currently active, and outputs its (row 0..3, col 0..2) on the Right Build Panel
static int get_active_build_button_grid(int *out_row, int *out_col)
{
    building_type type = building_construction_type();
    if (type == BUILDING_NONE) {
        return 0;
    }
    build_menu_group group = building_menu_for_type(type);
    int idx = -1;
    switch (group) {
        case BUILD_MENU_VACANT_HOUSE: idx = 0; break;
        case BUILD_MENU_CLEAR_LAND:   idx = 1; break;
        case BUILD_MENU_ROAD:         idx = 2; break;
        case BUILD_MENU_WATER:        idx = 3; break;
        case BUILD_MENU_HEALTH:       idx = 4; break;
        case BUILD_MENU_TEMPLES:
        case BUILD_MENU_SMALL_TEMPLES:
        case BUILD_MENU_LARGE_TEMPLES: idx = 5; break;
        case BUILD_MENU_EDUCATION:     idx = 6; break;
        case BUILD_MENU_ENTERTAINMENT: idx = 7; break;
        case BUILD_MENU_ADMINISTRATION: idx = 8; break;
        case BUILD_MENU_ENGINEERING:   idx = 9; break;
        case BUILD_MENU_SECURITY:
        case BUILD_MENU_FORTS:         idx = 10; break;
        case BUILD_MENU_INDUSTRY:
        case BUILD_MENU_FARMS:
        case BUILD_MENU_RAW_MATERIALS:
        case BUILD_MENU_WORKSHOPS:     idx = 11; break;
        default: return 0;
    }
    if (out_row) *out_row = idx / 3;
    if (out_col) *out_col = idx % 3;
    return 1;
}

static void get_bottom_city_viewport_src(int canvas_w, int *x_src, int *y_src, int *w_src, int *h_src)
{
    int sb_x = canvas_w - 162;
    int vp_cx = sb_x / 2;
    int vp_cy = 252;

    if (n3ds_state.bot_zoom == 0) {
        // 1.0X Native Pixel-Perfect 1:1 (142x200 -> 142x200)
        *w_src = 142;
        *h_src = 200;
    } else {
        // 0.5X Wide Construction Area with Exact 2:1 Integer Downscale (284x400 -> 142x200)
        *w_src = 284;
        *h_src = 400;
    }
    *x_src = vp_cx - (*w_src / 2);
    *y_src = vp_cy - (*h_src / 2);
}

static void get_build_submenu_crop(int canvas_w, int *x_src, int *y_src, int *w_src, int *h_src)
{
    int sb_x = canvas_w - 162;
    int bx = sb_x - 266, by = 240, bw = 256, bh = 200;
    window_build_menu_get_bounds(&bx, &by, &bw, &bh);
    int crop_h = bh + 12;
    if (crop_h < 180) {
        crop_h = 180;
    }
    int cy = by + bh / 2;
    int sy = calc_bound(cy - crop_h / 2, 24, 480 - crop_h);
    *x_src = calc_bound(bx - 4, 0, sb_x - (bw + 8));
    *y_src = sy;
    *w_src = bw + 8;
    *h_src = crop_h;
}

static void get_overlay_submenu_crop(int *x_src, int *y_src, int *w_src, int *h_src)
{
    window_overlay_menu_get_bounds(x_src, y_src, w_src, h_src);
}

// Map a touch on the Right Build Panel (bx in 158..319, by in 0..239) to canvas coordinates
static int map_right_panel_touch(int canvas_w, int bx, int by, int *out_cx, int *out_cy, int *out_grid_r, int *out_grid_c)
{
    if (out_grid_r) *out_grid_r = -1;
    if (out_grid_c) *out_grid_c = -1;

    int sb_x = canvas_w - 162;
    int sx = sb_x + calc_bound(bx - 158, 0, 161);
    int sy = 24;

    if (by >= 3 && by < 26) {
        // Advisors & Empire row
        sy = 179 + (by - 3);
    } else if (by >= 28 && by < 50) {
        // Briefing & 3x Rotate row
        sy = 208 + (by - 28);
    } else if (by >= 71 && by < 221) {
        // 5 rows of 3 buttons (30px stride per row)
        int r = (by - 71) / 30;
        int dy = (by - 71) % 30;
        if (dy > 25) {
            dy = 25;
        }
        sy = 301 + r * 36 + dy;

        int rel_x = bx - 158;
        int c = -1;
        if (rel_x >= 11 && rel_x <= 54) c = 0;
        else if (rel_x >= 61 && rel_x <= 104) c = 1;
        else if (rel_x >= 111 && rel_x <= 154) c = 2;

        if (out_grid_r) *out_grid_r = r;
        if (out_grid_c) *out_grid_c = c;
    } else {
        return 0;
    }

    *out_cx = sx;
    *out_cy = sy;
    return 1;
}

// Toggle the In-Game Menu (WINDOW_TOP_MENU: New, Load, Save, Options, Exit)
static void toggle_ingame_menu(void)
{
    if (window_is(WINDOW_TOP_MENU)) {
        mouse_set_right_down(1);
        mouse_set_right_down(0);
    } else {
        if (window_is(WINDOW_BUILD_MENU) || window_is(WINDOW_OVERLAY_MENU)) {
            window_city_show();
        }
        if (building_construction_type() != BUILDING_NONE) {
            building_construction_cancel();
            building_construction_clear_type();
        }
        n3ds_state.left_map_mode = 0;
        // Click "File" at (20, 10) on the canvas to open WINDOW_TOP_MENU
        mouse_set_position(20, 10);
        mouse_set_left_down(1);
        mouse_set_left_down(0);
    }
}

// Toggle the Minimap + Overlays view in the Bottom Screen's Left Pane
static void toggle_left_map_mode(void)
{
    if (window_is(WINDOW_BUILD_MENU) || window_is(WINDOW_OVERLAY_MENU) || window_is(WINDOW_TOP_MENU)) {
        window_city_show();
    }
    if (building_construction_type() != BUILDING_NONE) {
        building_construction_cancel();
        building_construction_clear_type();
    }
    n3ds_state.left_map_mode = !n3ds_state.left_map_mode;
    window_request_refresh();
}

// ============================================================================
// PER-FRAME INPUT, ZOOM-DEPENDENT FLUID SCROLLING & D-PAD CONTROLLER
// ============================================================================

static int get_top_zoom_speed_percent(void)
{
    if (n3ds_state.top_zoom == N3DS_TOP_ZOOM_0_5X) {
        return 220; // Fast sweeping speed for 800x432 Panoramic view
    } else if (n3ds_state.top_zoom == N3DS_TOP_ZOOM_2_0X) {
        return 55;  // Slow surgical speed for 200x108 Close-Up view
    }
    return 115;     // Balanced natural speed for 400x216 1:1 view
}

// Smooth quadratic curve for Circle Pad: gentle tilt = 1-2px precision, full tilt = swift glide
static int calc_quadratic_stick_vel(int val, int deadzone)
{
    int a = abs(val);
    if (a <= deadzone) {
        return 0;
    }
    int eff = a - deadzone; // 0..138
    int sq = (eff * eff) / 95;
    int lin = eff / 4;
    int mag = lin + sq;
    return (val > 0) ? mag : -mag;
}

static void handle_camera_scroll_and_dpad(u32 kDown, u32 kHeld)
{
    if (!is_city_context() && !window_is(WINDOW_EMPIRE)) {
        // In standard menus/dialogs, let D-Pad nudge the cursor with slow 2px precision
        int mdx = 0, mdy = 0;
        if (kHeld & KEY_DRIGHT) mdx += 2;
        if (kHeld & KEY_DLEFT)  mdx -= 2;
        if (kHeld & KEY_DDOWN)  mdy += 2;
        if (kHeld & KEY_DUP)    mdy -= 2;
        if (mdx != 0 || mdy != 0) {
            const mouse *m = mouse_get();
            int nx = calc_bound(m->x + mdx, 0, screen_width() - 1);
            int ny = calc_bound(m->y + mdy, 0, screen_height() - 1);
            system_set_mouse_position(&nx, &ny);
            mouse_set_position(nx, ny);
            n3ds_state.cam_center_x = nx;
            n3ds_state.cam_center_y = ny;
        }
        return;
    }

    // D-Pad Shortcuts in City Gameplay (when NOT holding L):
    // - D-Pad DOWN:  Open / Close In-Game MENU (Save, Load, Options, Exit)
    // - D-Pad UP:    Toggle MAPA / OVERLAYS <-> CIDADE on Bottom Screen
    // - D-Pad LEFT:  Rotate City Camera Counter-Clockwise
    // - D-Pad RIGHT: Rotate City Camera Clockwise
    // Holding L + D-Pad switches D-Pad to Ultra-Slow Precision Camera Micro-Scroll!
    if (is_city_context() && !(kHeld & KEY_L)) {
        if (kDown & KEY_DDOWN) {
            toggle_ingame_menu();
        }
        if (kDown & KEY_DUP) {
            toggle_left_map_mode();
        }
        if ((kDown & KEY_DLEFT) && (window_is(WINDOW_CITY) || window_is(WINDOW_CITY_MILITARY))) {
            game_orientation_rotate_left();
            window_invalidate();
        }
        if ((kDown & KEY_DRIGHT) && (window_is(WINDOW_CITY) || window_is(WINDOW_CITY_MILITARY))) {
            game_orientation_rotate_right();
            window_invalidate();
        }
    }

    circlePosition cpad;
    hidCircleRead(&cpad);
    circlePosition cstick;
    irrstCstickRead(&cstick);

    int zoom_pct = is_city_context() ? get_top_zoom_speed_percent() : 100;

    // 1. Circle Pad (Main Analog): Smooth quadratic curve scaled by Top Screen Zoom
    int vx = (calc_quadratic_stick_vel(cpad.dx, 18) * zoom_pct) / 100;
    int vy = (calc_quadratic_stick_vel(-cpad.dy, 18) * zoom_pct) / 100;

    // 2. C-Stick (New 3DS Right Nub): Dedicated slow precision camera control
    if (abs(cstick.dx) > 18) {
        vx += (cstick.dx - (cstick.dx > 0 ? 18 : -18)) / 2;
    }
    if (abs(cstick.dy) > 18) {
        vy -= (cstick.dy - (cstick.dy > 0 ? 18 : -18)) / 2;
    }

    // 3. L + D-Pad (or D-Pad in Empire Map): Slow, high-precision 2px/frame micro-scroll
    if ((kHeld & KEY_L) || window_is(WINDOW_EMPIRE)) {
        int prec_speed = 36; // 36 / 18 = exact 2px per frame
        if (kHeld & KEY_DRIGHT) vx += prec_speed;
        if (kHeld & KEY_DLEFT)  vx -= prec_speed;
        if (kHeld & KEY_DDOWN)  vy += prec_speed;
        if (kHeld & KEY_DUP)    vy -= prec_speed;
    }

    if (vx == 0 && vy == 0) {
        n3ds_state.scroll_accum_x = 0;
        n3ds_state.scroll_accum_y = 0;
        return;
    }

    n3ds_state.scroll_accum_x += vx;
    n3ds_state.scroll_accum_y += vy;

    int step_x = n3ds_state.scroll_accum_x / 18;
    int step_y = n3ds_state.scroll_accum_y / 18;
    n3ds_state.scroll_accum_x %= 18;
    n3ds_state.scroll_accum_y %= 18;

    if (step_x != 0 || step_y != 0) {
        if (is_city_context()) {
            // Do NOT call window_invalidate() here! window_city's draw_foreground() already
            // redraws the city and minimap camera box every frame at 60 FPS without rebuilding the 53k-tile minimap!
            city_view_scroll(step_x, step_y);
        } else if (window_is(WINDOW_EMPIRE)) {
            empire_scroll_map(step_x, step_y);
            n3ds_state.cam_center_x = calc_bound(n3ds_state.cam_center_x + step_x, 16, 623);
            n3ds_state.cam_center_y = calc_bound(n3ds_state.cam_center_y + step_y, 16, 343);
        }
    }
}

void platform_per_frame_callback(void)
{
    if (vkbd.requested) {
        n3ds_start_text_input();
        vkbd.requested = 0;
    }

    int cw = screen_width();
    int ch = screen_height();
    if (cw <= 0 || ch <= 0) {
        return;
    }

    // Keep internal sidebar expanded in City context so all 15 build buttons & widgets exist on canvas
    if (is_city_context() && city_view_is_sidebar_collapsed()) {
        city_view_toggle_sidebar();
        window_invalidate();
    }

    // Automatically exit Minimap view if user starts building or opens a build submenu
    if (n3ds_state.left_map_mode &&
        (building_construction_type() != BUILDING_NONE || window_is(WINDOW_BUILD_MENU))) {
        n3ds_state.left_map_mode = 0;
    }

    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();

    // ZL / ZR cycles Top Screen Zoom: 0.5X (Panoramico) <-> 1.0X (Nativo 1:1) <-> 2.0X (Super Zoom)
    if (kDown & KEY_ZR) {
        if (window_is(WINDOW_ADVISORS)) {
            n3ds_state.advisor_zoom_1to1 = !n3ds_state.advisor_zoom_1to1;
        } else {
            n3ds_state.top_zoom = (n3ds_top_zoom)((n3ds_state.top_zoom + 1) % N3DS_TOP_ZOOM_MAX);
        }
    }
    if (kDown & KEY_ZL) {
        if (window_is(WINDOW_ADVISORS)) {
            n3ds_state.advisor_zoom_1to1 = !n3ds_state.advisor_zoom_1to1;
        } else {
            n3ds_state.top_zoom = (n3ds_top_zoom)((n3ds_state.top_zoom + N3DS_TOP_ZOOM_MAX - 1) % N3DS_TOP_ZOOM_MAX);
        }
    }

    // Dynamic Canvas Resolution Switching for 0.5X Panoramic Mode (962x480 -> 800x432 City Viewport, Exact 2:1 Downscale!)
    int want_panoramic = is_city_context() &&
                         !window_is(WINDOW_TOP_MENU) &&
                         (n3ds_state.top_zoom == N3DS_TOP_ZOOM_0_5X);
    if (want_panoramic && cw != N3DS_PANORAMIC_WIDTH) {
        screen_set_resolution(N3DS_PANORAMIC_WIDTH, N3DS_LOGICAL_HEIGHT);
        // Shift camera left by 160px so the exact same center tile stays locked in the middle of the screen
        city_view_scroll(-160, 0);
        cw = screen_width();
        ch = screen_height();
    } else if (!want_panoramic && cw != N3DS_LOGICAL_WIDTH) {
        city_view_scroll(160, 0);
        screen_set_resolution(N3DS_LOGICAL_WIDTH, N3DS_LOGICAL_HEIGHT);
        cw = screen_width();
        ch = screen_height();
    }

    int sb_x = cw - 162;

    // SELECT toggles Bottom Screen Zoom (1.0X Native 1:1 <-> 0.5X Wide 2:1 in City, or 1:1 Magnifier in Advisors)
    if (kDown & KEY_SELECT) {
        if (window_is(WINDOW_ADVISORS)) {
            n3ds_state.advisor_zoom_1to1 = !n3ds_state.advisor_zoom_1to1;
        } else {
            n3ds_state.bot_zoom = !n3ds_state.bot_zoom;
        }
    }

    // START pauses/unpauses when in City Gameplay and not typing
    if ((kDown & KEY_START) && !keyboard_is_capturing() &&
        (window_is(WINDOW_CITY) || window_is(WINDOW_CITY_MILITARY))) {
        game_state_toggle_paused();
        window_invalidate();
    }

    // L / R cycles through the 12 Advisors when in WINDOW_ADVISORS
    if (window_is(WINDOW_ADVISORS)) {
        if (kDown & KEY_L) {
            int adv = window_advisors_get_advisor();
            adv = (adv <= 1) ? 12 : (adv - 1);
            window_advisors_show_advisor(adv);
        } else if (kDown & KEY_R) {
            int adv = window_advisors_get_advisor();
            adv = (adv >= 12 || adv < 1) ? 1 : (adv + 1);
            window_advisors_show_advisor(adv);
        }
    }

    // B cancels active construction immediately (even mid-drag!)
    if ((kDown & KEY_B) && is_city_context() && building_construction_type() != BUILDING_NONE) {
        building_construction_cancel();
        building_construction_clear_type();
        if (n3ds_state.touch_action == TOUCH_ACTION_CITY_BUILD) {
            mouse_set_left_down(0);
            n3ds_state.touch_action = TOUCH_ACTION_NONE;
        }
        window_request_refresh();
    }

    // Smooth camera scrolling via Circle Pad / C-Stick & D-Pad shortcuts/precision
    handle_camera_scroll_and_dpad(kDown, kHeld);

    touchPosition touch;
    hidTouchRead(&touch);
    int is_touching = (kHeld & KEY_TOUCH) && (touch.px > 0 || touch.py > 0);
    int bx = calc_bound(touch.px, 0, N3DS_BOTTOM_WIDTH - 1);
    int by = calc_bound(touch.py, 0, N3DS_BOTTOM_HEIGHT - 1);

    if (is_touching) {
        if (!n3ds_state.touch_was_down) {
            // ==================== TOUCH DOWN ====================
            n3ds_state.touch_was_down = 1;
            n3ds_state.start_bx = bx;
            n3ds_state.start_by = by;
            n3ds_state.last_bx = bx;
            n3ds_state.last_by = by;
            n3ds_state.is_dragging = 0;
            n3ds_state.drag_accum_x = 0;
            n3ds_state.drag_accum_y = 0;
            n3ds_state.touch_action = TOUCH_ACTION_NONE;

            if (is_city_context()) {
                if (bx >= 158) {
                    // Touch on Fixed Right Build Panel (x = 158..319)
                    int cx = 0, cy = 0, gr = -1, gc = -1;
                    if (map_right_panel_touch(cw, bx, by, &cx, &cy, &gr, &gc)) {
                        int active_r = -1, active_c = -1;
                        int has_active = get_active_build_button_grid(&active_r, &active_c);

                        // Tapping the currently highlighted build category button toggles Modo Construtor OFF
                        if (has_active && gr == active_r && gc == active_c && window_is(WINDOW_CITY)) {
                            building_construction_cancel();
                            building_construction_clear_type();
                            window_request_refresh();
                            n3ds_state.touch_action = TOUCH_ACTION_CUSTOM_BUTTON;
                        } else {
                            if (window_is(WINDOW_OVERLAY_MENU) || window_is(WINDOW_TOP_MENU)) {
                                window_city_show();
                            }
                            if (gr >= 0 && gr <= 3) {
                                n3ds_state.left_map_mode = 0;
                                if (building_construction_type() != BUILDING_NONE) {
                                    building_construction_cancel();
                                    building_construction_clear_type();
                                }
                            }
                            mouse_set_position(cx, cy);
                            mouse_set_left_down(1);
                            n3ds_state.touch_action = TOUCH_ACTION_LEFT_CLICK;
                        }
                    }
                } else if (by >= 204) {
                    // Touch on Bottom Roman Statue Bar (x = 0..157, y = 204..239)
                    if (bx >= 18 && bx <= 78) {
                        toggle_left_map_mode();
                        n3ds_state.touch_action = TOUCH_ACTION_CUSTOM_BUTTON;
                    } else if (bx >= 80 && bx <= 142) {
                        toggle_ingame_menu();
                        n3ds_state.touch_action = TOUCH_ACTION_CUSTOM_BUTTON;
                    }
                } else if (bx >= 8 && bx < 150 && by >= 4 && by < 204) {
                    // Touch inside Left Main Pane (142x200)
                    if (window_is(WINDOW_BUILD_MENU)) {
                        int sx, sy, sw, sh;
                        get_build_submenu_crop(cw, &sx, &sy, &sw, &sh);
                        int cx = sx + ((bx - 8) * sw) / 142;
                        int cy = sy + ((by - 4) * sh) / 200;
                        mouse_set_position(cx, cy);
                        mouse_set_left_down(1);
                        n3ds_state.touch_action = TOUCH_ACTION_LEFT_CLICK;
                    } else if (window_is(WINDOW_OVERLAY_MENU)) {
                        int sx, sy, sw, sh;
                        get_overlay_submenu_crop(&sx, &sy, &sw, &sh);
                        int cx = sx + ((bx - 8) * sw) / 142;
                        int cy = sy + ((by - 4) * sh) / 200;
                        mouse_set_position(cx, cy);
                        mouse_set_left_down(1);
                        n3ds_state.touch_action = TOUCH_ACTION_LEFT_CLICK;
                    } else if (window_is(WINDOW_TOP_MENU)) {
                        int cx = ((bx - 8) * 284) / 142;
                        int cy = ((by - 4) * 200) / 200;
                        mouse_set_position(cx, cy);
                        mouse_set_left_down(1);
                        n3ds_state.touch_action = TOUCH_ACTION_LEFT_CLICK;
                    } else if (n3ds_state.left_map_mode) {
                        // Minimap + Overlays Mode
                        if (by >= 12 && by <= 46 && bx >= 16 && bx <= 142) {
                            // Overlays button (x_src = sb_x + 4 .. sb_x + 121, y_src = 27..58)
                            int cx = (sb_x + 4) + ((bx - 16) * 117) / 126;
                            int cy = 27 + ((by - 12) * 31) / 34;
                            mouse_set_position(cx, cy);
                            mouse_set_left_down(1);
                            n3ds_state.touch_action = TOUCH_ACTION_LEFT_CLICK;
                        } else if (by >= 56 && by <= 170) {
                            // 1:1 Minimap (x_src = sb_x + 8 .. sb_x + 150, y_src = 59..170)
                            int cx = (sb_x + 8) + calc_bound(bx - 8, 0, 141);
                            int cy = 59 + calc_bound(by - 58, 0, 110);
                            mouse_set_position(cx, cy);
                            mouse_set_left_down(1);
                            n3ds_state.touch_action = TOUCH_ACTION_LEFT_CLICK;
                        }
                    } else {
                        // City Construction Viewport (142x200)
                        int sx, sy, sw, sh;
                        get_bottom_city_viewport_src(cw, &sx, &sy, &sw, &sh);
                        int cx = sx + ((bx - 8) * sw) / 142;
                        int cy = sy + ((by - 4) * sh) / 200;

                        if (building_construction_type() != BUILDING_NONE || window_is(WINDOW_CITY_MILITARY)) {
                            // MODO CONSTRUTOR: Camera locked, Stylus builds directly
                            mouse_set_position(cx, cy);
                            mouse_set_left_down(1);
                            n3ds_state.touch_action = TOUCH_ACTION_CITY_BUILD;
                        } else {
                            // MODO EXPLORACAO: Drag pans camera (scaled by Top Screen Zoom), quick tap inspects building/legion
                            mouse_set_position(cx, cy);
                            n3ds_state.touch_action = TOUCH_ACTION_CITY_EXPLORE;
                        }
                    }
                }
            } else if (window_is(WINDOW_ADVISORS)) {
                int cx = 320, cy = 240;
                if (by >= 4 && by < 114 && bx >= 8 && bx < 312) {
                    // Interactive Advisor Content Area
                    cx = 16 + ((bx - 8) * 608) / 304;
                    cy = 48 + ((by - 4) * 376) / 110;
                } else if (by >= 116 && by < 204 && bx >= 16 && bx < 304) {
                    // 2 Rows of 6 Advisor Portraits (40x40 each, 48px stride)
                    int col = calc_bound((bx - 16) / 48, 0, 5);
                    int row = (by < 160) ? 0 : 1;
                    int idx = row * 6 + col;
                    cx = 48 * idx + 32;
                    cy = 460;
                } else if (by >= 204) {
                    // Bottom Statue Bar -> Return to City button (index 12 at x=588, y=441)
                    cx = 608;
                    cy = 460;
                }
                n3ds_state.cam_center_x = cx;
                n3ds_state.cam_center_y = cy;
                mouse_set_position(cx, cy);
                mouse_set_left_down(1);
                n3ds_state.touch_action = TOUCH_ACTION_LEFT_CLICK;
            } else if (window_is(WINDOW_EMPIRE)) {
                if (by < 168 && bx >= 8 && bx < 312) {
                    // Touch on Mediterranean Map: drag pans, tap selects city
                    int cx = 16 + ((bx - 8) * 608) / 304;
                    int cy = 16 + ((by - 4) * 328) / 164;
                    n3ds_state.cam_center_x = cx;
                    n3ds_state.cam_center_y = cy;
                    mouse_set_position(cx, cy);
                    n3ds_state.touch_action = TOUCH_ACTION_EMPIRE_MAP;
                } else if (by >= 168) {
                    // Touch on Bottom Trade Panel (y_src = 352..480)
                    int cx = (bx * 640) / 320;
                    int cy = 352 + ((by - 168) * 128) / 72;
                    mouse_set_position(cx, cy);
                    mouse_set_left_down(1);
                    n3ds_state.touch_action = TOUCH_ACTION_LEFT_CLICK;
                }
            } else {
                // Standard Menus, Briefings & Modal Dialogs (Full 640x480 -> 320x240)
                int cx = (bx * cw) / N3DS_BOTTOM_WIDTH;
                int cy = (by * ch) / N3DS_BOTTOM_HEIGHT;
                n3ds_state.cam_center_x = cx;
                n3ds_state.cam_center_y = cy;
                mouse_set_position(cx, cy);
                if (kHeld & KEY_L) {
                    mouse_set_right_down(1);
                    n3ds_state.touch_action = TOUCH_ACTION_RIGHT_CLICK;
                } else {
                    mouse_set_left_down(1);
                    n3ds_state.touch_action = TOUCH_ACTION_LEFT_CLICK;
                }
            }
        } else {
            // ==================== TOUCH HELD / DRAG ====================
            if (n3ds_state.touch_action == TOUCH_ACTION_CITY_EXPLORE) {
                if (!n3ds_state.is_dragging &&
                    (abs(bx - n3ds_state.start_bx) > 3 || abs(by - n3ds_state.start_by) > 3)) {
                    n3ds_state.is_dragging = 1;
                }
                if (n3ds_state.is_dragging) {
                    int raw_dx = n3ds_state.last_bx - bx;
                    int raw_dy = n3ds_state.last_by - by;
                    n3ds_state.last_bx = bx;
                    n3ds_state.last_by = by;

                    // Scale Stylus drag speed by Top Screen Zoom AND Bottom Screen Zoom!
                    // - Top 2.0X (Close-Up): 1.0x base speed
                    // - Top 1.0X (Standard): 2.0x base speed
                    // - Top 0.5X (Panoramic): 3.6x base speed
                    int mult_num = (n3ds_state.top_zoom == N3DS_TOP_ZOOM_0_5X) ? 36 :
                                   (n3ds_state.top_zoom == N3DS_TOP_ZOOM_1_0X) ? 20 : 10;
                    if (n3ds_state.bot_zoom) {
                        mult_num = (mult_num * 3) / 2;
                    }

                    n3ds_state.drag_accum_x += raw_dx * mult_num;
                    n3ds_state.drag_accum_y += raw_dy * mult_num;
                    int dx = n3ds_state.drag_accum_x / 10;
                    int dy = n3ds_state.drag_accum_y / 10;
                    n3ds_state.drag_accum_x %= 10;
                    n3ds_state.drag_accum_y %= 10;

                    if (dx != 0 || dy != 0) {
                        // Fluid 60 FPS scroll without window_invalidate()
                        city_view_scroll(dx, dy);
                    }
                }
            } else if (n3ds_state.touch_action == TOUCH_ACTION_CITY_BUILD) {
                // If user slides Stylus outside the Left Main Pane onto the column/panel, abort current stroke!
                if (bx < 6 || bx >= 152 || by < 2 || by >= 206) {
                    building_construction_cancel();
                    mouse_set_left_down(0);
                    n3ds_state.touch_action = TOUCH_ACTION_NONE;
                    window_request_refresh();
                } else {
                    int sx, sy, sw, sh;
                    get_bottom_city_viewport_src(cw, &sx, &sy, &sw, &sh);
                    int clamped_bx = calc_bound(bx, 8, 149);
                    int clamped_by = calc_bound(by, 4, 203);
                    int cx = sx + ((clamped_bx - 8) * sw) / 142;
                    int cy = sy + ((clamped_by - 4) * sh) / 200;
                    mouse_set_position(cx, cy);
                }
            } else if (n3ds_state.touch_action == TOUCH_ACTION_EMPIRE_MAP) {
                if (!n3ds_state.is_dragging &&
                    (abs(bx - n3ds_state.start_bx) > 3 || abs(by - n3ds_state.start_by) > 3)) {
                    n3ds_state.is_dragging = 1;
                }
                if (n3ds_state.is_dragging) {
                    int dx = (n3ds_state.last_bx - bx) * 2;
                    int dy = (n3ds_state.last_by - by) * 2;
                    n3ds_state.last_bx = bx;
                    n3ds_state.last_by = by;
                    if (dx != 0 || dy != 0) {
                        empire_scroll_map(dx, dy);
                    }
                }
            } else if (n3ds_state.touch_action == TOUCH_ACTION_LEFT_CLICK) {
                if (is_city_context()) {
                    if (bx >= 158) {
                        int cx = 0, cy = 0;
                        if (map_right_panel_touch(cw, bx, by, &cx, &cy, NULL, NULL)) {
                            mouse_set_position(cx, cy);
                        }
                    } else if (window_is(WINDOW_BUILD_MENU) && bx >= 8 && bx < 150 && by >= 4 && by < 204) {
                        int sx, sy, sw, sh;
                        get_build_submenu_crop(cw, &sx, &sy, &sw, &sh);
                        mouse_set_position(sx + ((bx - 8) * sw) / 142, sy + ((by - 4) * sh) / 200);
                    } else if (window_is(WINDOW_OVERLAY_MENU) && bx >= 8 && bx < 150 && by >= 4 && by < 204) {
                        int sx, sy, sw, sh;
                        get_overlay_submenu_crop(&sx, &sy, &sw, &sh);
                        mouse_set_position(sx + ((bx - 8) * sw) / 142, sy + ((by - 4) * sh) / 200);
                    } else if (window_is(WINDOW_TOP_MENU) && bx >= 8 && bx < 150 && by >= 4 && by < 204) {
                        mouse_set_position(((bx - 8) * 284) / 142, ((by - 4) * 200) / 200);
                    } else if (n3ds_state.left_map_mode && by >= 56 && by <= 170 && bx >= 8 && bx < 150) {
                        int cx = (sb_x + 8) + calc_bound(bx - 8, 0, 141);
                        int cy = 59 + calc_bound(by - 58, 0, 110);
                        mouse_set_position(cx, cy);
                    }
                } else if (window_is(WINDOW_ADVISORS)) {
                    if (by >= 4 && by < 114 && bx >= 8 && bx < 312) {
                        int cx = 16 + ((bx - 8) * 608) / 304;
                        int cy = 48 + ((by - 4) * 376) / 110;
                        n3ds_state.cam_center_x = cx;
                        n3ds_state.cam_center_y = cy;
                        mouse_set_position(cx, cy);
                    }
                } else if (window_is(WINDOW_EMPIRE)) {
                    if (by >= 168) {
                        int cx = (bx * 640) / 320;
                        int cy = 352 + ((by - 168) * 128) / 72;
                        mouse_set_position(cx, cy);
                    }
                } else {
                    int cx = (bx * cw) / N3DS_BOTTOM_WIDTH;
                    int cy = (by * ch) / N3DS_BOTTOM_HEIGHT;
                    n3ds_state.cam_center_x = cx;
                    n3ds_state.cam_center_y = cy;
                    mouse_set_position(cx, cy);
                }
            }
        }
    } else if (n3ds_state.touch_was_down) {
        // ==================== TOUCH RELEASED ====================
        n3ds_state.touch_was_down = 0;

        if (n3ds_state.touch_action == TOUCH_ACTION_CITY_EXPLORE) {
            if (!n3ds_state.is_dragging) {
                // Quick tap in Modo Exploracao -> Inspect building (right-click) or select legion (left-click)
                int sx, sy, sw, sh;
                get_bottom_city_viewport_src(cw, &sx, &sy, &sw, &sh);
                int cx = sx + ((n3ds_state.start_bx - 8) * sw) / 142;
                int cy = sy + ((n3ds_state.start_by - 4) * sh) / 200;
                mouse_set_position(cx, cy);
                mouse_set_left_down(1);
                mouse_set_left_down(0);
                mouse_set_right_down(1);
                mouse_set_right_down(0);
            }
        } else if (n3ds_state.touch_action == TOUCH_ACTION_EMPIRE_MAP) {
            if (!n3ds_state.is_dragging) {
                // Quick tap on Empire Map -> Select city
                int cx = 16 + ((n3ds_state.start_bx - 8) * 608) / 304;
                int cy = 16 + ((n3ds_state.start_by - 4) * 328) / 164;
                n3ds_state.cam_center_x = cx;
                n3ds_state.cam_center_y = cy;
                mouse_set_position(cx, cy);
                mouse_set_left_down(1);
                mouse_set_left_down(0);
            }
        } else if (n3ds_state.touch_action == TOUCH_ACTION_LEFT_CLICK ||
                   n3ds_state.touch_action == TOUCH_ACTION_CITY_BUILD) {
            mouse_set_left_down(0);
        } else if (n3ds_state.touch_action == TOUCH_ACTION_RIGHT_CLICK) {
            mouse_set_right_down(0);
        }

        n3ds_state.touch_action = TOUCH_ACTION_NONE;
        n3ds_state.is_dragging = 0;
    } else if (!is_city_context() && !window_is(WINDOW_EMPIRE)) {
        const mouse *m = mouse_get();
        n3ds_state.cam_center_x = m->x;
        n3ds_state.cam_center_y = m->y;
    }
}

// ============================================================================
// EXACT INTEGER BLITTERS (1:1, 2:1, 1:2) & ROMAN ARCHITECTURE RENDERER
// ============================================================================

// Convert Julius 0xAARRGGBB pixel to 3DS GSP_RGBA8_OES (0xRRGGBBAA)
static inline uint32_t argb_to_rgba8(uint32_t c)
{
    return (c << 8) | 0xFFu;
}

// Fast 2x2 pixel average in 0xAARRGGBB followed by RGBA8 conversion
static inline uint32_t avg2x2_to_rgba8(uint32_t p00, uint32_t p01, uint32_t p10, uint32_t p11)
{
    uint32_t ab = ((p00 & 0xFEFEFEFEu) >> 1) + ((p01 & 0xFEFEFEFEu) >> 1);
    uint32_t cd = ((p10 & 0xFEFEFEFEu) >> 1) + ((p11 & 0xFEFEFEFEu) >> 1);
    uint32_t avg = ((ab & 0xFEFEFEFEu) >> 1) + ((cd & 0xFEFEFEFEu) >> 1);
    return (avg << 8) | 0xFFu;
}

static inline void put_pixel_3ds(uint32_t *fb, int x, int y, int screen_w, uint32_t rgba)
{
    if ((unsigned)x < (unsigned)screen_w && (unsigned)y < 240u) {
        fb[(239 - y) + 240 * x] = rgba;
    }
}

static void fill_rect_3ds(uint32_t *fb, int screen_w, int rx, int ry, int rw, int rh, uint32_t rgba)
{
    int x0 = calc_bound(rx, 0, screen_w - 1);
    int x1 = calc_bound(rx + rw - 1, 0, screen_w - 1);
    int y0 = calc_bound(ry, 0, 239);
    int y1 = calc_bound(ry + rh - 1, 0, 239);
    for (int x = x0; x <= x1; x++) {
        uint32_t *col = &fb[240 * x];
        for (int y = y0; y <= y1; y++) {
            col[239 - y] = rgba;
        }
    }
}

static void draw_rect_outline_3ds(uint32_t *fb, int screen_w, int rx, int ry, int rw, int rh, uint32_t rgba)
{
    int x0 = calc_bound(rx, 0, screen_w - 1);
    int x1 = calc_bound(rx + rw - 1, 0, screen_w - 1);
    int y0 = calc_bound(ry, 0, 239);
    int y1 = calc_bound(ry + rh - 1, 0, 239);

    for (int x = x0; x <= x1; x++) {
        put_pixel_3ds(fb, x, y0, screen_w, rgba);
        put_pixel_3ds(fb, x, y1, screen_w, rgba);
    }
    for (int y = y0; y <= y1; y++) {
        put_pixel_3ds(fb, x0, y, screen_w, rgba);
        put_pixel_3ds(fb, x1, y, screen_w, rgba);
    }
}

// 1. Exact 1:1 Native Pixel-Perfect Blitter (dst_w x dst_h == src_w x src_h)
static void blit_1to1_3ds(
    uint32_t *fb, int screen_w,
    int dst_x, int dst_y, int w, int h,
    const uint32_t *canvas, int canvas_w, int canvas_h,
    int src_x, int src_y)
{
    for (int dx = 0; dx < w; dx++) {
        int px = dst_x + dx;
        int sx = src_x + dx;
        if ((unsigned)px >= (unsigned)screen_w || (unsigned)sx >= (unsigned)canvas_w) continue;
        uint32_t *dst_col = &fb[240 * px];
        for (int dy = 0; dy < h; dy++) {
            int py = dst_y + dy;
            int sy = src_y + dy;
            if ((unsigned)py < 240u && (unsigned)sy < (unsigned)canvas_h) {
                dst_col[239 - py] = argb_to_rgba8(canvas[sy * canvas_w + sx]);
            }
        }
    }
}

// 2. Exact 2:1 Integer Downscale Blitter (src_w = 2*dst_w, src_h = 2*dst_h)
// Steps sx and sy by exactly 2 every pixel -> 100% immune to Moire horizontal stripes!
static void blit_downscale_2to1_3ds(
    uint32_t *fb, int screen_w,
    int dst_x, int dst_y, int dst_w, int dst_h,
    const uint32_t *canvas, int canvas_w, int canvas_h,
    int src_x, int src_y)
{
    for (int dx = 0; dx < dst_w; dx++) {
        int px = dst_x + dx;
        int sx = src_x + dx * 2;
        if ((unsigned)px >= (unsigned)screen_w || sx < 0 || sx + 1 >= canvas_w) continue;
        uint32_t *dst_col = &fb[240 * px];
        for (int dy = 0; dy < dst_h; dy++) {
            int py = dst_y + dy;
            int sy = src_y + dy * 2;
            if ((unsigned)py < 240u && sy >= 0 && sy + 1 < canvas_h) {
                const uint32_t *r0 = &canvas[sy * canvas_w + sx];
                const uint32_t *r1 = r0 + canvas_w;
                dst_col[239 - py] = avg2x2_to_rgba8(r0[0], r0[1], r1[0], r1[1]);
            }
        }
    }
}

// 3. Exact 1:2 Integer Upscale Blitter (dst_w = 2*src_w, dst_h = 2*src_h)
// Renders each source pixel as a razor-sharp 2x2 block -> 2x magnification with zero blur or stripes!
static void blit_upscale_1to2_3ds(
    uint32_t *fb, int screen_w,
    int dst_x, int dst_y, int dst_w, int dst_h,
    const uint32_t *canvas, int canvas_w, int canvas_h,
    int src_x, int src_y)
{
    for (int dx = 0; dx < dst_w; dx++) {
        int px = dst_x + dx;
        int sx = src_x + (dx >> 1);
        if ((unsigned)px >= (unsigned)screen_w || (unsigned)sx >= (unsigned)canvas_w) continue;
        uint32_t *dst_col = &fb[240 * px];
        for (int dy = 0; dy < dst_h; dy++) {
            int py = dst_y + dy;
            int sy = src_y + (dy >> 1);
            if ((unsigned)py < 240u && (unsigned)sy < (unsigned)canvas_h) {
                dst_col[239 - py] = argb_to_rgba8(canvas[sy * canvas_w + sx]);
            }
        }
    }
}

// General-purpose rectangular blitter (automatically dispatches to exact integer blitters when applicable)
static void blit_canvas_rect_3ds(
    uint32_t *fb, int screen_w,
    int dst_x, int dst_y, int dst_w, int dst_h,
    const uint32_t *canvas, int canvas_w, int canvas_h,
    int src_x, int src_y, int src_w, int src_h)
{
    if (dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0) {
        return;
    }
    if (dst_w == src_w && dst_h == src_h) {
        blit_1to1_3ds(fb, screen_w, dst_x, dst_y, dst_w, dst_h, canvas, canvas_w, canvas_h, src_x, src_y);
        return;
    }
    if (src_w == dst_w * 2 && src_h == dst_h * 2) {
        blit_downscale_2to1_3ds(fb, screen_w, dst_x, dst_y, dst_w, dst_h, canvas, canvas_w, canvas_h, src_x, src_y);
        return;
    }
    if (dst_w == src_w * 2 && dst_h == src_h * 2) {
        blit_upscale_1to2_3ds(fb, screen_w, dst_x, dst_y, dst_w, dst_h, canvas, canvas_w, canvas_h, src_x, src_y);
        return;
    }

    for (int dx = 0; dx < dst_w; dx++) {
        int px = dst_x + dx;
        if ((unsigned)px >= (unsigned)screen_w) continue;
        int sx = src_x + (dx * src_w) / dst_w;
        if (sx < 0) sx = 0;
        if (sx >= canvas_w) sx = canvas_w - 1;
        int sx1 = (sx + 1 < canvas_w) ? (sx + 1) : sx;

        uint32_t *dst_col = &fb[240 * px];
        for (int dy = 0; dy < dst_h; dy++) {
            int py = dst_y + dy;
            if ((unsigned)py >= 240u) continue;
            int sy = src_y + (dy * src_h) / dst_h;
            if (sy < 0) sy = 0;
            if (sy >= canvas_h) sy = canvas_h - 1;
            int sy1 = (sy + 1 < canvas_h) ? (sy + 1) : sy;

            const uint32_t *r0 = &canvas[sy * canvas_w];
            const uint32_t *r1 = &canvas[sy1 * canvas_w];
            dst_col[239 - py] = avg2x2_to_rgba8(r0[sx], r0[sx1], r1[sx], r1[sx1]);
        }
    }
}

// Crisp 4x5 pixel bitmap font for custom Roman plaques & buttons
static uint8_t get_glyph_row_4x5(char c, int row)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    static const uint8_t font_letters[26][5] = {
        {0x6, 0x9, 0xF, 0x9, 0x9}, // A
        {0xE, 0x9, 0xE, 0x9, 0xE}, // B
        {0x7, 0x8, 0x8, 0x8, 0x7}, // C
        {0xE, 0x9, 0x9, 0x9, 0xE}, // D
        {0xF, 0x8, 0xE, 0x8, 0xF}, // E
        {0xF, 0x8, 0xE, 0x8, 0x8}, // F
        {0x7, 0x8, 0xB, 0x9, 0x7}, // G
        {0x9, 0x9, 0xF, 0x9, 0x9}, // H
        {0xE, 0x4, 0x4, 0x4, 0xE}, // I
        {0x3, 0x1, 0x1, 0x9, 0x6}, // J
        {0x9, 0xA, 0xC, 0xA, 0x9}, // K
        {0x8, 0x8, 0x8, 0x8, 0xF}, // L
        {0x9, 0xF, 0xF, 0x9, 0x9}, // M
        {0x9, 0xD, 0xB, 0x9, 0x9}, // N
        {0x6, 0x9, 0x9, 0x9, 0x6}, // O
        {0xE, 0x9, 0xE, 0x8, 0x8}, // P
        {0x6, 0x9, 0x9, 0xB, 0x7}, // Q
        {0xE, 0x9, 0xE, 0xA, 0x9}, // R
        {0x7, 0x8, 0x6, 0x1, 0xE}, // S
        {0xE, 0x4, 0x4, 0x4, 0x4}, // T
        {0x9, 0x9, 0x9, 0x9, 0x6}, // U
        {0x9, 0x9, 0x9, 0x6, 0x6}, // V
        {0x9, 0x9, 0xF, 0xF, 0x9}, // W
        {0x9, 0x9, 0x6, 0x9, 0x9}, // X
        {0x9, 0x9, 0x6, 0x4, 0x4}, // Y
        {0xF, 0x2, 0x4, 0x8, 0xF}  // Z
    };
    static const uint8_t font_digits[10][5] = {
        {0x6, 0x9, 0x9, 0x9, 0x6}, // 0
        {0x4, 0xC, 0x4, 0x4, 0xE}, // 1
        {0xE, 0x1, 0x6, 0x8, 0xF}, // 2
        {0xE, 0x1, 0x6, 0x1, 0xE}, // 3
        {0x9, 0x9, 0xF, 0x1, 0x1}, // 4
        {0xF, 0x8, 0xE, 0x1, 0xE}, // 5
        {0x7, 0x8, 0xE, 0x9, 0x6}, // 6
        {0xF, 0x1, 0x2, 0x4, 0x4}, // 7
        {0x6, 0x9, 0x6, 0x9, 0x6}, // 8
        {0x6, 0x9, 0x7, 0x1, 0xE}  // 9
    };
    if (c >= 'A' && c <= 'Z') return font_letters[c - 'A'][row];
    if (c >= '0' && c <= '9') return font_digits[c - '0'][row];
    if (c == '.') return (row == 4) ? 0x4 : 0x0;
    if (c == ':') return (row == 1 || row == 3) ? 0x4 : 0x0;
    if (c == '-') return (row == 2) ? 0xE : 0x0;
    if (c == '/') return (row == 0) ? 0x1 : (row == 1) ? 0x2 : (row == 2) ? 0x4 : (row == 3) ? 0x8 : 0x0;
    return 0;
}

static void draw_text_centered_3ds(
    uint32_t *fb, int screen_w,
    int rx, int ry, int rw, int rh,
    const char *str, uint32_t fg_rgba, uint32_t shadow_rgba)
{
    int len = (int)strlen(str);
    if (len <= 0) return;
    int text_w = len * 5 - 1;
    int start_x = rx + (rw - text_w) / 2;
    int start_y = ry + (rh - 5) / 2;

    for (int i = 0; i < len; i++) {
        int cx = start_x + i * 5;
        char ch = str[i];
        if (ch == ' ') continue;
        for (int row = 0; row < 5; row++) {
            uint8_t bits = get_glyph_row_4x5(ch, row);
            for (int col = 0; col < 4; col++) {
                if (bits & (0x8 >> col)) {
                    if (shadow_rgba) {
                        put_pixel_3ds(fb, cx + col + 1, start_y + row + 1, screen_w, shadow_rgba);
                    }
                    put_pixel_3ds(fb, cx + col, start_y + row, screen_w, fg_rgba);
                }
            }
        }
    }
}

// Draw a sculpted Fluted Roman Marble Column with Corinthian Capital and Stepped Attic Base (width = 8px)
static void draw_roman_column_3ds(uint32_t *fb, int screen_w, int col_x, int y0, int y1)
{
    static const uint32_t shaft_colors[8] = {
        0x726A5AFFu, // 0: Outer left dark bevel
        0xEFE7D6FFu, // 1: Sunlit marble edge
        0xADA28EFFu, // 2: Flute 1 concave shadow
        0xFAF3E3FFu, // 3: Central ridge highlight
        0xA39884FFu, // 4: Flute 2 concave shadow
        0xE6DEC8FFu, // 5: Right ridge lit
        0x918672FFu, // 6: Flute 3 concave shadow
        0x5E5648FFu  // 7: Outer right shadow edge
    };

    for (int y = y0; y < y1; y++) {
        int dy = y - y0;
        int dist_bot = (y1 - 1) - y;

        for (int dx = 0; dx < 8; dx++) {
            uint32_t c;
            if (dy < 3) {
                // Top Corinthian Abacus slab
                c = (dy == 0) ? 0xF7EFE0FFu : (dy == 1) ? 0xDDD3BEFFu : 0x7A705EFFu;
            } else if (dy < 10) {
                // Carved Golden-Ivory Volutes & Acanthus Leaves
                int pat = (dx + dy) & 1;
                if (dx == 0 || dx == 7) c = 0x7A6E54FFu;
                else if (dx == 1 || dx == 6) c = pat ? 0xF2E2B8FFu : 0xC8B486FFu;
                else c = pat ? 0xDFCFA6FFu : 0x9E8C64FFu;
            } else if (dy == 10 || dy == 11) {
                // Astragal necking ring
                c = (dy == 10) ? 0x6A604EFFu : 0xF2EAD8FFu;
            } else if (dist_bot < 3) {
                // Bottom square marble plinth
                c = (dist_bot == 2) ? 0xEFE7D6FFu : (dist_bot == 1) ? 0xC8BEA8FFu : 0x5E5648FFu;
            } else if (dist_bot < 6) {
                // Lower torus molding
                c = (dx == 0 || dx == 7) ? 0x6E6452FFu : (dist_bot == 5) ? 0xF5EDE0FFu : 0xD2C8B2FFu;
            } else if (dist_bot == 6 || dist_bot == 7) {
                // Scotia dark recess
                c = 0x6E6452FFu;
            } else if (dist_bot == 8 || dist_bot == 9) {
                // Upper torus molding
                c = (dist_bot == 9) ? 0xF2EAD8FFu : 0xBEB49EFFu;
            } else {
                // Fluted Marble Shaft with subtle stone drum joints every 36px
                c = shaft_colors[dx];
                if ((dy % 36) == 0) {
                    c = 0x8C8270FFu;
                }
            }
            put_pixel_3ds(fb, col_x + dx, y, screen_w, c);
        }
    }
}

// Draw a chiseled Roman stone button/plaque with optional gold active highlight
static void draw_stone_button_3ds(
    uint32_t *fb, int screen_w,
    int rx, int ry, int rw, int rh,
    const char *label, int is_active)
{
    uint32_t bg_top    = is_active ? 0x6B5220FFu : 0x4A443AFFu;
    uint32_t bg_bot    = is_active ? 0x4E3A14FFu : 0x363129FFu;
    uint32_t border_lt = is_active ? 0xFFE066FFu : 0xDED4BEFFu;
    uint32_t border_dk = is_active ? 0x9E7B22FFu : 0x625A4CFFu;
    uint32_t text_col  = is_active ? 0xFFF099FFu : 0xF5EFE0FFu;

    for (int y = 0; y < rh; y++) {
        uint32_t row_bg = (y < rh / 2) ? bg_top : bg_bot;
        for (int x = 0; x < rw; x++) {
            uint32_t c = row_bg;
            if (x == 0 || y == 0 || x == rw - 1 || y == rh - 1) {
                c = 0x24201AFFu;
            } else if (x == 1 || y == 1) {
                c = border_lt;
            } else if (x == rw - 2 || y == rh - 2) {
                c = border_dk;
            }
            put_pixel_3ds(fb, rx + x, ry + y, screen_w, c);
        }
    }

    draw_text_centered_3ds(fb, screen_w, rx, ry, rw, rh, label, text_col, 0x181410FFu);
}

static void draw_cursor_on_3ds(uint32_t *fb, int screen_w, int cx, int cy)
{
    static const uint32_t cursor_rgba[] = {
        0, 0, 0,
        0x000000FFu, // 3: Black
        0x3F3F3FFFu, // 4: Dark gray
        0x737373FFu, // 5: Medium gray
        0xB3B3B3FFu, // 6: Light gray
        0xFFFFFFFFu  // 7: White
    };
    cursor_shape shape = platform_cursor_get_current_shape();
    const cursor *c = input_cursor_data(shape, CURSOR_SCALE_1);
    if (!c) {
        return;
    }
    int start_x = cx - c->hotspot_x;
    int start_y = cy - c->hotspot_y;
    for (int row = 0; row < c->height; row++) {
        int py = start_y + row;
        if (py < 0 || py >= 240) continue;
        for (int col = 0; col < c->width; col++) {
            int px = start_x + col;
            if (px < 0 || px >= screen_w) continue;
            int idx = (int)c->data[row * c->width + col] - 32;
            if (idx >= 3 && idx <= 7) {
                put_pixel_3ds(fb, px, py, screen_w, cursor_rgba[idx]);
            }
        }
    }
}

// ============================================================================
// CONTEXT 1 RENDERER: CITY GAMEPLAY (TOP & BOTTOM SCREENS)
// ============================================================================

static void render_city_dual_screens(
    uint32_t *top_fb, uint32_t *bot_fb,
    const uint32_t *canvas, int canvas_w, int canvas_h)
{
    int sb_x = canvas_w - 162;
    int vp_cx = sb_x / 2;
    int vp_cy = 252;

    // ---------------- TOP SCREEN (400x240 Widescreen) ----------------
    if (window_is(WINDOW_TOP_MENU)) {
        // When In-Game Menu is open, show top-left 400x240 of canvas 1:1 on Top Screen too
        blit_1to1_3ds(top_fb, N3DS_TOP_WIDTH,
            0, 0, N3DS_TOP_WIDTH, N3DS_TOP_HEIGHT,
            canvas, canvas_w, canvas_h,
            0, 0);
    } else {
        // 1. Top Status Bar (y = 0..24):
        // Right 304px (x = 96..400): 1:1 copy of Dn (Funds), Pop (Population), and Date from x_src = 336..640, y_src = 0..24
        blit_1to1_3ds(top_fb, N3DS_TOP_WIDTH,
            96, 0, 304, 24,
            canvas, canvas_w, canvas_h,
            336, 0);

        // Left 96px (x = 0..96): Clean Roman stone plaque showing current Zoom / Pause / Construction Mode
        int is_paused = game_state_is_paused();
        int is_building = (building_construction_type() != BUILDING_NONE);
        const char *mode_label = "ZOOM 1.0X";
        if (is_paused) {
            mode_label = "PAUSADO";
        } else if (is_building) {
            mode_label = "CONSTRUIR";
        } else if (n3ds_state.top_zoom == N3DS_TOP_ZOOM_0_5X) {
            mode_label = "ZOOM 0.5X";
        } else if (n3ds_state.top_zoom == N3DS_TOP_ZOOM_2_0X) {
            mode_label = "ZOOM 2.0X";
        }
        draw_stone_button_3ds(top_fb, N3DS_TOP_WIDTH, 0, 0, 96, 24, mode_label, is_paused || is_building);

        // 2. City Viewport (y = 24..240, 400x216 pixels) using 100% Exact Integer Ratios:
        if (n3ds_state.top_zoom == N3DS_TOP_ZOOM_0_5X && canvas_w >= N3DS_PANORAMIC_WIDTH) {
            // ZOOM 0.5X (Panoramico): 800x432 -> 400x216 Exact 2:1 Integer Downscale (4x area, zero Moire stripes!)
            blit_downscale_2to1_3ds(top_fb, N3DS_TOP_WIDTH,
                0, 24, 400, 216,
                canvas, canvas_w, canvas_h,
                vp_cx - 400, vp_cy - 216);
        } else if (n3ds_state.top_zoom == N3DS_TOP_ZOOM_2_0X) {
            // ZOOM 2.0X (Super Zoom / Close-Up): 200x108 -> 400x216 Exact 1:2 Integer Upscale (2x bigger sprites, crisp 2x2 pixels!)
            blit_upscale_1to2_3ds(top_fb, N3DS_TOP_WIDTH,
                0, 24, 400, 216,
                canvas, canvas_w, canvas_h,
                vp_cx - 100, vp_cy - 54);
        } else {
            // ZOOM 1.0X (Padrao Nativo): 400x216 -> 400x216 Exact 1:1 Native Copy (crystal clear!)
            blit_1to1_3ds(top_fb, N3DS_TOP_WIDTH,
                0, 24, 400, 216,
                canvas, canvas_w, canvas_h,
                vp_cx - 200, vp_cy - 108);
        }
    }

    // ---------------- BOTTOM SCREEN (320x240 Touch Screen) ----------------

    // 1. Fixed Right Control & Build Panel (x = 158..320, y = 0..240, 162px 1:1 width from x_src = sb_x..canvas_w)
    blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, 0, 162, 3, canvas, canvas_w, canvas_h, sb_x, 24);
    blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, 3, 162, 23, canvas, canvas_w, canvas_h, sb_x, 179);
    blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, 26, 162, 2, canvas, canvas_w, canvas_h, sb_x, 203);
    blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, 28, 162, 22, canvas, canvas_w, canvas_h, sb_x, 208);
    blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, 50, 162, 2, canvas, canvas_w, canvas_h, sb_x, 231);
    blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, 52, 162, 17, canvas, canvas_w, canvas_h, sb_x, 244);
    blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, 69, 162, 2, canvas, canvas_w, canvas_h, sb_x, 299);

    // 5 Rows of 3 Build/Action Buttons (y = 71..221, 30px stride per row: 26px 1:1 button + 4px stone border)
    for (int r = 0; r < 5; r++) {
        int dst_y = 71 + r * 30;
        int src_y = 301 + r * 36;
        blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, dst_y, 162, 26, canvas, canvas_w, canvas_h, sb_x, src_y);
        blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, dst_y + 26, 162, 4, canvas, canvas_w, canvas_h, sb_x, src_y + 26);
    }

    // Bottom Roman Stone Relief strip (y = 221..240, 19px 1:1)
    blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 158, 221, 162, 19, canvas, canvas_w, canvas_h, sb_x, 261);

    // Highlight active build category button with a 2px Roman Gold frame when in Modo Construtor
    int active_r = -1, active_c = -1;
    if (get_active_build_button_grid(&active_r, &active_c)) {
        int bx_btn = 158 + 13 + active_c * 50;
        int by_btn = 71 + active_r * 30;
        draw_rect_outline_3ds(bot_fb, N3DS_BOTTOM_WIDTH, bx_btn - 1, by_btn - 1, 41, 28, 0xFFE040FFu);
        draw_rect_outline_3ds(bot_fb, N3DS_BOTTOM_WIDTH, bx_btn - 2, by_btn - 2, 43, 30, 0xD4A017FFu);
    }

    // 2. Left Half Architecture: Fluted Roman Marble Columns + Architrave + Bottom Statue Bar
    draw_roman_column_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 0, 0, 204);
    draw_roman_column_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 150, 0, 204);

    // Top Marble Architrave (x = 8..150, y = 0..4)
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 0, 142, 1, 0xF5EDE0FFu);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 1, 142, 2, 0xD2C8B2FFu);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 3, 142, 1, 0x6E6452FFu);

    // Bottom Carved Roman Statue Relief Bar (x = 0..158, y = 204..240)
    blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 0, 204, 158, 36, canvas, canvas_w, canvas_h, sb_x + 2, 240);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 0, 204, 158, 1, 0xDED4BEFFu);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 0, 239, 158, 1, 0x3A342AFFu);

    // Two Carved Stone Buttons embedded in the middle of the Roman statues: MAPA and MENU
    draw_stone_button_3ds(bot_fb, N3DS_BOTTOM_WIDTH,
        20, 209, 57, 26,
        n3ds_state.left_map_mode ? "CIDADE" : "MAPA",
        n3ds_state.left_map_mode || window_is(WINDOW_OVERLAY_MENU));
    draw_stone_button_3ds(bot_fb, N3DS_BOTTOM_WIDTH,
        81, 209, 57, 26,
        "MENU",
        window_is(WINDOW_TOP_MENU));

    // 3. Left Main Pane (x = 8..150, y = 4..204, 142x200 pixels)
    if (window_is(WINDOW_BUILD_MENU)) {
        int sx, sy, sw, sh;
        get_build_submenu_crop(canvas_w, &sx, &sy, &sw, &sh);
        blit_canvas_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 4, 142, 200, canvas, canvas_w, canvas_h, sx, sy, sw, sh);
    } else if (window_is(WINDOW_OVERLAY_MENU)) {
        int sx, sy, sw, sh;
        get_overlay_submenu_crop(&sx, &sy, &sw, &sh);
        blit_canvas_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 4, 142, 200, canvas, canvas_w, canvas_h, sx, sy, sw, sh);
    } else if (window_is(WINDOW_TOP_MENU)) {
        blit_canvas_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 4, 142, 200, canvas, canvas_w, canvas_h, 0, 0, 284, 200);
    } else if (n3ds_state.left_map_mode) {
        // Minimap + Overlays View inside the Roman Columns
        fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 4, 142, 200, 0x302B24FFu);
        // Overlays button at top (y = 12..45)
        blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 20, 13, 117, 31, canvas, canvas_w, canvas_h, sb_x + 4, 27);
        draw_rect_outline_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 19, 12, 119, 33, 0xC8B486FFu);
        // 1:1 Minimap in center (y = 58..169)
        blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 58, 142, 111, canvas, canvas_w, canvas_h, sb_x + 10, 59);
        draw_rect_outline_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 57, 142, 113, 0xD4AF37FFu);
        // Hint plaque at bottom
        draw_stone_button_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 14, 175, 130, 22, "TOQUE NO MAPA", 0);
    } else {
        // Default: City Construction Viewport (1.0X Native 1:1 or 0.5X Wide Exact 2:1)
        int sx, sy, sw, sh;
        get_bottom_city_viewport_src(canvas_w, &sx, &sy, &sw, &sh);
        if (n3ds_state.bot_zoom == 0) {
            blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 4, 142, 200, canvas, canvas_w, canvas_h, sx, sy);
        } else {
            blit_downscale_2to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 4, 142, 200, canvas, canvas_w, canvas_h, sx, sy);
        }

        // Subtle gold inner border when in Modo Construtor so player clearly sees camera is locked for building
        if (building_construction_type() != BUILDING_NONE) {
            draw_rect_outline_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 4, 142, 200, 0xFFE040FFu);
        }
    }
}

// ============================================================================
// CONTEXT 2 RENDERER: ADVISORS SCREEN (WINDOW_ADVISORS)
// ============================================================================

static void render_advisors_dual_screens(
    uint32_t *top_fb, uint32_t *bot_fb,
    const uint32_t *canvas, int canvas_w, int canvas_h)
{
    // ---------------- TOP SCREEN (400x240) ----------------
    if (n3ds_state.advisor_zoom_1to1) {
        int crop_x = calc_bound(n3ds_state.cam_center_x - N3DS_TOP_WIDTH / 2, 0, canvas_w - N3DS_TOP_WIDTH);
        int crop_y = calc_bound(n3ds_state.cam_center_y - N3DS_TOP_HEIGHT / 2, 0, 432 - N3DS_TOP_HEIGHT);
        blit_1to1_3ds(top_fb, N3DS_TOP_WIDTH,
            0, 0, N3DS_TOP_WIDTH, N3DS_TOP_HEIGHT,
            canvas, canvas_w, canvas_h,
            crop_x, crop_y);
    } else {
        // Full Advisor Report (x = 16..624, y = 0..432) cleanly scaled to 400x240
        blit_canvas_rect_3ds(top_fb, N3DS_TOP_WIDTH,
            0, 0, N3DS_TOP_WIDTH, N3DS_TOP_HEIGHT,
            canvas, canvas_w, canvas_h,
            16, 0, 608, 432);
    }

    // ---------------- BOTTOM SCREEN (320x240) ----------------
    draw_roman_column_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 0, 0, 204);
    draw_roman_column_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 312, 0, 204);

    // Top Marble Architrave
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 0, 304, 1, 0xF5EDE0FFu);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 1, 304, 2, 0xD2C8B2FFu);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 3, 304, 1, 0x6E6452FFu);

    // 1. Interactive Advisor Content Area (x = 8..312, y = 4..114)
    blit_canvas_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH,
        8, 4, 304, 110,
        canvas, canvas_w, canvas_h,
        16, 48, 608, 376);

    // Stone divider at y = 114..116
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 114, 304, 1, 0xDED4BEFFu);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 115, 304, 1, 0x4A443AFFu);

    // 2. 12 Advisor Portraits in 2 Rows of 6 at 1:1 Native 40x40 Resolution (y = 116..204)
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 116, 304, 88, 0x363129FFu);
    int current_adv = window_advisors_get_advisor();
    for (int i = 0; i < 12; i++) {
        int row = i / 6;
        int col = i % 6;
        int dx = 20 + col * 48;
        int dy = (row == 0) ? 118 : 161;
        int sx = 48 * i + 12;
        int sy = 441;

        blit_1to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH,
            dx, dy, 40, 40,
            canvas, canvas_w, canvas_h,
            sx, sy);

        if (current_adv == i + 1) {
            draw_rect_outline_3ds(bot_fb, N3DS_BOTTOM_WIDTH, dx - 1, dy - 1, 42, 42, 0xFFE040FFu);
            draw_rect_outline_3ds(bot_fb, N3DS_BOTTOM_WIDTH, dx - 2, dy - 2, 44, 44, 0xD4A017FFu);
        } else {
            draw_rect_outline_3ds(bot_fb, N3DS_BOTTOM_WIDTH, dx - 1, dy - 1, 42, 42, 0x6E6452FFu);
        }
    }

    // 3. Bottom Roman Bar with "VOLTAR A CIDADE" button (y = 204..240)
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 0, 204, 320, 36, 0x2A251EFFu);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 0, 204, 320, 1, 0xDED4BEFFu);
    draw_stone_button_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 70, 209, 180, 26, "VOLTAR A CIDADE", 0);
    blit_canvas_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 260, 206, 32, 32, canvas, canvas_w, canvas_h, 588, 441, 40, 40);
}

// ============================================================================
// CONTEXT 3 RENDERER: EMPIRE TRADE MAP (WINDOW_EMPIRE)
// ============================================================================

static void render_empire_dual_screens(
    uint32_t *top_fb, uint32_t *bot_fb,
    const uint32_t *canvas, int canvas_w, int canvas_h)
{
    // ---------------- TOP SCREEN (400x240) ----------------
    // Top 160px (y = 0..160): 1:1 Pixel-Perfect Magnifier of the Mediterranean Map
    int crop_x = calc_bound(n3ds_state.cam_center_x - N3DS_TOP_WIDTH / 2, 16, 624 - N3DS_TOP_WIDTH);
    int crop_y = calc_bound(n3ds_state.cam_center_y - 80, 16, 344 - 160);
    blit_1to1_3ds(top_fb, N3DS_TOP_WIDTH,
        0, 0, N3DS_TOP_WIDTH, 160,
        canvas, canvas_w, canvas_h,
        crop_x, crop_y);

    // Bottom 80px (y = 160..240): Selected City Name Scroll & Trade Info Plaque
    blit_canvas_rect_3ds(top_fb, N3DS_TOP_WIDTH,
        0, 160, N3DS_TOP_WIDTH, 80,
        canvas, canvas_w, canvas_h,
        70, 348, 500, 128);
    fill_rect_3ds(top_fb, N3DS_TOP_WIDTH, 0, 160, N3DS_TOP_WIDTH, 1, 0xDED4BEFFu);

    // ---------------- BOTTOM SCREEN (320x240) ----------------
    draw_roman_column_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 0, 0, 168);
    draw_roman_column_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 312, 0, 168);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 0, 304, 2, 0xEFE7D6FFu);
    fill_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH, 8, 2, 304, 2, 0x6E6452FFu);

    // Top 164px (x = 8..312, y = 4..168): Full Mediterranean Map (608x328 -> 304x164 Exact 2:1 Downscale)
    blit_downscale_2to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH,
        8, 4, 304, 164,
        canvas, canvas_w, canvas_h,
        16, 16);

    // Gold viewport box showing Top Screen's 1:1 area
    draw_rect_outline_3ds(bot_fb, N3DS_BOTTOM_WIDTH,
        8 + (crop_x - 16) / 2, 4 + (crop_y - 16) / 2,
        N3DS_TOP_WIDTH / 2, 80, 0xFFE040FFu);

    // Bottom 72px (x = 0..320, y = 168..240): Interactive Trade Controls & Return Buttons
    blit_canvas_rect_3ds(bot_fb, N3DS_BOTTOM_WIDTH,
        0, 168, N3DS_BOTTOM_WIDTH, 72,
        canvas, canvas_w, canvas_h,
        0, 352, 640, 128);
}

// ============================================================================
// CONTEXT 4 RENDERER: MENUS, BRIEFINGS & MODAL DIALOGS
// ============================================================================

static void render_dialog_dual_screens(
    uint32_t *top_fb, uint32_t *bot_fb,
    const uint32_t *canvas, int canvas_w, int canvas_h,
    int mx, int my)
{
    // TOP SCREEN (400x240): 1:1 Magnifier centered around camera/cursor (or full scaled view if top_zoom == 0.5x)
    if (n3ds_state.top_zoom == N3DS_TOP_ZOOM_0_5X) {
        blit_canvas_rect_3ds(top_fb, N3DS_TOP_WIDTH,
            0, 0, N3DS_TOP_WIDTH, N3DS_TOP_HEIGHT,
            canvas, canvas_w, canvas_h,
            0, 0, canvas_w, canvas_h);
        draw_cursor_on_3ds(top_fb, N3DS_TOP_WIDTH, (mx * N3DS_TOP_WIDTH) / canvas_w, (my * N3DS_TOP_HEIGHT) / canvas_h);
    } else {
        int crop_x = calc_bound(n3ds_state.cam_center_x - N3DS_TOP_WIDTH / 2, 0, canvas_w - N3DS_TOP_WIDTH);
        int crop_y = calc_bound(n3ds_state.cam_center_y - N3DS_TOP_HEIGHT / 2, 0, canvas_h - N3DS_TOP_HEIGHT);
        blit_1to1_3ds(top_fb, N3DS_TOP_WIDTH,
            0, 0, N3DS_TOP_WIDTH, N3DS_TOP_HEIGHT,
            canvas, canvas_w, canvas_h,
            crop_x, crop_y);
        draw_cursor_on_3ds(top_fb, N3DS_TOP_WIDTH, mx - crop_x, my - crop_y);
    }

    // BOTTOM SCREEN (320x240): Full 640x480 Overview with Exact 2:1 Downscale
    blit_downscale_2to1_3ds(bot_fb, N3DS_BOTTOM_WIDTH,
        0, 0, N3DS_BOTTOM_WIDTH, N3DS_BOTTOM_HEIGHT,
        canvas, canvas_w, canvas_h,
        0, 0);
    draw_cursor_on_3ds(bot_fb, N3DS_BOTTOM_WIDTH, mx / 2, my / 2);
}

// ============================================================================
// MAIN DUAL-SCREEN ENTRY POINT
// ============================================================================

void n3ds_render_dual_screens(const uint32_t *canvas, int canvas_w, int canvas_h)
{
    if (!canvas || canvas_w <= 0 || canvas_h <= 0) {
        return;
    }

    u16 top_fb_w = 0, top_fb_h = 0;
    u16 bot_fb_w = 0, bot_fb_h = 0;
    uint32_t *top_fb = (uint32_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &top_fb_w, &top_fb_h);
    uint32_t *bot_fb = (uint32_t *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &bot_fb_w, &bot_fb_h);
    if (!top_fb || !bot_fb) {
        return;
    }

    const mouse *m = mouse_get();
    int mx = calc_bound(m->x, 0, canvas_w - 1);
    int my = calc_bound(m->y, 0, canvas_h - 1);

    if (is_city_context() && canvas_w >= 640 && canvas_h >= 480) {
        render_city_dual_screens(top_fb, bot_fb, canvas, canvas_w, canvas_h);
    } else if (window_is(WINDOW_ADVISORS) && canvas_w >= 640 && canvas_h >= 480) {
        render_advisors_dual_screens(top_fb, bot_fb, canvas, canvas_w, canvas_h);
    } else if (window_is(WINDOW_EMPIRE) && canvas_w >= 640 && canvas_h >= 480) {
        render_empire_dual_screens(top_fb, bot_fb, canvas, canvas_w, canvas_h);
    } else {
        render_dialog_dual_screens(top_fb, bot_fb, canvas, canvas_w, canvas_h, mx, my);
    }

    GSPGPU_FlushDataCache(top_fb, N3DS_TOP_WIDTH * N3DS_TOP_HEIGHT * 4);
    GSPGPU_FlushDataCache(bot_fb, N3DS_BOTTOM_WIDTH * N3DS_BOTTOM_HEIGHT * 4);
    gfxScreenSwapBuffers(GFX_TOP, false);
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
}

#endif // __3DS__
