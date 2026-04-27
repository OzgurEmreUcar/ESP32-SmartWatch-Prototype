/**
 * @file camera_init.c
 * @brief OV2640 camera initialization for the AI-Thinker ESP32-CAM board.
 *
 * Configures the camera peripheral with the AI-Thinker GPIO pinout, sets
 * JPEG capture parameters, and applies default image-quality sensor tuning.
 */

#include "include/camera_init.h"
#include "esp_log.h"

/* ── AI-Thinker ESP32-CAM GPIO Pin Map ──────────────────────────────────── */

/** @name Camera Data Bus Pins (D0–D7) */
/** @{ */
#define Y2_GPIO_NUM        5   /**< Data bit 0 */
#define Y3_GPIO_NUM       18   /**< Data bit 1 */
#define Y4_GPIO_NUM       19   /**< Data bit 2 */
#define Y5_GPIO_NUM       21   /**< Data bit 3 */
#define Y6_GPIO_NUM       36   /**< Data bit 4 */
#define Y7_GPIO_NUM       39   /**< Data bit 5 */
#define Y8_GPIO_NUM       34   /**< Data bit 6 */
#define Y9_GPIO_NUM       35   /**< Data bit 7 */
/** @} */

/** @name Camera Control & Sync Pins */
/** @{ */
#define PWDN_GPIO_NUM     32   /**< Power-down control      */
#define RESET_GPIO_NUM    -1   /**< Hardware reset (unused)  */
#define XCLK_GPIO_NUM      0   /**< External clock input     */
#define SIOD_GPIO_NUM     26   /**< SCCB data  (I²C SDA)    */
#define SIOC_GPIO_NUM     27   /**< SCCB clock (I²C SCL)    */
#define VSYNC_GPIO_NUM    25   /**< Vertical sync            */
#define HREF_GPIO_NUM     23   /**< Horizontal reference     */
#define PCLK_GPIO_NUM     22   /**< Pixel clock              */
/** @} */

/**
 * @brief Initialize the OV2640 camera sensor.
 *
 * Configures the camera driver with JPEG output at 240×240 resolution,
 * dual frame buffers stored in PSRAM, and a 20 MHz external clock.
 * After successful initialization, default sensor parameters (brightness,
 * contrast, white balance, auto-exposure) are applied.
 *
 * @return ESP_OK on success, or an error code from esp_camera_init().
 */
esp_err_t camera_init(void) {
    camera_config_t config = {
        .pin_pwdn       = PWDN_GPIO_NUM,
        .pin_reset      = RESET_GPIO_NUM,
        .pin_xclk       = XCLK_GPIO_NUM,
        .pin_sscb_sda   = SIOD_GPIO_NUM,
        .pin_sscb_scl   = SIOC_GPIO_NUM,
        .pin_d7         = Y9_GPIO_NUM,
        .pin_d6         = Y8_GPIO_NUM,
        .pin_d5         = Y7_GPIO_NUM,
        .pin_d4         = Y6_GPIO_NUM,
        .pin_d3         = Y5_GPIO_NUM,
        .pin_d2         = Y4_GPIO_NUM,
        .pin_d1         = Y3_GPIO_NUM,
        .pin_d0         = Y2_GPIO_NUM,
        .pin_vsync      = VSYNC_GPIO_NUM,
        .pin_href       = HREF_GPIO_NUM,
        .pin_pclk       = PCLK_GPIO_NUM,
        .xclk_freq_hz   = 20000000,
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,
        .pixel_format   = PIXFORMAT_JPEG,
        .frame_size     = FRAMESIZE_240X240,
        .jpeg_quality   = 20,
        .fb_count       = 2,
        .fb_location    = CAMERA_FB_IN_PSRAM,
        .grab_mode      = CAMERA_GRAB_LATEST
    };

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) return err;

    /* Apply default sensor tuning for balanced image quality. */
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 0);
        s->set_hmirror(s, 0);
        s->set_brightness(s, 1);
        s->set_contrast(s, 1);
        s->set_saturation(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
    }
    return ESP_OK;
}
