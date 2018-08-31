/*
 * ESP32 "Factory" WLAN Config + Factory Setup app
 *
 * Copyright (C) 2017 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */
#include <libwebsockets.h>
#include <nvs_flash.h>
#include "soc/ledc_reg.h"
#include "driver/ledc.h"

static struct lws_context *context;
static int id_flashes;

#define LWS_PLUGIN_STATIC
#include "protocol_lws_minimal_pmd_bulk.c"

static struct lws_protocols protocols[] = {
    {"http", lws_callback_http_dummy, 0, 0},
    LWS_PLUGIN_PROTOCOL_MINIMAL_PMD_BULK,
    {NULL, NULL, 0, 0} /* terminator */
};

static int interrupted, options;

/* pass pointers to shared vars to the protocol */

static const struct lws_protocol_vhost_options pvo_options = {
    NULL,
    NULL,
    "options",       /* pvo name */
    (void *)&options /* pvo value */
};

static const struct lws_protocol_vhost_options pvo_interrupted = {
    &pvo_options,
    NULL,
    "interrupted",       /* pvo name */
    (void *)&interrupted /* pvo value */
};

static const struct lws_protocol_vhost_options pvo = {
    NULL,                   /* "next" pvo linked-list */
    &pvo_interrupted,       /* "child" pvo linked-list */
    "lws-minimal-pmd-bulk", /* protocol name we belong to on this vhost */
    ""                      /* ignored */
};
static const struct lws_extension extensions[] = {
    {"permessage-deflate",
     lws_extension_callback_pm_deflate,
     "permessage-deflate"
     "; client_no_context_takeover"
     "; client_max_window_bits"},
    {NULL, NULL, NULL /* terminator */}};

void lws_esp32_leds_timer_cb(TimerHandle_t th)
{
    struct timeval t;
    unsigned long r;
    int div = 3 - (2 * !!lws_esp32.inet);
    int base = 4096 * !lws_esp32.inet;

    gettimeofday(&t, NULL);
    r = ((t.tv_sec * 1000000) + t.tv_usec);

    if (!id_flashes)
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0,
                      base + (lws_esp32_sine_interp(r / (1699 - (500 * !lws_esp32.inet))) / div));
    else
        ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, lws_esp32_sine_interp(r / 333));

    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

    if (id_flashes)
    {
        id_flashes++;
        if (id_flashes == 500)
            id_flashes = 0;
    }
}

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    return lws_esp32_event_passthru(ctx, event);
}

#define GPIO_ID 23

/*
 * This is called when the user asks to "Identify physical device"
 * he is configuring, by pressing the Identify button on the AP
 * setup page for the device.
 *
 * It should do something device-specific that
 * makes it easy to identify which physical device is being
 * addressed, like flash an LED on the device on a timer for a
 * few seconds.
 */
void lws_esp32_identify_physical_device(void)
{
    lwsl_notice("%s\n", __func__);

    id_flashes = 1;
}

void lws_esp32_button(int down)
{
    lwsl_notice("button %d\n", down);
    if (!context)
        return;
}

void app_main(void)
{
    static struct lws_context_creation_info info;
    nvs_handle nvh;
    struct lws_vhost *vh;
    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = 8191,
        .gpio_num = GPIO_ID,
        .intr_type = LEDC_INTR_FADE_END,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0,
    };

    // Initialize NVS (non-volatile storage = sd card).
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (!nvs_open("lws-station", NVS_READWRITE, &nvh))
    {
        nvs_set_str(nvh, "0ssid", "ssid");
        nvs_set_str(nvh, "0password", "password");
        nvs_commit(nvh);
        nvs_close(nvh);
    }

    ledc_channel_config(&ledc_channel);

    lws_esp32_set_creation_defaults(&info);

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.pvo = &pvo;
    info.extensions = extensions;
    info.pt_serv_buf_size = 32 * 1024;

    lws_esp32_wlan_config();

    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    lws_esp32_wlan_start_station();
    /* this configures the LED timer channel 0 and starts the fading cb */
    context = lws_esp32_init(&info, &vh);

    while (!lws_service(context, 10))
        taskYIELD();
}
