#include "esp_event.h"
#include "esp_log.h"
#include "esp_psram.h"

#include "asic_result_task.h"
#include "asic_task.h"
#include "create_jobs_task.h"
#include "hashrate_monitor_task.h"
#include "statistics_task.h"
#include "system.h"
#include "http_server.h"
#include "serial.h"
#include "stratum_task.h"
#include "i2c_bitaxe.h"
#include "adc.h"
#include "nvs_config.h"
#include "self_test.h"
#include "asic.h"
#include "bap/bap.h"
#include "device_config.h"
#include "connect.h"
#include "asic_reset.h"
#include "asic_init.h"

static GlobalState GLOBAL_STATE;

static const char * TAG = "bitaxe";

void app_main(void)
{
    ESP_LOGI(TAG, "Welcome to the bitaxe - FOSS || GTFO!");

    if (!esp_psram_is_initialized()) {
        ESP_LOGE(TAG, "No PSRAM available on ESP32 device!");
        GLOBAL_STATE.psram_is_available = false;
        ESP_LOGW(TAG, "******************************************");
        ESP_LOGW(TAG, "***   LOW MEMORY MODE ACTIVE          ***");
        ESP_LOGW(TAG, "***   Some features will be disabled  ***");
        ESP_LOGW(TAG, "******************************************");
    } else {
        GLOBAL_STATE.psram_is_available = true;
        ESP_LOGI(TAG, "PSRAM detected and initialized");
    }

    // Init I2C
    ESP_ERROR_CHECK(i2c_bitaxe_init());
    ESP_LOGI(TAG, "I2C initialized successfully");
    
    // Initialize RST pin to low early to minimize ASIC power consumption
    ESP_ERROR_CHECK(asic_hold_reset_low());
    ESP_LOGI(TAG, "RST pin initialized to low");

    //wait for I2C to init
    vTaskDelay(100 / portTICK_PERIOD_MS);

    //Init ADC
    ADC_init();

    //initialize the ESP32 NVS
    if (nvs_config_init() != ESP_OK){
        ESP_LOGE(TAG, "Failed to init NVS");
        return;
    }

    if (device_config_init(&GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init device config");
        return;
    }

    if (self_test(&GLOBAL_STATE)) return;

    SYSTEM_init_system(&GLOBAL_STATE);

    // init AP and connect to wifi
    wifi_init(&GLOBAL_STATE);

    if (SYSTEM_init_peripherals(&GLOBAL_STATE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init peripherals");
        return;
    }

    if (xTaskCreate(POWER_MANAGEMENT_task, "power management", 8192, (void *) &GLOBAL_STATE, 10, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creating power management task");
    }

    //start the API for AxeOS
    start_rest_server((void *) &GLOBAL_STATE);

    // Initialize BAP interface (only if PSRAM available)
    if (GLOBAL_STATE.psram_is_available) {
        esp_err_t bap_ret = BAP_init(&GLOBAL_STATE);
        if (bap_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize BAP interface: %d", bap_ret);
            // Continue anyway, as BAP is not critical for core functionality
        }
    } else {
        ESP_LOGI(TAG, "Skipping BAP init - low memory mode");
    }

    while (!GLOBAL_STATE.SYSTEM_MODULE.is_connected) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    queue_init(&GLOBAL_STATE.stratum_queue);
    queue_init(&GLOBAL_STATE.ASIC_jobs_queue);

    if (asic_initialize(&GLOBAL_STATE, ASIC_INIT_COLD_BOOT, 0) == 0) {
        return;
    }

    if (xTaskCreate(stratum_task, "stratum admin", 8192, (void *) &GLOBAL_STATE, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creating stratum admin task");
    }
    if (xTaskCreate(create_jobs_task, "stratum miner", 8192, (void *) &GLOBAL_STATE, 10, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creating stratum miner task");
    }
    if (xTaskCreate(ASIC_task, "asic", 8192, (void *) &GLOBAL_STATE, 10, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creating asic task");
    }
    if (xTaskCreate(ASIC_result_task, "asic result", 8192, (void *) &GLOBAL_STATE, 15, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Error creating asic result task");
    }

    // Create hashrate monitor task (critical for display) - use appropriate memory
    uint32_t task_mem_caps = GLOBAL_STATE.psram_is_available ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    if (xTaskCreateWithCaps(hashrate_monitor_task, "hashrate monitor", 8192, (void *) &GLOBAL_STATE, 5, NULL, task_mem_caps) != pdPASS) {
        ESP_LOGE(TAG, "Error creating hashrate monitor task");
    }

    // Only create statistics task if PSRAM is available (not critical for basic operation)
    if (GLOBAL_STATE.psram_is_available) {
        if (xTaskCreateWithCaps(statistics_task, "statistics", 8192, (void *) &GLOBAL_STATE, 3, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
            ESP_LOGE(TAG, "Error creating statistics task");
        }
    } else {
        ESP_LOGW(TAG, "Skipping statistics task - low memory mode");
        ESP_LOGW(TAG, "Hashrate monitor active, display will work normally");
    }
}
