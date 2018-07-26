/*
 * lws-minimal-ws-client-pmd-bulk
 *
 * Copyright (C) 2018 Andy Green <andy@warmcat.com>
 *
 * This file is made available under the Creative Commons CC0 1.0
 * Universal Public Domain Dedication.
 *
 * This demonstrates a ws client that sends bulk data in multiple
 * ws fragments, in a way compatible with per-message deflate.
 *
 * It shows how to send huge messages without needing a lot of memory.
 * 
 * Build and start the minimal-examples/ws-server/minmal-ws-server-pmd-bulk
 * example first.  Running this sends a large message to the server and
 * exits.
 *
 * If you give both sides the -n commandline option, it disables permessage-
 * deflate compression extension.
 */

#include <libwebsockets.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

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

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    /* deal with your own user events here first */

    return lws_esp32_event_passthru(ctx, event);
}

void lws_esp32_leds_timer_cb(TimerHandle_t th)
{
}

void app_main()
{
    struct lws_context_creation_info info;
    struct lws_context *context;
    struct lws_vhost *vh;
    nvs_handle nvh;
    int n = 0, logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE
        /* for LLL_ verbosity above NOTICE to be built into lws,
			 * lws must have been configured and built with
			 * -DCMAKE_BUILD_TYPE=DEBUG instead of =RELEASE */
        /* | LLL_INFO */ /* | LLL_PARSER */ /* | LLL_HEADER */
        /* | LLL_EXT */ /* | LLL_CLIENT */  /* | LLL_LATENCY */
        /* | LLL_DEBUG */;

    // Initialize NVS (non-volatile storage = sd card).
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (!nvs_open("esp-lws", NVS_READWRITE, &nvh))
    {
        nvs_set_str(nvh, "ssid", "ssid");
        nvs_set_str(nvh, "password", "password");
        nvs_commit(nvh);
        nvs_close(nvh);
    }

    lws_esp32_wlan_config();

    lws_set_log_level(logs, NULL);
    lwsl_user("LWS minimal ws client + permessage-deflate + multifragment bulk message\n");
    lwsl_user("   needs minimal-ws-server-pmd-bulk running to communicate with\n");

    memset(&info, 0, sizeof info); /* otherwise uninitialized garbage */
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.pvo = &pvo;
    info.extensions = extensions;
    info.pt_serv_buf_size = 32 * 1024;

    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    lws_esp32_wlan_start_station();
    context = lws_esp32_init(&info, &vh);

    while (!lws_service(context, 50))
        taskYIELD();
}
