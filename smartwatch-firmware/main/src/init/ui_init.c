/**
 * @file ui_init.c
 * @brief Top-level UI construction and tab navigation.
 *
 * Creates an LVGL tabview with hidden tab buttons (navigation is
 * handled entirely by swipe gestures) and populates each tab with
 * its dedicated screen builder.
 */

#include "include/init/ui_init.h"
#include "include/ui/home/home.h"
#include "include/ui/settings/settings.h"
#include "include/ui/imu_dashboard/imu_dashboard.h"
#include "include/ui/remote_graph/remote_graph.h"
#include "include/ui/flash_control/flash_control.h"
#include "include/ui/camera_stream/camera_stream.h"

/* ═══════════════════════════════════════════════════════════════
 *  UI Construction
 * ═══════════════════════════════════════════════════════════════ */

void app_ui_init(void)
{
    lvgl_port_lock(0);

    /* Root screen styling */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Tabview container */
    lv_obj_t *tabview = lv_tabview_create(scr);
    lv_obj_set_size(tabview, 240, 280);
    lv_obj_center(tabview);

    /* Remove scrolling and padding from content area */
    lv_obj_t *content = lv_tabview_get_content(tabview);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_gap(content, 0, 0);

    /* Hide built-in tab buttons (we navigate by swipe only) */
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(tab_btns, 0);

    /* Create individual tabs (order defines swipe sequence) */
    lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "IMU Data");
    lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Graph");
    lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "Flash");
    lv_obj_t *tab4 = lv_tabview_add_tab(tabview, "Camera");
    lv_obj_t *tab5 = lv_tabview_add_tab(tabview, "Home");
    lv_obj_t *tab6 = lv_tabview_add_tab(tabview, "Settings");

    /* Populate each tab */
    imu_dashboard(tab1);
    remote_graph_tab(tab2, tabview);
    flash_control(tab3);
    camera_stream_tab(tab4);
    home(tab5);
    settings(tab6);

    /* Register swipe gesture handlers for tab navigation */
    lv_obj_add_event_cb(scr, swipe_event_cb, LV_EVENT_GESTURE, tabview);
    lv_obj_add_event_cb(lv_tabview_get_content(tabview), swipe_event_cb, LV_EVENT_GESTURE, tabview);

    /* Default to the Home tab on startup */
    lv_tabview_set_act(tabview, 4, LV_ANIM_OFF);

    lvgl_port_unlock();
}

/* ═══════════════════════════════════════════════════════════════
 *  Global Swipe Gesture Handler
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief Process left/right swipe gestures to switch tabs.
 *
 * Tab switching is blocked while the camera stream is active
 * to prevent accidental navigation during live video.
 */
void swipe_event_cb(lv_event_t *e)
{
    lv_obj_t  *tabview = lv_event_get_user_data(e);
    lv_dir_t   dir     = lv_indev_get_gesture_dir(lv_indev_get_act());
    uint16_t   act_tab = lv_tabview_get_tab_act(tabview);

    /* Block navigation during active camera stream */
    if (camera_stream_is_active()) return;

    if (dir == LV_DIR_LEFT) {
        if (act_tab < 5) lv_tabview_set_act(tabview, act_tab + 1, LV_ANIM_OFF);
    } else if (dir == LV_DIR_RIGHT) {
        if (act_tab > 0) lv_tabview_set_act(tabview, act_tab - 1, LV_ANIM_OFF);
    }
}