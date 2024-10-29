#include "tuya_cloud_types.h"
#include "esp_log.h"
#include "tuya_log.h"
#include "system_interface.h"
#include "ble_interface.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"

typedef struct
{
    UCHAR_T role;
    struct
    {
        UCHAR_T buffer[256];
        USHORT_T size;
        USHORT_T handle;
    } read_char[1];
} TKL_BLUETOOTH_SERVER_PARAMS_T;

static TKL_BLUETOOTH_SERVER_PARAMS_T tuya_ble_server;
static TKL_BLE_GAP_EVT_FUNC_CB tkl_bluetooth_gap_callback;
static TKL_BLE_GATT_EVT_FUNC_CB tkl_bluetooth_gatt_callback;
static int stack_sync_flag = 0;

static int tuya_ble_host_write_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct os_mbuf *om = ctxt->om;
    TKL_BLE_GATT_PARAMS_EVT_T gatt_event;
    uint8_t i;

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        for (i = 0; i < 1; i++)
        {
            if (tuya_ble_server.read_char[i].handle == attr_handle)
            {
                os_mbuf_append(om, tuya_ble_server.read_char[i].buffer, tuya_ble_server.read_char[i].size);

                return OPRT_OK;
            }
        }
        break;
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        while (om)
        {
            gatt_event.type = TKL_BLE_GATT_EVT_WRITE_REQ;
            gatt_event.conn_handle = conn_handle;
            gatt_event.gatt_event.write_report.char_handle = attr_handle;
            gatt_event.gatt_event.write_report.report.length = om->om_len;
            gatt_event.gatt_event.write_report.report.p_data = (UCHAR_T *)om->om_data;

            if (tkl_bluetooth_gatt_callback)
            {
                tkl_bluetooth_gatt_callback(&gatt_event);
            }

            om = SLIST_NEXT(om, om_next);
        }
        return OPRT_OK;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
    return 0;
}

static void tuya_ble_host_stack_sync_callback(void)
{
    BLE_HS_LOG_INFO("Stack sync");

    stack_sync_flag = 1;
}

static void tuya_ble_host_stack_reset_callback(int reason)
{
    BLE_HS_LOG_INFO("Stack Reset,  reson = %d", reason);
}

static int tuya_ble_host_gap_event(struct ble_gap_event *event, void *arg)
{

    TKL_BLE_GAP_PARAMS_EVT_T gap_event;
    TKL_BLE_GATT_PARAMS_EVT_T gatt_event;

    TKL_BLUETOOTH_SERVER_PARAMS_T *p_role = arg;

    memset(&gap_event, 0, sizeof(TKL_BLE_GAP_PARAMS_EVT_T));
    memset(&gatt_event, 0, sizeof(TKL_BLE_GATT_PARAMS_EVT_T));

    gap_event.conn_handle = TKL_BLE_GATT_INVALID_HANDLE;
    gatt_event.conn_handle = TKL_BLE_GATT_INVALID_HANDLE;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        gap_event.type = TKL_BLE_GAP_EVT_CONNECT;
        gap_event.result = event->connect.status;
        gap_event.conn_handle = event->connect.conn_handle;
        gap_event.gap_event.connect.role = p_role->role;

        BLE_HS_LOG(INFO, "BLE_GAP_EVENT_CONNECT(0x%02x), handle = 0x%02x, Role(%d)\n", event->connect.status, event->connect.conn_handle, p_role->role);

        break;

    case BLE_GAP_EVENT_DISCONNECT:
        gap_event.type = TKL_BLE_GAP_EVT_DISCONNECT;
        gap_event.result = 0;
        gap_event.conn_handle = event->disconnect.conn.conn_handle;

        gap_event.gap_event.disconnect.reason = event->disconnect.reason;
        gap_event.gap_event.disconnect.role = p_role->role;

        BLE_HS_LOG(INFO, "BLE_GAP_EVENT_DISCONNECT(0x%02x)\n", event->disconnect.reason);

        break;

    case BLE_GAP_EVENT_DISC_COMPLETE:
    {
        BLE_HS_LOG(INFO, "BLE_GAP_EVENT_DISC_COMPLETE");
    }
    break;

    case BLE_GAP_EVENT_CONN_UPDATE:
    {
        struct ble_gap_conn_desc desc;

        gap_event.type = TKL_BLE_GAP_EVT_CONN_PARAM_UPDATE;
        gap_event.result = 0;
        gap_event.conn_handle = event->conn_update.conn_handle;

        ble_gap_conn_find(event->conn_update.conn_handle, &desc);

        gap_event.gap_event.conn_param.conn_interval_min = desc.conn_itvl;
        gap_event.gap_event.conn_param.conn_interval_max = desc.conn_itvl;
        gap_event.gap_event.conn_param.conn_latency = desc.conn_latency;
        gap_event.gap_event.conn_param.conn_sup_timeout = desc.supervision_timeout;

        TY_LOGD("BLE_GAP_EVENT_CONN_UPDATE,0x%x,0x%x,0x%x", desc.conn_itvl, desc.conn_latency, desc.supervision_timeout);
    }
    break;
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        BLE_HS_LOG(INFO, "BLE_GAP_EVENT_CONN_UPDATE_REQ");

        break;

    case BLE_GAP_EVENT_MTU:
        gatt_event.type = TKL_BLE_GATT_EVT_MTU_REQUEST;
        gatt_event.result = 0;
        gatt_event.conn_handle = event->mtu.conn_handle;

        gatt_event.gatt_event.exchange_mtu = event->mtu.value;

        TY_LOGD("mtu update event; conn_handle=0x%04x mtu=%d, channel id = %d\n", event->mtu.conn_handle, event->mtu.value, event->mtu.channel_id);

        break;

    case BLE_GAP_EVENT_NOTIFY_TX:
        gatt_event.type = TKL_BLE_GATT_EVT_NOTIFY_TX;
        gatt_event.result = event->notify_tx.status;
        gatt_event.conn_handle = event->notify_tx.conn_handle;

        gatt_event.gatt_event.notify_result.char_handle = event->notify_tx.attr_handle;

        BLE_HS_LOG(INFO, "send notify ok");

        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        BLE_HS_LOG(INFO, "receive notify ok");
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        gatt_event.type = TKL_BLE_GATT_EVT_SUBSCRIBE;
        gatt_event.result = 0;
        gatt_event.conn_handle = event->subscribe.conn_handle;
        gatt_event.gatt_event.subscribe.char_handle = event->subscribe.attr_handle;
        gatt_event.gatt_event.subscribe.reason = event->subscribe.reason;
        gatt_event.gatt_event.subscribe.prev_notify = event->subscribe.prev_notify;
        gatt_event.gatt_event.subscribe.cur_notify = event->subscribe.cur_notify;
        gatt_event.gatt_event.subscribe.prev_indicate = event->subscribe.prev_indicate;
        gatt_event.gatt_event.subscribe.cur_indicate = event->subscribe.cur_indicate;

        BLE_HS_LOG(INFO, "BLE_GAP_EVENT_SUBSCRIBE");

        break;
    default:
        TY_LOGD("Unknow Type = %d", event->type);

        return OPRT_OK;
    }

    if (tkl_bluetooth_gap_callback && gap_event.conn_handle != TKL_BLE_GATT_INVALID_HANDLE)
    {
        tkl_bluetooth_gap_callback(&gap_event);
    }
    else if (tkl_bluetooth_gatt_callback && gatt_event.conn_handle != TKL_BLE_GATT_INVALID_HANDLE)
    {
        tkl_bluetooth_gatt_callback(&gatt_event);
    }

    return OPRT_OK;
}

static void bleprph_host_task(void *param)
{
    TY_LOGD("BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();
    TY_LOGD("BLE Host Task Stop");
    nimble_port_freertos_deinit();
}

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

OPERATE_RET tkl_ble_stack_init(uint8_t role)
{
    if (ble_hs_is_enabled())
    {
        BLE_HS_LOG_INFO("ble_stack already inited\r\n");

        return OPRT_OK;
    }

    OPERATE_RET ret = OPRT_OK;

    if ((role & TKL_BLE_ROLE_SERVER) == TKL_BLE_ROLE_SERVER)
    {
        memset(&tuya_ble_server, 0, sizeof(TKL_BLUETOOTH_SERVER_PARAMS_T));

        tuya_ble_server.role = TKL_BLE_ROLE_SERVER;
    }

    if ((role & TKL_BLE_ROLE_CLIENT) == TKL_BLE_ROLE_CLIENT)
    {
        // memset(&tuya_ble_client, 0, sizeof(TKL_BLUETOOTH_CLIENT_PARAMS_T));
        // tuya_ble_client.role = TKL_BLE_ROLE_CLIENT;
    }

    nimble_port_init();

    ble_hs_cfg.reset_cb = tuya_ble_host_stack_reset_callback;
    ble_hs_cfg.sync_cb = tuya_ble_host_stack_sync_callback;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.sm_io_cap = 3;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_ans_init();

    return ret;
}

OPERATE_RET tkl_ble_stack_deinit(uint8_t role)
{
    // TODO: ...

    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_callback_register(const TKL_BLE_GAP_EVT_FUNC_CB gap_evt)
{
    tkl_bluetooth_gap_callback = gap_evt;

    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_adv_rsp_data_set(TKL_BLE_DATA_T const *p_adv, TKL_BLE_DATA_T const *p_scan_rsp)
{
    OPERATE_RET rt = OPRT_OK;

    if (p_adv && p_adv->p_data != NULL)
    {
        TUYA_CALL_ERR_RETURN(ble_gap_adv_set_data(p_adv->p_data, p_adv->length));
    }

    if (p_scan_rsp && p_scan_rsp->p_data != NULL)
    {
        TUYA_CALL_ERR_RETURN(ble_gap_adv_rsp_set_data(p_scan_rsp->p_data, p_scan_rsp->length));
    }

    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_adv_start(TKL_BLE_GAP_ADV_PARAMS_T const *p_adv_params)
{
    struct ble_gap_adv_params adv_params;
    int rc;

    memset(&adv_params, 0, sizeof(adv_params));

    if (p_adv_params->adv_type == TKL_BLE_GAP_ADV_TYPE_CONN_SCANNABLE_UNDIRECTED)
    {
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    }
    else if (p_adv_params->adv_type == TKL_BLE_GAP_ADV_TYPE_NONCONN_SCANNABLE_UNDIRECTED)
    {
        adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    }
    else
    {
        TY_LOGW("Invalid adv type");

        return OPRT_INVALID_PARM;
    }

    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(p_adv_params->adv_interval_min);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(p_adv_params->adv_interval_max);
    rc = ble_gap_adv_start(BLE_HCI_ADV_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, tuya_ble_host_gap_event, &tuya_ble_server);
    if (rc != 0)
    {
        BLE_HS_LOG(INFO, "error enabling advertisement; rc=%d\n", rc);

        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_adv_stop(void)
{
    if (!ble_hs_is_enabled())
    {
        BLE_HS_LOG_INFO("bt_stack close,bt operation invalid.\n");

        return OPRT_OK;
    }

    if (ble_gap_adv_stop() != 0)
    {
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_ble_gap_disconnect(uint16_t conn_handle, uint8_t hci_reason)
{
    OPERATE_RET rt = OPRT_OK;

    if (!ble_hs_is_enabled())
    {
        BLE_HS_LOG_INFO("bt_stack close,bt operation invalid.\n");

        return OPRT_OK;
    }

    TUYA_CALL_ERR_RETURN_VAL(ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM), OPRT_COM_ERROR);

    return OPRT_OK;
}

OPERATE_RET tkl_ble_gatt_callback_register(const TKL_BLE_GATT_EVT_FUNC_CB gatt_evt)
{
    tkl_bluetooth_gatt_callback = gatt_evt;

    return OPRT_OK;
}

static const ble_uuid16_t gatt_svr_svc_uuid =
    BLE_UUID16_INIT(0xFD50);
static const ble_uuid128_t gatt_svr_chr_write_uuid =
    BLE_UUID128_INIT(0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80,
                     0x01, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00);
static const ble_uuid128_t gatt_svr_chr_notify_uuid =
    BLE_UUID128_INIT(0xD0, 0x07, 0x9B, 0x5F, 0x80, 0x00, 0x01, 0x80,
                     0x01, 0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &gatt_svr_svc_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]){
         {
             .uuid = &gatt_svr_chr_write_uuid.u,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
             .access_cb = tuya_ble_host_write_callback,
         },
         {
             .uuid = &gatt_svr_chr_notify_uuid.u,
             .flags = BLE_GATT_CHR_F_NOTIFY,
             .access_cb = tuya_ble_host_write_callback,
         },
         {0}}},
    {0}};

static uint16_t __write_hdl = 0, __notify_hdl = 0;

static void
gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];
    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        TY_LOGD("registered service %s with handle=%d\n",
                ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                ctxt->svc.handle);

        break;

    case BLE_GATT_REGISTER_OP_CHR:
        TY_LOGD("registering characteristic %s with "
                "def_handle=%d val_handle=%d\n",
                ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                ctxt->chr.def_handle,
                ctxt->chr.val_handle);

        if (ctxt->chr.chr_def->uuid->type == BLE_UUID_TYPE_128)
        {
            if (memcmp(ctxt->chr.chr_def->uuid + 1, gatt_svr_chr_write_uuid.value, 16 * sizeof(uint8_t)) == 0)
            {
                TY_LOGD("write handle %d", ctxt->chr.val_handle);

                __write_hdl = ctxt->chr.val_handle;
            }
            if (memcmp(ctxt->chr.chr_def->uuid + 1, gatt_svr_chr_notify_uuid.value, 16 * sizeof(uint8_t)) == 0)
            {
                TY_LOGD("notify handle %d", ctxt->chr.val_handle);

                __notify_hdl = ctxt->chr.val_handle;
            }
        }

        break;

    case BLE_GATT_REGISTER_OP_DSC:
        TY_LOGD("registering descriptor %s with handle=%d\n",
                ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                ctxt->dsc.handle);

        break;

    default:
        assert(0);

        break;
    }
}

OPERATE_RET tkl_ble_gatts_service_add(TKL_BLE_GATTS_PARAMS_T *p_service)
{
    OPERATE_RET rt = OPRT_OK;
    int rc;

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    nimble_port_freertos_init(bleprph_host_task);
    stack_sync_flag = 0;

    int num = 0;

    while ((!stack_sync_flag) && (num++ < 500))
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    TY_LOGD("num:%d", num);

    while (0 == __notify_hdl || 0 == __write_hdl)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    p_service->p_service->p_char[0].handle = __write_hdl;
    p_service->p_service->p_char[1].handle = __notify_hdl;

    return rt;
}

int tuya_ble_hs_notify(uint16_t conn_handle, uint16_t svc_handle, uint8_t *notify_data, uint16_t data_len)
{
    struct os_mbuf *om = NULL;

    om = ble_hs_mbuf_from_flat(notify_data, data_len);
    if (om == NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(50));

        om = ble_hs_mbuf_from_flat(notify_data, data_len);
        if (om == NULL)
        {
            PR_ERR("OM BUF FAIL\r\n");

            return OPRT_COM_ERROR;
        }
    }

    if (om->om_omp->omp_pool->mp_num_free <= 2)
    {
        // Max Data Need Wait
        vTaskDelay(pdMS_TO_TICKS(50));
        PR_ERR("hs_notify wait:%d", om->om_omp->omp_pool->mp_num_free);
    }

    int rc = ble_gattc_notify_custom(conn_handle, svc_handle, om);
    if (rc != 0)
    {
        PR_ERR("HS_NOTIFY ERR:%x", rc);

        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tkl_ble_gatts_value_notify(uint16_t conn_handle, uint16_t char_handle, uint8_t *p_data, uint16_t length)
{
    if (!ble_hs_is_enabled())
    {
        BLE_HS_LOG_INFO("bt_stack close,bt operation invalid.\n");

        return OPRT_OK;
    }

    return tuya_ble_hs_notify(conn_handle, char_handle, p_data, length);
}
