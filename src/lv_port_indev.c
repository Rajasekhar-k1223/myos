#include "lv_port_indev.h"
#include "lvgl.h"
#include <stdbool.h>

#include "keyboard.h"

static void mouse_init(void);
static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static bool mouse_is_pressed(void);
static void mouse_get_xy(lv_coord_t * x, lv_coord_t * y);

static void keypad_init(void);
static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

lv_indev_t * indev_mouse;
lv_indev_t * indev_keypad;

extern int32_t mouse_get_x(void);
extern int32_t mouse_get_y(void);
extern uint8_t mouse_get_buttons(void);

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    mouse_init();
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read;
    indev_mouse = lv_indev_drv_register(&indev_drv);

    static lv_indev_drv_t indev_keypad_drv;
    keypad_init();
    lv_indev_drv_init(&indev_keypad_drv);
    indev_keypad_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_keypad_drv.read_cb = keypad_read;
    indev_keypad = lv_indev_drv_register(&indev_keypad_drv);
}

static void mouse_init(void) {}

static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    if(mouse_is_pressed()) {
        mouse_get_xy(&last_x, &last_y);
        data->state = LV_INDEV_STATE_PR;
    } else {
        mouse_get_xy(&last_x, &last_y);
        data->state = LV_INDEV_STATE_REL;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

static bool mouse_is_pressed(void)
{
    return (mouse_get_buttons() & 1) != 0;
}

static void mouse_get_xy(lv_coord_t * x, lv_coord_t * y)
{
    *x = (lv_coord_t)mouse_get_x();
    *y = (lv_coord_t)mouse_get_y();
}

static void keypad_init(void) {}

static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    extern char keyboard_poll_char(void);
    static uint32_t last_key = 0;
    
    char c = keyboard_poll_char();
    if (c != 0) {
        uint32_t lk = c;
        if (c == '\n' || c == '\r') lk = 10; // LV_KEY_ENTER
        else if (c == '\b') lk = 8; // LV_KEY_BACKSPACE
        else if (c == '\t') lk = 9; // LV_KEY_NEXT
        else if (c == 27) lk = 27; // LV_KEY_ESC
        else if (c == '\x10') lk = 17; // LV_KEY_UP
        else if (c == '\x11') lk = 18; // LV_KEY_DOWN
        else if (c == '\x12') lk = 20; // LV_KEY_LEFT
        else if (c == '\x13') lk = 19; // LV_KEY_RIGHT
        
        last_key = lk;
        data->key = last_key;
        data->state = LV_INDEV_STATE_PR;
        data->continue_reading = true; // Tell LVGL there might be more keys
    } else {
        data->key = last_key;
        data->state = LV_INDEV_STATE_REL;
        data->continue_reading = false;
    }
}
