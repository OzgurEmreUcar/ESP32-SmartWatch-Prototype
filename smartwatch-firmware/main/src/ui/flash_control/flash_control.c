/**
 * @file flash_control.c
 * @brief Flash Control tab – remote flash toggle UI.
 */

#include "include/ui/flash_control/flash_control.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"

static const char *TAG = "FLASH_UI";
#define ESP32_CAM_IP "192.168.1.22"
static esp_http_client_handle_t s_http_client = NULL;

static lv_obj_t *s_flash_btn   = NULL;
static lv_obj_t *s_flash_label = NULL;

static void device_control_set_flash(bool state)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s/flash/%s", ESP32_CAM_IP, state ? "on" : "off");

    if (s_http_client == NULL) {
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 2000,
            .keep_alive_enable = true,
        };
        s_http_client = esp_http_client_init(&config);
        if (s_http_client == NULL) {
            ESP_LOGE(TAG, "Failed to initialize HTTP client");
            return;
        }
    } else {
        esp_http_client_set_url(s_http_client, url);
        esp_http_client_set_method(s_http_client, HTTP_METHOD_GET);
    }

    ESP_LOGI(TAG, "Sending HTTP GET %s", url);
    esp_err_t err = esp_http_client_perform(s_http_client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d", esp_http_client_get_status_code(s_http_client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
    }
}

static void flash_toggle_cb(lv_event_t *e)
{
    lv_obj_t *btn  = lv_event_get_target(e);

    /* Ignore if the touch turned into a swipe gesture */
    lv_indev_t *indev = lv_indev_get_act();
    if (indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE) return;

    /* Manually toggle the checked state */
    bool was_on = lv_obj_has_state(btn, LV_STATE_CHECKED);

    if (was_on) {
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
        device_control_set_flash(false);
        lv_label_set_text(s_flash_label, LV_SYMBOL_CLOSE "  Flash: OFF");
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);
    } else {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
        device_control_set_flash(true);
        lv_label_set_text(s_flash_label, LV_SYMBOL_OK "  Flash: ON");
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_AMBER), 0);
    }
}

void flash_control(lv_obj_t *tab)
{
    printf("Flash Control tab initialised\n");

    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START,
                               LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(tab, 20, 0);
    lv_obj_set_style_pad_ver(tab, 30, 0);

    lv_obj_t *title = lv_label_create(tab);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Flash Control");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    lv_obj_t *hint = lv_label_create(tab);
    lv_label_set_text(hint, "ESP32-CAM Flash");
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);

    s_flash_btn = lv_btn_create(tab);
    lv_obj_set_size(s_flash_btn, 180, 50);
    lv_obj_add_event_cb(s_flash_btn, flash_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(s_flash_btn, lv_palette_main(LV_PALETTE_GREY), 0);

    s_flash_label = lv_label_create(s_flash_btn);
    lv_label_set_text(s_flash_label, LV_SYMBOL_CLOSE "  Flash: OFF");
    lv_obj_center(s_flash_label);
}
