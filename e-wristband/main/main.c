#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_bt.h"
#include "esp_timer.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"
#include "board.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "common.h"
#include "libcoap.h"
#include "coap_dtls.h"
#include "coap.h"
#include "protocol_examples_common.h"
#include "esp_sntp.h"

#define BUFSIZE 40

#define TAG "MAIN"

#define CID_ESP             0x02E5

#define MSG_SEND_TTL        3
#define MSG_SEND_REL        false
#define MSG_TIMEOUT         4000
#define MSG_ROLE            ROLE_NODE

#define ESP_BLE_MESH_VND_MODEL_ID_CLIENT    0x0000
#define ESP_BLE_MESH_VND_MODEL_ID_SERVER    0x0001

#define OP_REQ           ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)
#define OP_RES           ESP_BLE_MESH_MODEL_OP_3(0x02, CID_ESP)

time_t now;
struct tm timeinfo;
struct timeval tv_now;

char str[30];

static coap_optlist_t *optlist = NULL;
uint16_t tx_mid = 0;

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN];

static struct info_store {
    uint16_t net_idx;   /* NetKey Index */
    uint16_t app_idx;   /* AppKey Index */
    uint8_t  onoff;     /* Remote OnOff */
    uint16_t  tid;       /* Message TID */
} store = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .onoff = 0x0,
    .tid = 0,
};

static nvs_handle_t NVS_HANDLE;
static const char * NVS_KEY = "vendor_client";

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .beacon = ESP_BLE_MESH_BEACON_DISABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
    .default_ttl = 7,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

static const esp_ble_mesh_client_op_pair_t vnd_op_pair[] = {
};

static esp_ble_mesh_client_t vendor_client = {
    .op_pair_size = ARRAY_SIZE(vnd_op_pair),
    .op_pair = vnd_op_pair,
};

// 有人傳東西給 device，這是他允許接收的指令
static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(OP_REQ, 2),
    ESP_BLE_MESH_MODEL_OP(OP_RES, 2),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, ESP_BLE_MESH_VND_MODEL_ID_CLIENT,
    vnd_op, NULL, &vendor_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
};

static void mesh_info_store(void)
{
    ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &store, sizeof(store));
}

static void mesh_info_restore(void)
{
    esp_err_t err = ESP_OK;
    bool exist = false;

    err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &store, sizeof(store), &exist);
    if (err != ESP_OK) {
        return;
    }

    if (exist) {
        ESP_LOGI(TAG, "Restore, net_idx 0x%04x, app_idx 0x%04x, onoff %u, tid 0x%04x",
            store.net_idx, store.app_idx, store.onoff, store.tid);
    }
}

uint16_t myaddr = 0x0001;

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG, "!!! prov_complete !!!");
    ESP_LOGI(TAG, "net_idx: 0x%04x, addr: 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08x", flags, iv_index);

    myaddr = addr;
    store.net_idx = net_idx;

    // 不同的 node 有不同的顏色或者文字顯示在 LCD
    if (myaddr == 0x000d) {
        // FillTestRed(&dev, CONFIG_WIDTH, CONFIG_HEIGHT);
        logger("prov_complete!!!", RED);
    } else if (myaddr == 0x000e) {
        // FillTestGreen(&dev, CONFIG_WIDTH, CONFIG_HEIGHT);
    } else {
        logger("prov_complete!!!", PURPLE);
    }
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                 esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        mesh_info_restore();
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
            param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
            param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
        prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
            param->node_prov_complete.flags, param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    default:
        break;
    }
}

static void ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);
            if (param->value.state_change.mod_app_bind.company_id == CID_ESP &&
                param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_VND_MODEL_ID_CLIENT) {
                store.app_idx = param->value.state_change.mod_app_bind.app_idx;
                mesh_info_store();
            }
            break;
        default:
            break;
        }
    }
}

uint8_t action = 0;
uint16_t count = 0;
uint8_t retry = 0;

esp_ble_mesh_msg_ctx_t ctx = {0};
uint32_t opcode;
esp_err_t err;
static coap_uri_t uri;
const char *server_uri = "coap://localhost";

unsigned char _buf[BUFSIZE];
unsigned char *buf;
size_t buflen;
int res;
coap_pdu_t *request = NULL;

void btn_click_b()
{
    // while (1)
    // {
        coap_set_log_level(CONFIG_COAP_LOG_DEFAULT_LEVEL);

        optlist = NULL;

        if (coap_split_uri((const uint8_t *)server_uri, strlen(server_uri), &uri) == -1) {
            ESP_LOGE(TAG, "CoAP server uri error");
            return;
        }

        if (uri.path.length) {
            buflen = BUFSIZE;
            buf = _buf;
            res = coap_split_path(uri.path.s, uri.path.length, buf, &buflen);

            while (res--) {
                coap_insert_optlist(&optlist,
                                    coap_new_optlist(COAP_OPTION_URI_PATH,
                                                        coap_opt_length(buf),
                                                        coap_opt_value(buf)));

                buf += coap_opt_size(buf);
            }
        }

        if (uri.query.length) {
            buflen = BUFSIZE;
            buf = _buf;
            res = coap_split_query(uri.query.s, uri.query.length, buf, &buflen);

            while (res--) {
                coap_insert_optlist(&optlist,
                                    coap_new_optlist(COAP_OPTION_URI_QUERY,
                                                        coap_opt_length(buf),
                                                        coap_opt_value(buf)));

                buf += coap_opt_size(buf);
            }
        }

        if (tx_mid == 0) {
            prng((unsigned char *)&tx_mid, sizeof(tx_mid));
        }
        
        request = coap_pdu_init(0, 0, 0, 1152 - 4);
        request->type = COAP_MESSAGE_NON;
        request->tid = ++tx_mid;
        request->code = COAP_REQUEST_GET;

        // uint8_t token[1];
        // prng(token, 1);
        uint8_t token[3];
        count++;
        token[0] = (uint8_t)(count >> 8);
        token[1] = (uint8_t)(count);
        token[2] = 0;
        coap_add_token(request, 3, token);

        coap_add_optlist_pdu(request, &optlist);

        gettimeofday(&tv_now, NULL);
        int64_t cpu_time = (int64_t)tv_now.tv_sec * 1000L + (int64_t)tv_now.tv_usec / 1000 - 1617120000000;
        uint32_t t = (uint32_t) cpu_time;

        ESP_LOGI(TAG, "The current time is:");
        ESP_LOGI(TAG, "%" PRIu32 "", t);

        uint8_t payload[8];
        payload[0] = (uint8_t)(t >> 24);
        payload[1] = (uint8_t)(t >> 16);
        payload[2] = (uint8_t)(t >> 8);
        payload[3] = (uint8_t)(t);
        payload[4] = (uint8_t)(t >> 24);
        payload[5] = (uint8_t)(t >> 16);
        payload[6] = (uint8_t)(t >> 8);
        payload[7] = (uint8_t)(t);
        coap_add_data(request, 8, payload);

        coap_pdu_encode_header(request, COAP_PROTO_UDP);

        bool need_rsp = false;

        // 準備 msg
        ctx.net_idx = store.net_idx;
        ctx.app_idx = store.app_idx;
        ctx.addr = 0xffff;
        ctx.send_ttl = MSG_SEND_TTL;
        ctx.send_rel = MSG_SEND_REL;
        opcode = OP_REQ;

        // send
        ESP_LOGI("btn_click_b", "send, src 0x%04x, dst 0x%04x",
            myaddr, ctx.addr);

        sprintf(str, "a: %d, send: %u", action, count);
        logger(str, BLUE);

        err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
                request->used_size + request->hdr_size, request->token - request->hdr_size,
                MSG_TIMEOUT, need_rsp, MSG_ROLE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send vendor message 0x%06x", opcode);
        }

        if (optlist) {
            coap_delete_optlist(optlist);
            optlist = NULL;
        }
        if (request) {
            coap_delete_pdu(request);
            request = NULL;
        }
        coap_cleanup();

        ESP_LOGI("coap", "send\n\n");

        // vTaskDelay(1000 / portTICK_PERIOD_MS);
    // }
}

esp_timer_handle_t timer;

void btn_click_a()
{
    if (action == 2 || action == 0) {
        // 比佢入
        if (action == 0) {
            count = 0;
        }
    } else {
        ESP_LOGE(TAG, "wait action complete!!!");
        logger("wait action complete!!!", RED);
        return;
    }
    action = 2;

    coap_set_log_level(CONFIG_COAP_LOG_DEFAULT_LEVEL);

    optlist = NULL;

    if (coap_split_uri((const uint8_t *)server_uri, strlen(server_uri), &uri) == -1) {
        ESP_LOGE(TAG, "CoAP server uri error");
        return;
    }

    if (uri.path.length) {
        buflen = BUFSIZE;
        buf = _buf;
        res = coap_split_path(uri.path.s, uri.path.length, buf, &buflen);

        while (res--) {
            coap_insert_optlist(&optlist,
                                coap_new_optlist(COAP_OPTION_URI_PATH,
                                                    coap_opt_length(buf),
                                                    coap_opt_value(buf)));

            buf += coap_opt_size(buf);
        }
    }

    if (uri.query.length) {
        buflen = BUFSIZE;
        buf = _buf;
        res = coap_split_query(uri.query.s, uri.query.length, buf, &buflen);

        while (res--) {
            coap_insert_optlist(&optlist,
                                coap_new_optlist(COAP_OPTION_URI_QUERY,
                                                    coap_opt_length(buf),
                                                    coap_opt_value(buf)));

            buf += coap_opt_size(buf);
        }
    }

    if (tx_mid == 0) {
        prng((unsigned char *)&tx_mid, sizeof(tx_mid));
    }

    request = coap_pdu_init(0, 0, 0, 1152 - 4);
    request->type = COAP_MESSAGE_CON;
    request->tid = ++tx_mid;
    request->code = COAP_REQUEST_GET;

    // uint8_t token[1];
    // prng(token, 1);
    uint8_t token[3];
    count++;
    token[0] = (uint8_t)(count >> 8);
    token[1] = (uint8_t)(count);
    token[2] = 0;
    coap_add_token(request, 3, token);

    coap_add_optlist_pdu(request, &optlist);

    gettimeofday(&tv_now, NULL);
    int64_t cpu_time = (int64_t)tv_now.tv_sec * 1000L + (int64_t)tv_now.tv_usec / 1000 - 1617120000000;
    uint32_t t = (uint32_t) cpu_time;

    ESP_LOGI(TAG, "The current time is:");
    ESP_LOGI(TAG, "%" PRIu32 "", t);

    uint8_t payload[8];
    payload[0] = (uint8_t)(t >> 24);
    payload[1] = (uint8_t)(t >> 16);
    payload[2] = (uint8_t)(t >> 8);
    payload[3] = (uint8_t)(t);
    payload[4] = (uint8_t)(t >> 24);
    payload[5] = (uint8_t)(t >> 16);
    payload[6] = (uint8_t)(t >> 8);
    payload[7] = (uint8_t)(t);
    coap_add_data(request, 8, payload);

    coap_pdu_encode_header(request, COAP_PROTO_UDP);

    bool need_rsp = false;

    // 準備 msg
    ctx.net_idx = store.net_idx;
    ctx.app_idx = store.app_idx;
    ctx.addr = 0xffff;
    ctx.send_ttl = MSG_SEND_TTL;
    ctx.send_rel = MSG_SEND_REL;
    opcode = OP_REQ;

    // send
    ESP_LOGI("btn_click_a", "send, src 0x%04x, dst 0x%04x",
        myaddr, ctx.addr);

    sprintf(str, "a: %d, send: %u", action, count);
    logger(str, BLUE);

    err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
            request->used_size + request->hdr_size, request->token - request->hdr_size,
            MSG_TIMEOUT, need_rsp, MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send vendor message 0x%06x", opcode);
    }

    if (optlist) {
        coap_delete_optlist(optlist);
        optlist = NULL;
    }
    if (request) {
        coap_delete_pdu(request);
        request = NULL;
    }
    coap_cleanup();

    ESP_LOGI("coap", "send\n\n");

    esp_timer_stop(timer);
    ESP_ERROR_CHECK(esp_timer_start_once(timer, 3000000));
}

void btn_click_menu()
{
}

void btn_click_volume()
{
}

void btn_click_select()
{
}

void btn_click_start()
{
}

static void timer_callback(void* arg)
{
    ESP_LOGI(TAG, "\n\n!!!!!!!!!!!!!! timeout timer called !!!!!!!!!!!!!\n\n");
    if (action == 2) {
        count--;
        retry++;
        btn_click_a();
    }
}

static void ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                             esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        ESP_LOGI(TAG, "Recv 0x%06x, tid 0x%04x", param->model_operation.opcode, store.tid);
        break;
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG, "Failed to send message 0x%06x", param->model_send_comp.opcode);
            break;
        }
        ESP_LOGI(TAG, "Send 0x%06x", param->model_send_comp.opcode);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:;
        ESP_LOGI(TAG, "Receive publish message 0x%06x", param->client_recv_publish_msg.opcode);
        ESP_LOG_BUFFER_HEX("recv data", param->client_recv_publish_msg.msg, param->client_recv_publish_msg.length);

        if (param->client_recv_publish_msg.opcode == OP_RES) {
            if (myaddr == 0x000d) {
                if (action == 2) {
                    ESP_LOGI("[2]", ",0x%06x", param->client_recv_publish_msg.opcode);
                    esp_timer_stop(timer);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
                    retry = 0;
                    btn_click_a();
                }
            }
        }

        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Client message 0x%06x timeout", param->client_send_timeout.opcode);
        break;
    default:
        break;
    }
}

static esp_err_t ble_mesh_init(void)
{
    esp_err_t err = ESP_OK;

    esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(ble_mesh_config_server_cb);
    esp_ble_mesh_register_custom_model_callback(ble_mesh_custom_model_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack");
        return err;
    }

    err = esp_ble_mesh_client_model_init(&vnd_models[0]);
    if (err) {
        ESP_LOGE(TAG, "Failed to initialize vendor client");
        return err;
    }

    err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh Node initialized");

    return ESP_OK;
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void app_main(void)
{
    ESP_LOGI(TAG, "!!! app_main !!!");

    ESP_LOGI(TAG, "Initializing SPIFFS");

	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 8,
		.format_if_mount_failed =true
	};

    // Use settings defined above toinitialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is anall-in-one convenience function.
	esp_err_t ret =esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",esp_err_to_name(ret));
		}
		return;
	}

    size_t total = 0, used = 0;
	ret = esp_spiffs_info(NULL, &total,&used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG,"Failed to get SPIFFS partition information (%s)",esp_err_to_name(ret));
	} else {
		ESP_LOGI(TAG,"Partition size: total: %d, used: %d", total, used);
	}

	SPIFFS_Directory("/spiffs/");

    InitFontx(fx16G,"/spiffs/ILGH16XB.FNT",""); // 8x16Dot Gothic

    spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO, CONFIG_BL_GPIO);
    uint16_t model = 0x9341;
    lcdInit(&dev, model, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETX, CONFIG_OFFSETY);

    ScrollTest(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT);

    esp_err_t err;

    ESP_LOGI(TAG, "Initializing...");
    logger("Initializing...", BLUE);

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    ESP_ERROR_CHECK(example_connect());

    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");

        ESP_LOGI(TAG, "Initializing SNTP");
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_set_time_sync_notification_cb(time_sync_notification_cb);
        sntp_init();

        // wait for time to be set
        while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
            ESP_LOGI(TAG, "Waiting for system time to be set...");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }

        ESP_ERROR_CHECK( example_disconnect() );
    }

    setenv("TZ", "CST-8", 1);
    tzset();
    localtime_r(&now, &timeinfo);

    gettimeofday(&tv_now, NULL);
    int64_t cpu_time = (int64_t)tv_now.tv_sec * 1000L + (int64_t)tv_now.tv_usec / 1000 - 1617033600000;
    uint32_t t = (uint32_t) cpu_time;

    ESP_LOGI(TAG, "The current time is:");
    ESP_LOGI(TAG, "%" PRIu32 "\n", t);
    sprintf(str, "time: %u", t);
    logger(str, BLUE);

    board_init();

    err = bluetooth_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    err = ble_mesh_nvs_open(&NVS_HANDLE);
    if (err) {
        return;
    }

    ble_mesh_get_dev_uuid(dev_uuid);

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }

    const esp_timer_create_args_t timer_args = {
            .callback = &timer_callback,
            /* name is optional, but may help identify the timer when debugging */
            .name = "periodic"
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
}