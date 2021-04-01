#include <stdio.h>
#include "esp_log.h"
#include "iot_button.h"

#define TAG "BOARD"

// extern void btn_click_a();
extern void btn_click_b();
// extern void btn_click_menu();
// extern void btn_click_volume();
// extern void btn_click_select();
// extern void btn_click_start();

static void board_button_init(void)
{
    // iot_button_set_evt_cb(iot_button_create(32, 0), BUTTON_CB_RELEASE, btn_click_a, "RELEASE");
    iot_button_set_evt_cb(iot_button_create(33, 0), BUTTON_CB_RELEASE, btn_click_b, "RELEASE");
    // iot_button_set_evt_cb(iot_button_create(13, 0), BUTTON_CB_RELEASE, btn_click_menu, "RELEASE");
    // iot_button_set_evt_cb(iot_button_create(0, 0), BUTTON_CB_RELEASE, btn_click_volume, "RELEASE");
    // iot_button_set_evt_cb(iot_button_create(27, 0), BUTTON_CB_RELEASE, btn_click_select, "RELEASE");
    // iot_button_set_evt_cb(iot_button_create(39, 0), BUTTON_CB_RELEASE, btn_click_start, "RELEASE");
}

void board_init(void)
{
    board_button_init();
}
