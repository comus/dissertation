#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
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

#define BUFSIZE 40

#define TAG "MAIN"

#define CID_ESP             0x02E5

#define MSG_SEND_TTL        3
#define MSG_SEND_REL        false
#define MSG_TIMEOUT         4000
#define MSG_ROLE            ROLE_NODE

#define ESP_BLE_MESH_VND_MODEL_ID_CLIENT    0x0000
#define ESP_BLE_MESH_VND_MODEL_ID_SERVER    0x0001

#define OP_SEND_A           ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define OP_SEND_B           ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)
#define OP_SEND_MENU        ESP_BLE_MESH_MODEL_OP_3(0x02, CID_ESP)
#define OP_SEND_VOLUME      ESP_BLE_MESH_MODEL_OP_3(0x03, CID_ESP)
#define OP_SEND_SELECT      ESP_BLE_MESH_MODEL_OP_3(0x04, CID_ESP)
#define OP_SEND_START       ESP_BLE_MESH_MODEL_OP_3(0x05, CID_ESP)
#define OP_STATUS_A           ESP_BLE_MESH_MODEL_OP_3(0x06, CID_ESP)
#define OP_STATUS_B           ESP_BLE_MESH_MODEL_OP_3(0x07, CID_ESP)
#define OP_STATUS_MENU        ESP_BLE_MESH_MODEL_OP_3(0x08, CID_ESP)
#define OP_STATUS_VOLUME      ESP_BLE_MESH_MODEL_OP_3(0x09, CID_ESP)
#define OP_STATUS_SELECT      ESP_BLE_MESH_MODEL_OP_3(0x0a, CID_ESP)
#define OP_STATUS_START       ESP_BLE_MESH_MODEL_OP_3(0x0b, CID_ESP)

static coap_optlist_t *optlist = NULL;
uint16_t tx_mid;

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
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
    .default_ttl = 7,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

static const esp_ble_mesh_client_op_pair_t vnd_op_pair[] = {
    { OP_SEND_A, OP_STATUS_A },
    { OP_SEND_B, OP_STATUS_B },
    { OP_SEND_MENU, OP_STATUS_MENU },
    { OP_SEND_VOLUME, OP_STATUS_VOLUME },
    { OP_SEND_SELECT, OP_STATUS_SELECT },
    { OP_SEND_START, OP_STATUS_START },
};

static esp_ble_mesh_client_t vendor_client = {
    .op_pair_size = ARRAY_SIZE(vnd_op_pair),
    .op_pair = vnd_op_pair,
};

// 有人傳東西給 device，這是他允許接收的指令
static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(OP_SEND_A, 2),
    ESP_BLE_MESH_MODEL_OP(OP_SEND_B, 2),
    ESP_BLE_MESH_MODEL_OP(OP_SEND_MENU, 2),
    ESP_BLE_MESH_MODEL_OP(OP_SEND_VOLUME, 2),
    ESP_BLE_MESH_MODEL_OP(OP_SEND_SELECT, 2),
    ESP_BLE_MESH_MODEL_OP(OP_SEND_START, 2),
    ESP_BLE_MESH_MODEL_OP(OP_STATUS_A, 2),
    ESP_BLE_MESH_MODEL_OP(OP_STATUS_B, 2),
    ESP_BLE_MESH_MODEL_OP(OP_STATUS_MENU, 2),
    ESP_BLE_MESH_MODEL_OP(OP_STATUS_VOLUME, 2),
    ESP_BLE_MESH_MODEL_OP(OP_STATUS_SELECT, 2),
    ESP_BLE_MESH_MODEL_OP(OP_STATUS_START, 2),
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
        FillTestRed(&dev, CONFIG_WIDTH, CONFIG_HEIGHT);
    } else if (myaddr == 0x000e) {
        FillTestGreen(&dev, CONFIG_WIDTH, CONFIG_HEIGHT);
    } else {
        FillTestBlack(&dev, CONFIG_WIDTH, CONFIG_HEIGHT);
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

void btn_click_b()
{
    // // 開始時
    // esp_ble_mesh_msg_ctx_t ctx = {0};
    // uint32_t opcode;
    // esp_err_t err;
    // int64_t idx = 0;
    // int64_t time;

    // // 計好 time 和 idx
    // time = esp_timer_get_time();
    // time = time - time % 1000 + idx;
    // // ESP_LOGI(TAG, "current time %lldus", time);
    // idx++;

    // // 準備 msg
    // ctx.net_idx = store.net_idx;
    // ctx.app_idx = store.app_idx;
    // ctx.addr = 0x000e;
    // ctx.send_ttl = MSG_SEND_TTL;
    // ctx.send_rel = MSG_SEND_REL;
    // opcode = OP_SEND_B;

    // // send
    // ESP_LOGI("btn_click_b", "send, src 0x%04x, dst 0x%04x, data: %lld, (hex) 0x%016llx",
    //     myaddr, ctx.addr, time, time);

    // err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
    //         sizeof(time), (uint8_t *)&time,
    //         MSG_TIMEOUT, true, MSG_ROLE);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to send vendor message 0x%06x", opcode);
    //     return;
    // }

    // 開始時
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint32_t opcode;
    esp_err_t err;

    // 準備 msg
    ctx.net_idx = store.net_idx;
    ctx.app_idx = store.app_idx;
    ctx.addr = 0x000e;
    ctx.send_ttl = MSG_SEND_TTL;
    ctx.send_rel = MSG_SEND_REL;
    opcode = OP_SEND_B;

    static coap_uri_t uri;
    const char       *server_uri = "coap://localhost/test";

    coap_set_log_level(0);

    unsigned char _buf[BUFSIZE];
    unsigned char *buf;
    size_t buflen;
    int res;
    coap_pdu_t *request = NULL;

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

    prng((unsigned char *)&tx_mid, sizeof(tx_mid));
    request = coap_pdu_init(0, 0, 0, 1152 - 4);
    request->type = COAP_MESSAGE_CON;
    request->tid = ++tx_mid;
    request->code = COAP_REQUEST_GET;
    coap_add_optlist_pdu(request, &optlist);
    coap_pdu_encode_header(request, COAP_PROTO_UDP);

    // send
    ESP_LOGI("btn_click_b", "send, src 0x%04x, dst 0x%04x",
        myaddr, ctx.addr);

    err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
            request->used_size + request->hdr_size, request->token - request->hdr_size,
            MSG_TIMEOUT, true, MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send vendor message 0x%06x", opcode);
        return;
    }

    if (optlist) {
        coap_delete_optlist(optlist);
        optlist = NULL;
    }
    coap_cleanup();

    ESP_LOGI("coap", "send");
}

void btn_click_a()
{
    // 開始時
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint32_t opcode;
    esp_err_t err;
    int64_t idx = 0;
    int64_t time;

    // // loop 時
    // for (int i = 0; i < 100; ++i) {
        // 計好 time 和 idx
        time = esp_timer_get_time();
        time = time - time % 1000 + idx;
        // ESP_LOGI(TAG, "%d, current time %lldus", i, time);
        idx++;

        // 準備 msg
        ctx.net_idx = store.net_idx;
        ctx.app_idx = store.app_idx;
        ctx.addr = 0x000e;
        ctx.send_ttl = MSG_SEND_TTL;
        ctx.send_rel = MSG_SEND_REL;
        opcode = OP_SEND_A;

        // send
        ESP_LOGI("btn_click_a", "send, src 0x%04x, dst 0x%04x, data: %lld, (hex) 0x%016llx",
            myaddr, ctx.addr, time, time);

        err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
                sizeof(time), (uint8_t *)&time,
                MSG_TIMEOUT, true, MSG_ROLE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send vendor message 0x%06x", opcode);
            return;
        }

        // 等時間
        // vTaskDelay(3000 / portTICK_PERIOD_MS);
    // }
}

void btn_click_menu()
{
    // 開始時
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint32_t opcode;
    esp_err_t err;
    int64_t idx = 0;
    int64_t time;
    int64_t time2[2];

    // for (int i = 0; i < 100; ++i) {
        // 計好 time 和 idx
        time = esp_timer_get_time();
        time = time - time % 1000 + idx;
        // ESP_LOGI(TAG, "current time %lldus", time);
        idx++;

        // 準備 msg
        ctx.net_idx = store.net_idx;
        ctx.app_idx = store.app_idx;
        ctx.addr = 0x000e;
        ctx.send_ttl = MSG_SEND_TTL;
        ctx.send_rel = MSG_SEND_REL;
        opcode = OP_SEND_MENU;

        // send
        ESP_LOGI("btn_click_menu", "send, src 0x%04x, dst 0x%04x, data: %lld, (hex) 0x%016llx",
            myaddr, ctx.addr, time, time);

        time2[0] = time;
        time2[1] = time;
        err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
                sizeof(time2), (uint8_t *)&time2,
                MSG_TIMEOUT, true, MSG_ROLE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send vendor message 0x%06x", opcode);
            return;
        }

        // 等時間
    //     vTaskDelay(3000 / portTICK_PERIOD_MS);
    // }
}

void btn_click_volume()
{
    // // 開始時
    // esp_ble_mesh_msg_ctx_t ctx = {0};
    // uint32_t opcode;
    // esp_err_t err;
    // int64_t idx = 0;
    // int64_t time;
    // int64_t time4[4];

    // for (int i = 0; i < 100; ++i) {
    //     // 計好 time 和 idx
    //     time = esp_timer_get_time();
    //     time = time - time % 1000 + idx;
    //     // ESP_LOGI(TAG, "current time %lldus", time);
    //     idx++;

    //     // 準備 msg
    //     ctx.net_idx = store.net_idx;
    //     ctx.app_idx = store.app_idx;
    //     ctx.addr = 0xffff;
    //     ctx.send_ttl = MSG_SEND_TTL;
    //     ctx.send_rel = MSG_SEND_REL;
    //     opcode = OP_SEND_VOLUME;

    //     // send
    //     // ESP_LOGI(TAG, "send, net_idx 0x%04x, app_idx 0x%04x",
    //         // store.net_idx, store.app_idx);
    //     time4[0] = time;
    //     time4[1] = time;
    //     time4[2] = time;
    //     time4[3] = time;
    //     err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
    //             sizeof(time4), (uint8_t *)&time4,
    //             MSG_TIMEOUT, true, MSG_ROLE);
    //     if (err != ESP_OK) {
    //         // ESP_LOGE(TAG, "Failed to send vendor message 0x%06x", opcode);
    //         return;
    //     }

    //     // 等時間
    //     vTaskDelay(3000 / portTICK_PERIOD_MS);
    // }
}

void btn_click_select()
{
    // // 開始時
    // esp_ble_mesh_msg_ctx_t ctx = {0};
    // uint32_t opcode;
    // esp_err_t err;
    // int64_t idx = 0;
    // int64_t time;
    // int64_t time8[8];

    // for (int i = 0; i < 100; ++i) {
    //     // 計好 time 和 idx
    //     time = esp_timer_get_time();
    //     time = time - time % 1000 + idx;
    //     // ESP_LOGI(TAG, "current time %lldus", time);
    //     idx++;

    //     // 準備 msg
    //     ctx.net_idx = store.net_idx;
    //     ctx.app_idx = store.app_idx;
    //     ctx.addr = 0xffff;
    //     ctx.send_ttl = MSG_SEND_TTL;
    //     ctx.send_rel = MSG_SEND_REL;
    //     opcode = OP_SEND_SELECT;

    //     // send
    //     // ESP_LOGI(TAG, "send, net_idx 0x%04x, app_idx 0x%04x",
    //         // store.net_idx, store.app_idx);
    //     time8[0] = time;
    //     time8[1] = time;
    //     time8[2] = time;
    //     time8[3] = time;
    //     time8[4] = time;
    //     time8[5] = time;
    //     time8[6] = time;
    //     time8[7] = time;
    //     err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
    //             sizeof(time8), (uint8_t *)&time8,
    //             MSG_TIMEOUT, true, MSG_ROLE);
    //     if (err != ESP_OK) {
    //         // ESP_LOGE(TAG, "Failed to send vendor message 0x%06x", opcode);
    //         return;
    //     }

    //     // 等時間
    //     vTaskDelay(3000 / portTICK_PERIOD_MS);
    // }
}

void btn_click_start()
{
    // // 開始時
    // esp_ble_mesh_msg_ctx_t ctx = {0};
    // uint32_t opcode;
    // esp_err_t err;
    // int64_t idx = 0;
    // int64_t time;
    // int64_t time16[16];

    // for (int i = 0; i < 100; ++i) {
    //     // 計好 time 和 idx
    //     time = esp_timer_get_time();
    //     time = time - time % 1000 + idx;
    //     // ESP_LOGI(TAG, "current time %lldus", time);
    //     idx++;

    //     // 準備 msg
    //     ctx.net_idx = store.net_idx;
    //     ctx.app_idx = store.app_idx;
    //     ctx.addr = 0xffff;
    //     ctx.send_ttl = MSG_SEND_TTL;
    //     ctx.send_rel = MSG_SEND_REL;
    //     opcode = OP_SEND_START;

    //     // send
    //     // ESP_LOGI(TAG, "send, net_idx 0x%04x, app_idx 0x%04x",
    //         // store.net_idx, store.app_idx);
    //     time16[0] = time;
    //     time16[1] = time;
    //     time16[2] = time;
    //     time16[3] = time;
    //     time16[4] = time;
    //     time16[5] = time;
    //     time16[6] = time;
    //     time16[7] = time;
    //     time16[8] = time;
    //     time16[9] = time;
    //     time16[10] = time;
    //     time16[11] = time;
    //     time16[12] = time;
    //     time16[13] = time;
    //     time16[14] = time;
    //     time16[15] = time;
    //     err = esp_ble_mesh_client_model_send_msg(vendor_client.model, &ctx, opcode,
    //             sizeof(time16), (uint8_t *)&time16,
    //             MSG_TIMEOUT, true, MSG_ROLE);
    //     if (err != ESP_OK) {
    //         // ESP_LOGE(TAG, "Failed to send vendor message 0x%06x", opcode);
    //         return;
    //     }
        
    //     // 等時間
    //     vTaskDelay(3000 / portTICK_PERIOD_MS);
    // }
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
        // ESP_LOGI(TAG, "event 0x%02x, opcode 0x%04x, src 0x%04x, dst 0x%04x",
        // event, param->client_recv_publish_msg.ctx->recv_op, param->client_recv_publish_msg.ctx->addr, param->client_recv_publish_msg.ctx->recv_dst);

        // uint16_t srcaddr = param->client_recv_publish_msg.ctx->addr;
        // uint16_t dstaddr = param->client_recv_publish_msg.ctx->recv_dst;

        // // 收到有人被按了a的訊息
        // if (param->client_recv_publish_msg.opcode == OP_SEND_A) {
        //     // 如果是被按的是自己，我不關心
        //     if (srcaddr == myaddr) {
        //         // nothing
        //     } else {
                
        //     }
        //     // 將結果輸出到 serial
        //     int64_t time = *(int64_t *)param->client_recv_publish_msg.msg;
        //     ESP_LOGI("[!]", ",0x%06x,%lld", param->client_recv_publish_msg.opcode, time);
        // }

        // // 收到有人被按了b的訊息
        // if (param->client_recv_publish_msg.opcode == OP_SEND_B) {
        //     int64_t time = *(int64_t *)param->client_recv_publish_msg.msg;
        //     ESP_LOGI("[#]", ",0x%06x,%lld", param->client_recv_publish_msg.opcode, time);
        // }

        // if (param->client_recv_publish_msg.opcode == ESP_BLE_MESH_VND_MODEL_OP_STATUS2) {
        //     int64_t *time = (int64_t *)param->client_recv_publish_msg.msg;
        //     ESP_LOGI("[2]", ",0x%06x,%lld,%lld", param->client_recv_publish_msg.opcode, *time, *(time + 1));
        // }

        // if (param->client_recv_publish_msg.opcode == ESP_BLE_MESH_VND_MODEL_OP_STATUS4) {
        //     int64_t *time = (int64_t *)param->client_recv_publish_msg.msg;
        //     ESP_LOGI("[4]", ",0x%06x,%lld,%lld,%lld,%lld", param->client_recv_publish_msg.opcode, *time, *(time + 1), *(time + 2), *(time + 3));
        // }

        // if (param->client_recv_publish_msg.opcode == ESP_BLE_MESH_VND_MODEL_OP_STATUS8) {
        //     int64_t *time = (int64_t *)param->client_recv_publish_msg.msg;
        //     ESP_LOGI("[8]", ",0x%06x,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld", param->client_recv_publish_msg.opcode, *time, *(time + 1), *(time + 2), *(time + 3), *(time + 4), *(time + 5), *(time + 6), *(time + 7));
        // }

        // if (param->client_recv_publish_msg.opcode == ESP_BLE_MESH_VND_MODEL_OP_STATUS16) {
        //     int64_t *time = (int64_t *)param->client_recv_publish_msg.msg;
        //     ESP_LOGI("[16]", ",0x%06x,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld", param->client_recv_publish_msg.opcode, *time, *(time + 1), *(time + 2), *(time + 3), *(time + 4), *(time + 5), *(time + 6), *(time + 7), *(time + 8), *(time + 9), *(time + 10), *(time + 11), *(time + 12), *(time + 13), *(time + 14), *(time + 15));
        // }

        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        ESP_LOGW(TAG, "Client message 0x%06x timeout", param->client_send_timeout.opcode);
        if (param->client_send_timeout.opcode == OP_SEND_B) {
            btn_click_b();
        } else if (param->client_send_timeout.opcode == OP_SEND_A) {
            btn_click_a();
        } else if (param->client_send_timeout.opcode == OP_SEND_MENU) {
            btn_click_menu();
        }
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

    esp_err_t err;

    ESP_LOGI(TAG, "Initializing...");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

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
}