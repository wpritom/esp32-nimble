#include "nvs_flash.h"
#include "esp_log.h"
#include "raven_nimble.h"

#include "driver/gpio.h"

void app_main(void)
{
    // Initialize NVS (Required for storing pairing/bonding keys)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    raven_ble_init();

    // uint8_t INDICATOR_LED = 1;
    // uint8_t BUTTON_PIN = 0;

    // gpio_config_t status_io_conf = {
    //     .pin_bit_mask = (1ULL << INDICATOR_LED),
    //     .mode = GPIO_MODE_OUTPUT,
    //     .pull_up_en = GPIO_PULLUP_DISABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .intr_type = GPIO_INTR_DISABLE};
    
    // gpio_config_t button_io_conf = {
    //     .pin_bit_mask = (1ULL << BUTTON_PIN),
    //     .mode = GPIO_MODE_INPUT,
    //     .pull_up_en = GPIO_PULLUP_ENABLE,
    //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //     .intr_type = GPIO_INTR_DISABLE};


    // gpio_config(&status_io_conf);
    //  gpio_config(&button_io_conf);
    // uint8_t INDICATOR_STATE = 0;

    while (true)
    {
        // printf(" --- BLE NODE RUNNING...\n");
        vTaskDelay(pdMS_TO_TICKS(10));
        // INDICATOR_STATE = !INDICATOR_STATE;
        // gpio_set_level(INDICATOR_LED, INDICATOR_STATE);

        // if (gpio_get_level(BUTTON_PIN) == 0)
        // {
        //     printf("Button is pressed\n");
        // }
        // else
        // {
        //     printf("Button not is pressed\n");
        // }

        if (conn_handle_global != BLE_HS_CONN_HANDLE_NONE)
        {
            // --- Logic for when a Central is connected ---
            printf("Connected to Central! Handle: %d\n", conn_handle_global);
            if (!ble_gap_adv_active())
            {
                ble_app_advertise();
            }
        }
    }
}