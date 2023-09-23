#include "nvs.h"
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

    /*STA example*/

    //Init Wifi
    wifi_c_init_wifi(WIFI_C_MODE_STA);
    //Start STA and connect to AP:
    wifi_c_start_sta("SSID", "PASSWORD");

}