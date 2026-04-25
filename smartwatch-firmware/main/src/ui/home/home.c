/**
 * @file home.c
 * @brief Home screen tab – wallpaper, battery, and clock widgets.
 *
 * Assembles the home screen with a scaled wallpaper background,
 * a semi-transparent battery status overlay (top-left), and
 * a vertical RTC clock overlay (bottom-left).
 */

#include "include/ui/home/home.h"
#include "include/ui/home/battery.h"
#include "include/ui/home/rtc_time.h"
#include "include/init/touch_init.h"
#include "assets/wallpaper.h"

void home(lv_obj_t *tab)
{
    printf("Home screen initialized\n");

    lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_border_width(tab, 0, 0);

    /* ── Wallpaper Background ── */
    lv_obj_t *img = lv_img_create(tab);
    lv_img_set_src(img, &wallpaper);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_zoom(img, 300);

    /* ── Battery Widget (Top-Left) ── */
    lv_obj_t *bat_cont = lv_obj_create(tab);
    lv_obj_set_size(bat_cont, 85, 30);
    lv_obj_align(bat_cont, LV_ALIGN_TOP_LEFT, 15, 0);
    lv_obj_set_style_bg_color(bat_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bat_cont, 150, 0);
    lv_obj_set_style_border_width(bat_cont, 0, 0);
    lv_obj_set_style_radius(bat_cont, 8, 0);
    lv_obj_set_style_pad_all(bat_cont, 0, 0);

    lv_obj_t *bat_label = lv_label_create(bat_cont);
    lv_label_set_text(bat_label, "--V --%");
    lv_obj_set_style_text_font(bat_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bat_label, lv_palette_main(LV_PALETTE_LIGHT_GREEN), 0);
    lv_obj_center(bat_label);

    battery_init();
    battery_set_label(bat_label);
    xTaskCreate(battery_task, "battery_task", 2048, NULL, 5, NULL);

    /* ── Clock Widget (Bottom-Left) ── */
    lv_obj_t *time_cont = lv_obj_create(tab);
    lv_obj_set_size(time_cont, 55, 95);
    lv_obj_align(time_cont, LV_ALIGN_BOTTOM_LEFT, 10, 0);
    lv_obj_set_style_bg_color(time_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(time_cont, 150, 0);
    lv_obj_set_style_border_width(time_cont, 0, 0);
    lv_obj_set_style_radius(time_cont, 8, 0);
    lv_obj_set_style_pad_all(time_cont, 0, 0);

    lv_obj_t *time_lbl = lv_label_create(time_cont);
    lv_label_set_text(time_lbl, "--\n--\n--");
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(time_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(time_lbl, -4, 0);
    lv_obj_center(time_lbl);

    rtc_time_init(app_get_i2c_bus());
    rtc_set_label(time_lbl);
    xTaskCreate(rtc_time_task, "rtc_task", 2048, NULL, 5, NULL);
}