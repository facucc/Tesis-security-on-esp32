#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <string.h>

#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_camera.h"
#include "camera_pin.h"

#include "mqtt_common.h"

int framesize;

#if CONFIG_FRAMESIZE_VGA
    int framesize = FRAMESIZE_VGA;
    #define FRAMESIZE_STRING "640x480"
#elif CONFIG_FRAMESIZE_SVGA
    int framesize = FRAMESIZE_SVGA;
    #define FRAMESIZE_STRING "800x600"
#elif CONFIG_FRAMESIZE_XGA
    int framesize = FRAMESIZE_XGA;
    #define FRAMESIZE_STRING "1024x768"
#elif CONFIG_FRAMESIZE_HD
    int framesize = FRAMESIZE_HD;
    #define FRAMESIZE_STRING "1280x720"
#elif CONFIG_FRAMESIZE_SXGA
    int framesize = FRAMESIZE_SXGA;
    #define FRAMESIZE_STRING "1280x1024"
#elif CONFIG_FRAMESIZE_UXGA
    int framesize = FRAMESIZE_UXGA;
    #define FRAMESIZE_STRING "1600x1200"
#endif

#define GET_BASE64_BUFFER_SIZE(n) ((((n) + 2) / 3) * 4 + 1)

#define IMAGES_UPLOAD_TOPIC "$aws/rules/UploadImages"
#define IMAGES_UPLOAD_TOPIC_LENGTH strlen(IMAGES_UPLOAD_TOPIC)

#define DELAY 300000

camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, /* YUV422,GRAYSCALE,RGB565,JPEG */
    .frame_size = FRAMESIZE_VGA,    /* QQVGA-UXGA Do not use sizes above QVGA when not JPEG */

    .jpeg_quality = 12, /* 0-63 lower number means higher quality. */
    .fb_count = 1       /* if more than one, i2s runs in continuous mode. Use only with JPEG */
};

static const char* TAG = "APP";

/* Only Debug to detect stack size */
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    static UBaseType_t uxHighWaterMark;
#endif

static esp_err_t prInitCamera(int framesize);
static void prSendPictureToAWS(const char* pictureEncoded, size_t pictureEncodedLength);
static esp_err_t camera_capture();

void applicationTask(void* parameters)
{
#if defined(CONFIG_ENABLE_STACK_WATERMARK)
    uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
#endif
    if (ESP_OK != prInitCamera(framesize)) {
        return;
    }

    while (true) {
        ESP_LOGI(TAG, "Taking picture...");

        camera_capture();

#if defined(CONFIG_ENABLE_STACK_WATERMARK)
        uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI(TAG, "HIGH WATER MARK %d\n", uxHighWaterMark);
#endif
        vTaskDelay(pdMS_TO_TICKS(DELAY));
    }

}

/* Initializes the camera with the given frame size. */
static esp_err_t prInitCamera(int framesize)
{
    camera_config.frame_size = framesize;
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}

/* Captures an image, encodes it in Base64, and sends it to AWS IoT. */
static esp_err_t camera_capture()
{
    char* dest = NULL;
    size_t destLength = 0;
    size_t outlen;

    for (int i = 0; i < 1; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        ESP_LOGI(TAG, "fb->len=%d", fb->len);
        esp_camera_fb_return(fb);
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera Capture Failed");
        return ESP_FAIL;
    }

    destLength = GET_BASE64_BUFFER_SIZE(fb->len);

    dest = (char*) calloc(destLength, sizeof(char));

    ESP_LOGI(TAG, "Buffer size %d", destLength);

    assert(dest != NULL);

    if (mbedtls_base64_encode((unsigned char*)dest, destLength, &outlen, fb->buf, fb->len) == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        ESP_LOGE(TAG, "Buffer too small");
        return ESP_FAIL;
    } else {
        /* ESP_LOGI(TAG, "\nDatos encoded: %d \nn", outlen); */
    }

    prSendPictureToAWS(dest, destLength);

    /* return the frame buffer back to the driver for reuse */
    esp_camera_fb_return(fb);
    free(dest);

    return ESP_OK;
}

/* Encodes the image in a JSON object and publishes it to the AWS IoT topic. */
static void prSendPictureToAWS(const char* pictureEncoded, size_t pictureEncodedLength)
{
    char* buffer = (char*) calloc(pictureEncodedLength + 30, sizeof(char));

    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed malloc");
        return;
    }

    snprintf(buffer,
             pictureEncodedLength + 30,
             "{"
             "\"image\": \"%s\""
             "}",
             pictureEncoded);

    PublishToTopic(IMAGES_UPLOAD_TOPIC,
                   IMAGES_UPLOAD_TOPIC_LENGTH,
                   buffer,
                   strlen(buffer),
                   MQTTQoS0,
                   TAG);

    free(buffer);
}