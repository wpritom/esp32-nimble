
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"


static uint8_t rw_value[20] = "Hello BLE";


static const char *TAG = "NIMBLE_EX";

static void ble_app_advertise(void);

static int
gatt_svc_access_cb(uint16_t conn_handle,
                   uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt,
                   void *arg)
{
    switch (ctxt->op) {

    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "GATT READ");
        os_mbuf_append(ctxt->om, rw_value, strlen((char *)rw_value));
        return 0;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ESP_LOGI(TAG, "GATT WRITE");
        memset(rw_value, 0, sizeof(rw_value));
        memcpy(rw_value,
               ctxt->om->om_data,
               ctxt->om->om_len);
        rw_value[ctxt->om->om_len] = 0;

        ESP_LOGI(TAG, "Written value: %s", rw_value);
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFF0),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF1),
                .access_cb = gatt_svc_access_cb,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE,
            },
            { 0 } // End of characteristics
        },
    },
    { 0 } // End of services
};



static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    int rc = 0;
    struct ble_gap_conn_desc desc;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            printf("---  BLE_GAP_EVENT_CONNECT Connection status: %d\n", event->connect.status);
            ESP_LOGI(TAG, "Connection %s", event->connect.status == 0 ? "established" : "failed");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            printf("rc = %d\n", rc);
            if (rc != 0) {
                ESP_LOGE(TAG,
                        "failed to find connection by handle, error code: %d",
                        rc);
                return rc;
            }
            if (event->connect.status != 0) {
                ble_app_advertise(); // restart advertising
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            printf("--- Connection status: %d\n", event->connect.status);
            printf("--- Reason: %d\n", event->disconnect.reason);
            ESP_LOGI(TAG, "Disconnected; restarting advertising");
            ble_app_advertise();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            printf("--- Connection status: %d\n", event->connect.status);
            ESP_LOGI(TAG, "Advertising complete; restarting");
            ble_app_advertise();
            break;

        default:
            break;
    }
    return 0;
}

static void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    memset(&fields, 0, sizeof(fields));

    // Advertise flags
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Device name
    const char *name = "ESP32_NIMBLE";
    fields.tx_pwr_lvl_is_present = 1;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(
        BLE_OWN_ADDR_PUBLIC,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        ble_gap_event,
        NULL
    );

    ESP_LOGI(TAG, "Advertising started");
}

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void) {

     /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nimble_port_init();

    // --- ADD THESE THREE LINES ---
    ble_svc_gap_init();                    // Initialize the GAP service
    ble_svc_gatt_init();                   // Initialize the GATT service
    ble_svc_gap_device_name_set("ESP32_NIMBLE"); // Set the internal name

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_app_advertise;

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE NimBLE initialized");
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        printf("--- Program is running !!!\n");
    }
}
