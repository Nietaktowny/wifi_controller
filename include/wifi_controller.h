#pragma once

#include <stdbool.h>
#include "esp_wifi.h"

/**
 * @brief Types of available WiFi modes.
 * 
 */
typedef enum {
    WIFI_C_MODE_STA,        /*Use WiFi as STA.*/
    WIFI_C_MODE_AP,         /*Use WiFi as AP.*/
    WIFI_C_MODE_APSTA,      /*Use WiFi as AP+STA.*/
    WIFI_C_NO_MODE          /*No mode currently set.*/
} wifi_c_mode_t;

/**
 * @brief Object showing and maintaining current status of wifi_controller.
 * 
 */
struct wifi_c_status_obj {
    bool wifi_initialized;
    bool netif_initialized;
    wifi_c_mode_t wifi_mode;
    bool even_loop_started;
    bool wifi_started;
    bool sta_started;
    bool ap_started;
    bool scan_done;
    bool sta_connected;
};

/**
 * @brief Type of wifi_controller status object.
 * 
 */
typedef struct wifi_c_status_obj wifi_c_status_t;

/**
 * @brief Object representing scan configuration and results.
 * 
 */
struct wifi_c_scan_result_obj {
    wifi_ap_record_t* ap_record;
    uint16_t ap_count;
};

/**
 * @brief Type of scan results object.
 * 
 */
typedef struct wifi_c_scan_result_obj wifi_c_scan_result_t;

/**
 * @brief Definitions of error codes for wifi_controller.
 * 
 */
#define WIFI_C_ERR_BASE                 0x00FF                      ///< Error base used to indicate where wifi_controller errors numbers start.
#define WIFI_C_ERR_NULL_SSID            WIFI_C_ERR_BASE + 0x01      ///< SSID for WiFi was null or zero length.
#define WIFI_C_ERR_WRONG_MODE           WIFI_C_ERR_BASE + 0x02      ///< Mode type of WiFI was wrong.
#define WIFI_C_ERR_NETIF_INIT_FAILED    WIFI_C_ERR_BASE + 0x03      ///< Failed to initialize netif - see wifi_c_init_netif() (CRITICAL).
#define WIFI_C_ERR_WIFI_ALREADY_INIT    WIFI_C_ERR_BASE + 0x04      ///< WiFi was already initialized once.
#define WIFI_C_ERR_NETIF_ALREADY_INIT   WIFI_C_ERR_BASE + 0x05      ///< Netif is already initialized.
#define WIFI_C_ERR_WRONG_PASSWORD       WIFI_C_ERR_BASE + 0x06      ///< Password lenght is too short for WIFI_AUTH_WPA2_PSK (need at least 8 characters).
#define WIFI_C_ERR_WIFI_NOT_STARTED     WIFI_C_ERR_BASE + 0x07      ///< Wifi was not started.
#define WIFI_C_ERR_WIFI_NOT_INIT        WIFI_C_ERR_BASE + 0x08      ///< Wifi was not initialized.
#define WIFI_C_ERR_SCAN_NOT_DONE        WIFI_C_ERR_BASE + 0x09      ///< Trying to read scan results without prior scanning.
#define WIFI_C_ERR_STA_NOT_STARTED      WIFI_C_ERR_BASE + 0x0A      ///< Trying to scan without configuring nad starting STA.
#define WIFI_C_AP_NOT_FOUND             WIFI_C_ERR_BASE + 0x0B      ///< Not found desired AP when scanning.

#define WIFI_C_STA_RETRY_COUNT          4                           ///< Number of times to try to connect to AP as STA.
#define WIFI_C_DEFAULT_SCAN_SIZE        10                          ///< Number of APs to store when scanning.

#define WIFI_C_CONNECTED_BIT            0x00000001
#define WIFI_C_CONNECT_FAIL_BIT         0x00000002
#define WIFI_C_SCAN_DONE_BIT            0x00000004
#define WIFI_C_STA_STARTED_BIT          0x00000008

#define WIFI_C_SCAN_BLOCK               false

/**
 * @brief Used to initialize and prepare Wifi to work.
 * 
 * @param WIFI_C_WIFI_MODE Mode in which WiFi will work.
 * @return
 *          - ERR_C_OK on success
 *          - WIFI_C_ERR_WIFI_ALREADY_INIT if already initialized
 *          - esp specific errors
 */
int wifi_c_init_wifi(wifi_c_mode_t WIFI_C_WIFI_MODE);

/**
 * @brief Starts WiFi in softAP mode.
 * 
 * @attention
 * Passed SSID of AP cannot be zero length otherwise it will throw error.
 * 
 * @note
 * When passed password length is zero, the auth mode is set to open.
 * 
 * @param ssid          SSID of AP.
 * @param password      Password of AP.
 * @return
 *          - ERR_C_OK on success
 *          - WIFI_C_ERR_NULL_SSID if passed ssid was null or zero length
 *          - ERR_C_MEMORY_ERR if memcpy of password/ssid was not successfull
 *          - esp specific error codes
 *          
 */
int wifi_c_start_ap(const char* ssid, const char* password);

/**
 * @brief Starts WiFi in STA mode.
 * 
 * @param ssid          SSID of AP to connect to as station.
 * @param password      password of AP to connect to as station.
 * @return
 *          - ERR_C_OK on success
 *          - WIFI_C_ERR_NULL_SSID if passed ssid was null or zero length
 *          - ERR_C_MEMORY_ERR if memcpy of password/ssid was not successfull
 *          - esp specific error codes
 */
int wifi_c_start_sta(const char* ssid, const char* password);

/**
 * @brief Get current wifi_controller status.
 * 
 * @return wifi_status_t* Pointer to wifi_controller status struct.
 */
const wifi_c_status_t *wifi_c_get_status(void);

/**
 * @brief Initializes default event loop and sets callback functions.
 * 
 * @return
 *          - ERR_C_OK on success
 *          - esp specific error codes
 */
int wifi_c_create_default_event_loop(void);

/**
 * @brief Scan for AP on all channels.
 * 
 * @return wifi_c_scan_result_t* Pointer to scan results struct.
 */
wifi_c_scan_result_t* wifi_c_scan_all_ap(void);

/**
 * @brief Scan for AP with desired SSID.
 * 
 * @param searched_ssid SSID of AP to search for.
 * @param ap_record     Pointer to wifi_ap_record_t to store result.
 * @return
 *          - ERR_C_OK on success
 *          - WIFI_C_AP_NOT_FOUND when not found AP
 *          - esp specific error codes
 */
int wifi_c_scan_for_ap_with_ssid(const char* searched_ssid, wifi_ap_record_t* ap_record);

/**
 * @brief Log results of Wifi scan.
 * 
 * @note It should be called only after scanning is done.
 * 
 */
int wifi_c_print_scanned_ap (void);