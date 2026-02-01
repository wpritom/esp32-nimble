#include "nvs_flash.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static uint8_t rw_value[20] = "Hello BLE";
static const char *TAG = "NIMBLE_EX";

static void ble_app_advertise(void);

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
                // These flags force the "Just Works" pairing popup
                .flags = BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC,
            },
            { 0 } 
        },
    },
    { 0 }
};

// 3. GAP Event Handler: Managing the Connection
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "Connection %s", event->connect.status == 0 ? "established" : "failed");
            if (event->connect.status != 0) {
                ble_app_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected; restarting advertising. Reason: %d", event->disconnect.reason);
            ble_app_advertise();
            break;

        case BLE_GAP_EVENT_ENC_CHANGE:
            // This confirms that bonding/encryption is successful
            ESP_LOGI(TAG, "Encryption status changed; status=%d", event->enc_change.status);
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ble_app_advertise();
            break;

        default:
            break;
    }
    return 0;
}

// 4. Advertising Configuration
static void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    const char *name = "ESP32_NIMBLE";
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
    ESP_LOGI(TAG, "Advertising started...");
}

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// 5. Main Application Entry
void app_main(void) {
    // Initialize NVS (Required for storing pairing/bonding keys)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nimble_port_init();

    // Initialize GAP and GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("ESP32_NIMBLE");

    // Security Configuration for "Just Works" Pairing
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; // No PIN entry required
    ble_hs_cfg.sm_bonding = 1;                        // Save keys to flash
    ble_hs_cfg.sm_mitm = 0;                           // MITM must be 0 for Just Works
    ble_hs_cfg.sm_sc = 1;                             // Use LE Secure Connections

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_app_advertise;
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "NimBLE Stack Initialized");
}