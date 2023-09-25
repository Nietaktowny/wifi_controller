#include "nvs_flash.h"
#include "esp_err.h"
#include "wifi_controller.h"

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    /*AP+STA example*/

    //Init Wifi
    ESP_ERROR_CHECK(wifi_c_init_wifi(WIFI_C_MODE_APSTA));
    //Start STA and connect to AP:
    ESP_ERROR_CHECK(wifi_c_start_sta("STA_SSID", "STA_PASSWORD"));
    //Start AP with passed credentials:
    ESP_ERROR_CHECK(wifi_c_start_ap("AP_SSID", "AP_PASSWORD"));
}