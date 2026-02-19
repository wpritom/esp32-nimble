#include "nvs_flash.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "store/config/ble_store_config.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static uint8_t rw_value[20] = "Hello BLE";
static const char *TAG = "NIMBLE_EX";

static void ble_app_advertise(void);
void ble_store_config_init(void);

// handle notification / data sending
static uint16_t notify_handle;
static uint16_t conn_handle_global = BLE_HS_CONN_HANDLE_NONE;


// 1. Access Callback: Handles Read/Write operations
static int gatt_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            ESP_LOGI(TAG, "GATT READ Triggered");
            os_mbuf_append(ctxt->om, rw_value, strlen((char *)rw_value));
            return 0;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            ESP_LOGI(TAG, "GATT WRITE Triggered");
            memset(rw_value, 0, sizeof(rw_value));
            memcpy(rw_value, ctxt->om->om_data, ctxt->om->om_len);
            rw_value[ctxt->om->om_len] = 0;
            ESP_LOGI(TAG, "New value: %s", rw_value);
            return 0;

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

// 2. Service Definition: Using _ENC flags to force Bonding
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFF0),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF1),
                .access_cb = gatt_svc_access_cb,
                .val_handle = &notify_handle,
                // These flags force the "Just Works" pairing popup
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 } 
        },
    },
    { 0 }
};

// 3. GAP Event Handler: Managing the Connection
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            conn_handle_global = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected");
        }
        else
        {
            ble_app_advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        conn_handle_global = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "Disconnected");
        ble_app_advertise();
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        // This confirms that bonding/encryption is successful
        ESP_LOGI(TAG, "Encryption status changed; status=%d", event->enc_change.status);
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "Passkey action required");
        break;
        
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_app_advertise();
        break;

    default:
        break;
    }
    return 0;
}
static void notify_task(void *param)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (conn_handle_global == BLE_HS_CONN_HANDLE_NONE) {
            continue; // not connected
        }

        char msg[20];
        snprintf(msg, sizeof(msg), "Tick %lu", esp_log_timestamp());

        struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
        if (!om) {
            ESP_LOGE(TAG, "Failed to allocate mbuf");
            continue;
        }

        int rc = ble_gatts_notify_custom(
            conn_handle_global,
            notify_handle,
            om
        );

        if (rc == 0) {
            ESP_LOGI(TAG, "Notification sent: %s", msg);
        } else {
            ESP_LOGE(TAG, "Notify failed: %d", rc);
        }
    }
}


// 4. Advertising Configuration
static void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    

    const char *name = "MEEWT";
    uint8_t mfg_data[4] = {0xe9, 0x14, 0x0f, 0x1}; // Manufacturer specific data

    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.mfg_data = mfg_data;
    fields.mfg_data_len = sizeof(mfg_data);

    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // BLE_GAP_CONN_MODE_NON (to avoid connection attepts)
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
    ESP_LOGI(TAG, "Advertising started...");
}

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void raven_ble_init(void) {
    nimble_port_init();
    

    // Initialize GAP and GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("MEEWT");

    // Security Configuration for "Just Works" Pairing
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; // No PIN entry required
    ble_hs_cfg.sm_bonding = 1;                        // Save keys to flash
    ble_hs_cfg.sm_mitm = 0;                           // MITM must be 0 for Just Works
    ble_hs_cfg.sm_sc = 1;                             // Use LE Secure Connections

    ble_store_config_init();

    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_app_advertise;
    nimble_port_freertos_init(ble_host_task);
    

    ESP_LOGI(TAG, "NimBLE Stack Initialized");
    xTaskCreate(notify_task, "notify", 4096, NULL, 5, NULL);
}



void app_main(void) {
    // Initialize NVS (Required for storing pairing/bonding keys)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    raven_ble_init();
    while (true)
    {
        printf(" --- BLE NODE RUNNING...\n");
        vTaskDelay(pdMS_TO_TICKS(5000));


        if (conn_handle_global != BLE_HS_CONN_HANDLE_NONE) {
            // --- Logic for when a Central is connected ---
            printf("Connected to Central! Handle: %d\n", conn_handle_global);
            ble_app_advertise();
            
            // Example: Perform a GATT notification here
        }

    }
    

}