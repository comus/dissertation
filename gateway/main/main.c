#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/param.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "protocol_examples_common.h"

#if 1
/* Needed until coap_dtls.h becomes a part of libcoap proper */
#include "libcoap.h"
#include "coap_dtls.h"
#endif
#include "coap.h"

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

#define BUFSIZE 40
#define COAP_DEFAULT_DEMO_URI "coap://coap.me/hello"
#define COAP_DEFAULT_TIME_SEC 5
#define EXAMPLE_COAP_LOG_DEFAULT_LEVEL CONFIG_COAP_LOG_DEFAULT_LEVEL

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;
static esp_netif_t * sta_netif = NULL;
static esp_netif_t * ap_netif = NULL;
static bool reconnect = true;

static int resp_wait = 1;
static coap_optlist_t *optlist = NULL;
static int wait_ms;
uint8_t *send_buf;
size_t send_buf_len = 0;
struct hostent *hp;
coap_address_t    dst_addr;
static coap_uri_t uri;
const char       *server_uri = COAP_DEFAULT_DEMO_URI;
char *phostname = NULL;

unsigned char _buf[BUFSIZE];
unsigned char *buf;
size_t buflen;
int res;
coap_session_t *session = NULL;

uint16_t tx_mid = 0;

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x32, 0x10 };

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
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 7,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
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
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, ESP_BLE_MESH_VND_MODEL_ID_SERVER,
    vnd_op, NULL, NULL),
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

void send_coap_msg(uint8_t *buf, uint16_t len);

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
                param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_VND_MODEL_ID_SERVER) {
                store.app_idx = param->value.state_change.mod_app_bind.app_idx;
                mesh_info_store();
            }
            break;
        default:
            break;
        }
    }
}

static void ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                             esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        ESP_LOGI(TAG, "Recv 0x%06x", param->model_operation.opcode);
        ESP_LOG_BUFFER_HEX("data", param->model_operation.msg, param->model_operation.length);

        // 將收到的東西 forward 出去
        send_coap_msg(param->model_operation.msg, param->model_operation.length);

        break;
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG, "Failed to send message 0x%06x", param->model_send_comp.opcode);
            break;
        }
        ESP_LOGI(TAG, "Send 0x%06x", param->model_send_comp.opcode);
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

    err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh Node initialized");

    return ESP_OK;
}

ssize_t
custom_coap_session_send_pdu(coap_session_t *session, coap_pdu_t *pdu) {
  ESP_LOGI("custom_coap_session_send_pdu", "called");
  ssize_t bytes_written = -1;
  assert(pdu->hdr_size > 0);
  switch(session->proto) {
    case COAP_PROTO_UDP:
      ESP_LOGI("custom_coap_session_send_pdu", "COAP_PROTO_UDP");
      bytes_written = coap_session_send(session, send_buf, send_buf_len);
      // bytes_written = coap_session_send(session, pdu->token - pdu->hdr_size,
      //                                   send_buf_len);
    //   bytes_written = coap_session_send(session, pdu->token - pdu->hdr_size,
    //                                     pdu->used_size + pdu->hdr_size);
      break;
    case COAP_PROTO_DTLS:
      bytes_written = coap_dtls_send(session, pdu->token - pdu->hdr_size,
                                     pdu->used_size + pdu->hdr_size);
      break;
    case COAP_PROTO_TCP:
      bytes_written = coap_session_write(session, pdu->token - pdu->hdr_size,
                                         pdu->used_size + pdu->hdr_size);
      break;
    case COAP_PROTO_TLS:
      bytes_written = coap_tls_write(session, pdu->token - pdu->hdr_size,
                                     pdu->used_size + pdu->hdr_size);
      break;
    default:
      break;
  }
//   coap_show_pdu(LOG_DEBUG, pdu);
  return bytes_written;
}

void custom_coap_session_send_csm(coap_session_t *session) {
  ESP_LOGI("coap_session_send_csm", "called");
  coap_pdu_t *pdu;
  uint8_t buf[4];
  assert(COAP_PROTO_RELIABLE(session->proto));
  coap_log(LOG_DEBUG, "***%s: sending CSM\n", coap_session_str(session));
  session->state = COAP_SESSION_STATE_CSM;
  session->partial_write = 0;
  if (session->mtu == 0)
    session->mtu = COAP_DEFAULT_MTU;  /* base value */
  pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_SIGNALING_CSM, 0, 16);
  if ( pdu == NULL
    || coap_add_option(pdu, COAP_SIGNALING_OPTION_MAX_MESSAGE_SIZE,
         coap_encode_var_safe(buf, sizeof(buf),
                                COAP_DEFAULT_MAX_PDU_RX_SIZE), buf) == 0
    || coap_pdu_encode_header(pdu, session->proto) == 0
  ) {
    coap_session_disconnected(session, COAP_NACK_NOT_DELIVERABLE);
  } else {
    ssize_t bytes_written = custom_coap_session_send_pdu(session, pdu);
    ESP_LOGI("coap_session_send_csm", "bytes_written %d", bytes_written);
    ESP_LOGI("coap_session_send_csm", "pdu->used_size %d", pdu->used_size);
    ESP_LOGI("coap_session_send_csm", "pdu->hdr_size %d", pdu->hdr_size);
    if (bytes_written != (ssize_t)pdu->used_size + pdu->hdr_size)
      coap_session_disconnected(session, COAP_NACK_NOT_DELIVERABLE);
  }
  if (pdu)
    coap_delete_pdu(pdu);
}

static ssize_t
custom_coap_send_pdu(coap_session_t *session, coap_pdu_t *pdu, coap_queue_t *node) {
  ESP_LOGI("coap_send_pdu", "called");

  ssize_t bytes_written;

#ifdef WITH_LWIP

  ESP_LOGI("coap_send_pdu", "defined WITH_LWIP");

  coap_socket_t *sock = &session->sock;
  if (sock->flags == COAP_SOCKET_EMPTY) {
    assert(session->endpoint != NULL);
    sock = &session->endpoint->sock;
  }
  if (pdu->type == COAP_MESSAGE_CON && COAP_PROTO_NOT_RELIABLE(session->proto))
    session->con_active++;

  bytes_written = coap_socket_send_pdu(sock, session, pdu);
  if (LOG_DEBUG <= coap_get_log_level()) {
    coap_show_pdu(LOG_DEBUG, pdu);
  }
  coap_ticks(&session->last_rx_tx);

#else

  ESP_LOGI("coap_send_pdu", "not defined WITH_LWIP");

  /* Do not send error responses for requests that were received via
  * IP multicast.
  * FIXME: If No-Response option indicates interest, these responses
  *        must not be dropped. */
  if (coap_is_mcast(&session->local_addr) &&
    COAP_RESPONSE_CLASS(pdu->code) > 2) {
    return COAP_DROPPED_RESPONSE;
  }

  if (session->state == COAP_SESSION_STATE_NONE) {
    if (session->proto == COAP_PROTO_DTLS && !session->tls) {
      session->tls = coap_dtls_new_client_session(session);
      if (session->tls) {
        session->state = COAP_SESSION_STATE_HANDSHAKE;
        return coap_session_delay_pdu(session, pdu, node);
      }
      coap_handle_event(session->context, COAP_EVENT_DTLS_ERROR, session);
      return -1;
    } else if(COAP_PROTO_RELIABLE(session->proto)) {
      if (!coap_socket_connect_tcp1(
        &session->sock, &session->local_if, &session->remote_addr,
        session->proto == COAP_PROTO_TLS ? COAPS_DEFAULT_PORT : COAP_DEFAULT_PORT,
        &session->local_addr, &session->remote_addr
      )) {
        coap_handle_event(session->context, COAP_EVENT_TCP_FAILED, session);
        return -1;
      }
      session->last_ping = 0;
      session->last_pong = 0;
      session->csm_tx = 0;
      coap_ticks( &session->last_rx_tx );
      if ((session->sock.flags & COAP_SOCKET_WANT_CONNECT) != 0) {
        session->state = COAP_SESSION_STATE_CONNECTING;
        return coap_session_delay_pdu(session, pdu, node);
      }
      coap_handle_event(session->context, COAP_EVENT_TCP_CONNECTED, session);
      if (session->proto == COAP_PROTO_TLS) {
        int connected = 0;
        session->state = COAP_SESSION_STATE_HANDSHAKE;
        session->tls = coap_tls_new_client_session(session, &connected);
        if (session->tls) {
          if (connected) {
            coap_handle_event(session->context, COAP_EVENT_DTLS_CONNECTED, session);
            custom_coap_session_send_csm(session);
          }
          return coap_session_delay_pdu(session, pdu, node);
        }
        coap_handle_event(session->context, COAP_EVENT_DTLS_ERROR, session);
        coap_session_disconnected(session, COAP_NACK_TLS_FAILED);
        return -1;
      } else {
        custom_coap_session_send_csm(session);
      }
    } else {
      return -1;
    }
  }

  if (session->state != COAP_SESSION_STATE_ESTABLISHED ||
      (pdu->type == COAP_MESSAGE_CON && session->con_active >= COAP_DEFAULT_NSTART)) {
    return coap_session_delay_pdu(session, pdu, node);
  }

  if ((session->sock.flags & COAP_SOCKET_NOT_EMPTY) &&
    (session->sock.flags & COAP_SOCKET_WANT_WRITE))
    return coap_session_delay_pdu(session, pdu, node);

  if (pdu->type == COAP_MESSAGE_CON && COAP_PROTO_NOT_RELIABLE(session->proto))
    session->con_active++;

  bytes_written = custom_coap_session_send_pdu(session, pdu);

#endif /* WITH_LWIP */

  return bytes_written;
}

coap_tid_t
custom_coap_send(coap_session_t *session, coap_pdu_t *pdu) {
  ESP_LOGI("coap_send", "called");
  uint8_t r;
  ssize_t bytes_written;

//   if (!coap_pdu_encode_header(pdu, session->proto)) {
//     goto error;
//   }

  // 輸入自己定義的 buffer
  // memcpy(pdu->token - pdu->hdr_size, send_buf, send_buf_len);
  // memcpy(pdu->token - 4, send_buf, send_buf_len);
  // pdu->used_size = send_buf_len - 4;
  // pdu->hdr_size = 4;
//   ESP_LOG_BUFFER_HEX("!!!data!!!", pdu->token - pdu->hdr_size,
//                                         pdu->used_size + pdu->hdr_size);

  bytes_written = custom_coap_send_pdu( session, pdu, NULL );

  if (bytes_written == COAP_PDU_DELAYED) {
    /* do not free pdu as it is stored with session for later use */
    return pdu->tid;
  }

  if (bytes_written < 0) {
    coap_delete_pdu(pdu);
    return (coap_tid_t)bytes_written;
  }

  if (COAP_PROTO_RELIABLE(session->proto) &&
    (size_t)bytes_written < pdu->used_size + pdu->hdr_size) {
    if (coap_session_delay_pdu(session, pdu, NULL) == COAP_PDU_DELAYED) {
      session->partial_write = (size_t)bytes_written;
      /* do not free pdu as it is stored with session for later use */
      return pdu->tid;
    } else {
      goto error;
    }
  }

  if (pdu->type != COAP_MESSAGE_CON || COAP_PROTO_RELIABLE(session->proto)) {
    coap_tid_t id = pdu->tid;
    coap_delete_pdu(pdu);
    return id;
  }

  coap_queue_t *node = coap_new_node();
  if (!node) {
    coap_log(LOG_DEBUG, "coap_wait_ack: insufficient memory\n");
    goto error;
  }

  node->id = pdu->tid;
  node->pdu = pdu;
  prng(&r, sizeof(r));
  /* add timeout in range [ACK_TIMEOUT...ACK_TIMEOUT * ACK_RANDOM_FACTOR] */
  node->timeout = coap_calc_timeout(session, r);
  return coap_wait_ack(session->context, session, node);
 error:
  coap_delete_pdu(pdu);
  return COAP_INVALID_TID;
}

static void message_handler(coap_context_t *ctx, coap_session_t *session,
                            coap_pdu_t *sent, coap_pdu_t *received,
                            const coap_tid_t id)
{
    ESP_LOGI("message_handler", "called");
    unsigned char *data = NULL;
    size_t data_len;
    coap_pdu_t *pdu = NULL;
    coap_opt_t *block_opt;
    coap_opt_iterator_t opt_iter;
    unsigned char buf[4];
    coap_optlist_t *option;
    coap_tid_t tid;

    if (COAP_RESPONSE_CLASS(received->code) == 2) {
        ESP_LOG_BUFFER_HEX("message_handler, received",
            received->token - received->hdr_size, received->used_size + received->hdr_size);

        esp_ble_mesh_msg_ctx_t ctx = {0};
        ctx.net_idx = store.net_idx;
        ctx.app_idx = store.app_idx;
        ctx.addr = 0x000d;
        ctx.send_ttl = MSG_SEND_TTL;
        ctx.send_rel = MSG_SEND_REL;

        esp_ble_mesh_server_model_send_msg(&vnd_models[0],
                &ctx, OP_RES,
                received->used_size + received->hdr_size, received->token - received->hdr_size);
    }
}

#ifdef CONFIG_COAP_MBEDTLS_PKI

static int
verify_cn_callback(const char *cn,
                   const uint8_t *asn1_public_cert,
                   size_t asn1_length,
                   coap_session_t *session,
                   unsigned depth,
                   int validated,
                   void *arg
                  )
{
    coap_log(LOG_INFO, "CN '%s' presented by server (%s)\n",
             cn, depth ? "CA" : "Certificate");
    return 1;
}
#endif /* CONFIG_COAP_MBEDTLS_PKI */

void send_coap_msg(uint8_t *buf, uint16_t len)
{
    coap_pdu_t *request = NULL;
    request = coap_new_pdu(session);
    if (!request) {
        ESP_LOGE(TAG, "coap_new_pdu() failed");
        return;
    }

    send_buf = buf;
    send_buf_len = len;

    // 開始改變
    request->type = (send_buf[0] >> 4) & 0x03;
    request->token_length = send_buf[0] & 0x0f;
    request->code = send_buf[1];
    request->tid = (uint16_t)send_buf[2] << 8 | send_buf[3];
    request->hdr_size = 4;
    request->used_size = send_buf_len - 4;

    custom_coap_send(session, request);
}

void subscribe()
{
    
}

void coap_example_client()
{
    coap_context_t *ctx = NULL;

    while (1) {
        optlist = NULL;
        if (coap_split_uri((const uint8_t *)server_uri, strlen(server_uri), &uri) == -1) {
            ESP_LOGE(TAG, "CoAP server uri error");
            break;
        }

        if (uri.scheme == COAP_URI_SCHEME_COAPS && !coap_dtls_is_supported()) {
            ESP_LOGE(TAG, "MbedTLS (D)TLS Client Mode not configured");
            break;
        }
        if (uri.scheme == COAP_URI_SCHEME_COAPS_TCP && !coap_tls_is_supported()) {
            ESP_LOGE(TAG, "CoAP server uri coaps+tcp:// scheme is not supported");
            break;
        }

        phostname = (char *)calloc(1, uri.host.length + 1);
        if (phostname == NULL) {
            ESP_LOGE(TAG, "calloc failed");
            break;
        }

        memcpy(phostname, uri.host.s, uri.host.length);
        hp = gethostbyname(phostname);
        free(phostname);

        if (hp == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            free(phostname);
            continue;
        }
        char tmpbuf[INET6_ADDRSTRLEN];
        coap_address_init(&dst_addr);
        switch (hp->h_addrtype) {
            case AF_INET:
                dst_addr.addr.sin.sin_family      = AF_INET;
                dst_addr.addr.sin.sin_port        = htons(uri.port);
                memcpy(&dst_addr.addr.sin.sin_addr, hp->h_addr, sizeof(dst_addr.addr.sin.sin_addr));
                inet_ntop(AF_INET, &dst_addr.addr.sin.sin_addr, tmpbuf, sizeof(tmpbuf));
                ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", tmpbuf);
                break;
            case AF_INET6:
                dst_addr.addr.sin6.sin6_family      = AF_INET6;
                dst_addr.addr.sin6.sin6_port        = htons(uri.port);
                memcpy(&dst_addr.addr.sin6.sin6_addr, hp->h_addr, sizeof(dst_addr.addr.sin6.sin6_addr));
                inet_ntop(AF_INET6, &dst_addr.addr.sin6.sin6_addr, tmpbuf, sizeof(tmpbuf));
                ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", tmpbuf);
                break;
            default:
                ESP_LOGE(TAG, "DNS lookup response failed");
                goto clean_up;
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

        coap_insert_optlist(&optlist, coap_new_optlist(COAP_OPTION_OBSERVE,
                        COAP_OBSERVE_ESTABLISH, NULL));

        ctx = coap_new_context(NULL);
        if (!ctx) {
            ESP_LOGE(TAG, "coap_new_context() failed");
            goto clean_up;
        }

        /*
         * Note that if the URI starts with just coap:// (not coaps://) the
         * session will still be plain text.
         *
         * coaps+tcp:// is NOT supported by the libcoap->mbedtls interface
         * so COAP_URI_SCHEME_COAPS_TCP will have failed in a test above,
         * but the code is left in for completeness.
         */
        if (uri.scheme == COAP_URI_SCHEME_COAPS || uri.scheme == COAP_URI_SCHEME_COAPS_TCP) {
        } else {
            session = coap_new_client_session(ctx, NULL, &dst_addr,
                                              uri.scheme == COAP_URI_SCHEME_COAP_TCP ? COAP_PROTO_TCP :
                                              COAP_PROTO_UDP);
        }
        if (!session) {
            ESP_LOGE(TAG, "coap_new_client_session() failed");
            goto clean_up;
        }

        coap_register_response_handler(ctx, message_handler);

        coap_pdu_t *request = NULL;

        if (tx_mid == 0) {
            prng((unsigned char *)&tx_mid, sizeof(tx_mid));
        }

        request = coap_new_pdu(session);
        if (!request) {
            ESP_LOGE(TAG, "coap_new_pdu() failed");
            goto clean_up;
        }
        request->type = COAP_MESSAGE_CON;
        request->tid = ++tx_mid;
        request->code = COAP_REQUEST_GET;

        uint8_t token[1];
        prng(token, 1);
        coap_add_token(request, 1, token);

        coap_add_optlist_pdu(request, &optlist);

        coap_pdu_encode_header(request, COAP_PROTO_UDP);

        custom_coap_send(session, request);

        wait_ms = COAP_DEFAULT_TIME_SEC * 1000;
        while (1) {
            ESP_LOGI("coap_example_client", "while resp_wait!!!");
            int result = coap_run_once(ctx, wait_ms > 1000 ? 1000 : wait_ms);
        }
clean_up:
        ESP_LOGI("coap_example_client", "clean_up");
        if (optlist) {
            coap_delete_optlist(optlist);
            optlist = NULL;
        }
        if (session) {
            coap_session_release(session);
        }
        if (ctx) {
            coap_free_context(ctx);
        }
        coap_cleanup();
        
        break;
    }

    vTaskDelete(NULL);
}

void btn_click_a()
{
}

void btn_click_b()
{
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

static bool wifi_cmd_sta_join(const char *ssid, const char *pass)
{
    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 0);

    wifi_config_t wifi_config = { 0 };

    strlcpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strncpy((char *) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    if (bits & CONNECTED_BIT) {
        reconnect = false;
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        xEventGroupWaitBits(wifi_event_group, DISCONNECTED_BIT, 0, 1, portTICK_RATE_MS);
    }

    reconnect = true;
    esp_wifi_disconnect();
    //ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) ); //by snake
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    esp_wifi_connect();

    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, 0, 1, 5000 / portTICK_RATE_MS);

    return true;
}

void register_wifi()
{
    wifi_cmd_sta_join("369office", "office333666999");
}

static void scan_done_handler(void)
{
    uint16_t sta_number = 0;
    uint8_t i;
    wifi_ap_record_t *ap_list_buffer;

    esp_wifi_scan_get_ap_num(&sta_number);
    ap_list_buffer = malloc(sta_number * sizeof(wifi_ap_record_t));
    if (ap_list_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to malloc buffer to print scan results");
        return;
    }

    if (esp_wifi_scan_get_ap_records(&sta_number, (wifi_ap_record_t *)ap_list_buffer) == ESP_OK) {
        for (i = 0; i < sta_number; i++) {
            ESP_LOGI(TAG, "[%s][rssi=%d]", ap_list_buffer[i].ssid, ap_list_buffer[i].rssi);
        }
    }
    free(ap_list_buffer);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    switch (event_id) {
    case WIFI_EVENT_SCAN_DONE:
        scan_done_handler();
        ESP_LOGI(TAG, "sta scan done");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "L2 connected");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        if (reconnect) {
            ESP_LOGI(TAG, "sta disconnect, reconnect...");
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "sta disconnect");
        }
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, DISCONNECTED_BIT);
        break;
    default:
        break;
    }
    return;
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    switch (event_id) {
    case IP_EVENT_STA_GOT_IP:
        xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(TAG, "got ip");
        break;
    default:
        break;
    }
    return;
}

void initialise_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static bool initialized = false;

    if (initialized) {
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_ps(WIFI_PS_MIN_MODEM) ); //must call this
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    initialized = true;
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

    initialise_wifi();
    register_wifi();

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }

    coap_set_log_level(EXAMPLE_COAP_LOG_DEFAULT_LEVEL);
    xTaskCreate(coap_example_client, "coap", 8 * 1024, NULL, 5, NULL);
}