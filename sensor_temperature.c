/*
* Copyright 2016-2022, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*/

/** @file
 *
 * This demo application shows a  implementation of a temperature sensor.
 * The app is based on the snip/mesh/mesh_sensor_server sample which
 * implements LE Mesh Sensor Server model.
 *
 * Features demonstrated
 *  - Temperature measurement using the on board Thermistor on the EVK
 *
 * To demonstrate the app, walk through the following steps.
 * 1. Build and download the application (to the WICED board)
 * 2. Use Android MeshController and provision the temperature sensor
 * 3. After successful provisioning, user can use the Android MeshController/Mesh Client to configure the below parameters of the sensor
 *    a> configure sensor to publish the sensor data to a specific group or to all-nodes.
 *    b> configure publish period : publish period defines how often the user wants the sensor to publish the data.
 *    c> set cadence of the sensor :
 *       set minimum interval in which sensor data has to be published.
 *       set the range in which the fast cadence has to be observed.
 *       set the fast cadence period (how fast the data has to be published with respect to publish period).
 *       set the unit in which if the values change the data should be published and trigger type (Native or percentage).
 *           example : publish data if the data changes by 2 units/10%
 * 4. To change the temperature on the thermistor, you can keep your finger on the sensor and see the changes.
 */
#include "wiced_bt_uuid.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_mesh_models.h"
#include "wiced_bt_trace.h"
#include "wiced_timer.h"
#include "wiced_bt_mesh_app.h"
#include "wiced_thermistor.h"
#include "wiced_hal_nvram.h"
#include "wiced_sleep.h"
#include "wiced_hal_adc.h"
#include "wiced_platform.h"

#include "wiced_bt_cfg.h"
extern wiced_bt_cfg_settings_t wiced_bt_cfg_settings;

/******************************************************
 *          Constants
 ******************************************************/
#define MESH_PID                0x3122
#define MESH_VID                0x0002

#define MESH_TEMP_SENSOR_PROPERTY_ID                    WICED_BT_MESH_PROPERTY_PRESENT_AMBIENT_TEMPERATURE
#define MESH_TEMP_SENSOR_VALUE_LEN                      WICED_BT_MESH_PROPERTY_LEN_PRESENT_AMBIENT_TEMPERATURE

// The onboard thermistor hardware has a positive and negative tolerance of 1%
#define MESH_TEMPERATURE_SENSOR_POSITIVE_TOLERANCE      CONVERT_TOLERANCE_PERCENTAGE_TO_MESH(1)
#define MESH_TEMPERATURE_SENSOR_NEGATIVE_TOLERANCE      CONVERT_TOLERANCE_PERCENTAGE_TO_MESH(1)

#define MESH_TEMPERATURE_SENSOR_SAMPLING_FUNCTION       WICED_BT_MESH_SENSOR_SAMPLING_FUNCTION_UNKNOWN
#define MESH_TEMPERATURE_SENSOR_MEASUREMENT_PERIOD      WICED_BT_MESH_SENSOR_VAL_UNKNOWN
#define MESH_TEMPERATURE_SENSOR_UPDATE_INTERVAL         WICED_BT_MESH_SENSOR_VAL_UNKNOWN

#define MESH_TEMPERATURE_SENSOR_CADENCE_NVRAM_ID        WICED_NVRAM_VSID_START

/******************************************************************************
 *                                Constants
 ******************************************************************************/
#ifndef THERMISTOR_aux_0_TRIGGER_OUT

#ifdef CYW20719B1
#define THERMISTOR_PIN  ADC_INPUT_P10                       /*CYW920719Q40EVB-01 has P10 connected to Thermistor*/
#endif

#ifdef CYW20835B1
#define THERMISTOR_PIN  ADC_INPUT_P8                        /*CYW920835M2EVB-01 has P8 connected to Thermistor*/
#endif

#ifdef CYW20819A1
#define THERMISTOR_PIN  ADC_INPUT_P8                        /*CYW920819EVB-02 has P8 connected to Thermistor*/
#endif

#ifdef CYW20820A1
#define THERMISTOR_PIN  ADC_INPUT_P8                        /*CYW920820EVB-02 has P8 connected to Thermistor*/
#endif

#ifdef CYW20719B2
#define THERMISTOR_PIN  ADC_INPUT_P10                        /*CYW920719B2Q40EVB-01 has P10 connected to Thermistor*/
#endif

#else

#define THERMISTOR_PIN THERMISTOR_aux_0_TRIGGER_OUT

#endif
/******************************************************
 *          Structures
 ******************************************************/

/******************************************************
 *          Function Prototypes
 ******************************************************/
static void         mesh_app_init(wiced_bool_t is_provisioned);
static wiced_bool_t mesh_app_notify_period_set(uint8_t element_idx, uint16_t company_id, uint16_t model_id, uint32_t period);
static void         mesh_app_lpn_sleep(uint32_t timeout);
static void         mesh_app_factory_reset(void);
static void         mesh_sensor_server_restart_timer(wiced_bt_mesh_core_config_sensor_t *p_sensor);
static void         mesh_sensor_server_report_handler(uint16_t event, uint8_t element_idx, void *p_get_data, void *p_ref_data);
static void         mesh_sensor_server_config_change_handler(uint8_t element_idx, uint16_t event, void* p_data);
static void         mesh_sensor_server_process_cadence_changed(uint8_t element_idx, wiced_bt_mesh_sensor_cadence_status_data_t* p_data);
static void         mesh_sensor_server_process_setting_changed(uint8_t element_idx, wiced_bt_mesh_sensor_setting_status_data_t* p_data);
static int8_t       mesh_sensor_get_temperature_8(void);
static void         mesh_sensor_publish_timer_callback(TIMER_PARAM_TYPE arg);
static void         mesh_sensor_server_enter_hid_off(uint32_t timeout_ms);


/******************************************************
 *          Variables Definitions
 ******************************************************/
uint8_t mesh_mfr_name[WICED_BT_MESH_PROPERTY_LEN_DEVICE_MANUFACTURER_NAME]          = { 'C', 'y', 'p', 'r', 'e', 's', 's', 0 };
uint8_t mesh_model_num[WICED_BT_MESH_PROPERTY_LEN_DEVICE_MODEL_NUMBER]              = { '1', '2', '3', '4', 0, 0, 0, 0 };
uint8_t mesh_prop_fw_version[WICED_BT_MESH_PROPERTY_LEN_DEVICE_FIRMWARE_REVISION] =   { '0', '6', '.', '0', '2', '.', '0', '5' }; // this is overwritten during init
uint8_t mesh_system_id[8]                                                           = { 0xbb, 0xb8, 0xa1, 0x80, 0x5f, 0x9f, 0x91, 0x71 };

// Present Ambient Temperature property uses Temperature 8 format, i.e. 0.5 degree Celsius.
int8_t        mesh_sensor_current_value = 42;           // 21 degree Celsius
int8_t        mesh_sensor_sent_value = 0;               // Value sent as a result of publication or GET
int8_t        mesh_sensor_pub_value = 0;                // Value sent as a result of publication
uint32_t      mesh_sensor_pub_time;                     // time stamp when temperature was published
uint32_t      mesh_sensor_publish_period = 0;           // publish period in msec
uint32_t      mesh_sensor_fast_publish_period = 0;      // publish period in msec when values are outside of limit
uint32_t      mesh_sensor_measure_min_interval = 3000;  // Measure temperature at least every 3 seconds
wiced_timer_t mesh_sensor_cadence_timer;

// Optional setting for the temperature sensor, the Total Device Runtime, in Time Hour 24 format
uint8_t mesh_temperature_sensor_setting0_val[] = { 0x01, 0x00, 0x00 };

wiced_bt_mesh_core_config_model_t mesh_element1_models[] =
{
    WICED_BT_MESH_DEVICE,
    WICED_BT_MESH_MODEL_SENSOR_SERVER,
};
#define MESH_APP_NUM_MODELS  (sizeof(mesh_element1_models) / sizeof(wiced_bt_mesh_core_config_model_t))

wiced_bt_mesh_sensor_config_setting_t sensor_settings[] =
{
    {
        .setting_property_id = WICED_BT_MESH_PROPERTY_TOTAL_DEVICE_RUNTIME,
        .access              = WICED_BT_MESH_SENSOR_SETTING_READABLE_AND_WRITABLE,
        .value_len           = WICED_BT_MESH_PROPERTY_LEN_TOTAL_DEVICE_RUNTIME,
        .val                 = mesh_temperature_sensor_setting0_val
    },
};

wiced_bt_mesh_core_config_sensor_t mesh_element1_sensors[] =
{
    {
        .property_id = WICED_BT_MESH_PROPERTY_PRESENT_AMBIENT_TEMPERATURE,
        .prop_value_len = WICED_BT_MESH_PROPERTY_LEN_PRESENT_AMBIENT_TEMPERATURE,
        .descriptor =
        {
            .positive_tolerance = MESH_TEMPERATURE_SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = MESH_TEMPERATURE_SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function  = MESH_TEMPERATURE_SENSOR_SAMPLING_FUNCTION,
            .measurement_period = MESH_TEMPERATURE_SENSOR_MEASUREMENT_PERIOD,
            .update_interval    = MESH_TEMPERATURE_SENSOR_UPDATE_INTERVAL,
        },
        .data = (uint8_t*)&mesh_sensor_sent_value,
        .cadence =
        {
            // Value 1 indicates that cadence does not change depending on the measurements
            .fast_cadence_period_divisor = 1,
            .trigger_type_percentage     = WICED_FALSE,
            .trigger_delta_down          = 0,
            .trigger_delta_up            = 0,
            .min_interval                = (1 << 12), // minimum interval for sending data by default is 4 seconds
            .fast_cadence_low            = 0,
            .fast_cadence_high           = 0,
        },
        .num_series     = 0,
        .series_columns = NULL,
        .num_settings   = 1,
        .settings       = sensor_settings,
    },
};


#define MESH_APP_NUM_PROPERTIES (sizeof(mesh_element1_properties) / sizeof(wiced_bt_mesh_core_config_property_t))

#define MESH_SENSOR_SERVER_ELEMENT_INDEX   0
#define MESH_TEMPERATURE_SENSOR_INDEX      0

wiced_bt_mesh_core_config_element_t mesh_elements[] =
{
    {
        .location = MESH_ELEM_LOC_MAIN,                                  // location description as defined in the GATT Bluetooth Namespace Descriptors section of the Bluetooth SIG Assigned Numbers
        .default_transition_time = MESH_DEFAULT_TRANSITION_TIME_IN_MS,   // Default transition time for models of the element in milliseconds
        .onpowerup_state = WICED_BT_MESH_ON_POWER_UP_STATE_RESTORE,      // Default element behavior on power up
        .default_level = 0,                                              // Default value of the variable controlled on this element (for example power, lightness, temperature, hue...)
        .range_min = 1,                                                  // Minimum value of the variable controlled on this element (for example power, lightness, temperature, hue...)
        .range_max = 0xffff,                                             // Maximum value of the variable controlled on this element (for example power, lightness, temperature, hue...)
        .move_rollover = 0,                                              // If true when level gets to range_max during move operation, it switches to min, otherwise move stops.
        .properties_num = 0,                                             // Number of properties in the array models
        .properties = NULL,                                              // Array of properties in the element.
        .sensors_num = 1,                                                // Number of properties in the array models
        .sensors = mesh_element1_sensors,                                // Array of properties in the element.
        .models_num = MESH_APP_NUM_MODELS,                               // Number of models in the array models
        .models = mesh_element1_models,                                  // Array of models located in that element. Model data is defined by structure wiced_bt_mesh_core_config_model_t
    },
};

wiced_bt_mesh_core_config_t  mesh_config =
{
    .company_id         = MESH_COMPANY_ID_CYPRESS,                  // Company identifier assigned by the Bluetooth SIG
    .product_id         = MESH_PID,                                 // Vendor-assigned product identifier
    .vendor_id          = MESH_VID,                                 // Vendor-assigned product version identifier
#if defined(LOW_POWER_NODE) && (LOW_POWER_NODE == 1)
    .features           = WICED_BT_MESH_CORE_FEATURE_BIT_LOW_POWER, // A bit field indicating the device features. In Low Power mode no Relay, no Proxy and no Friend
    .friend_cfg         =                                           // Empty Configuration of the Friend Feature
    {
        .receive_window = 0,                                        // Receive Window value in milliseconds supported by the Friend node.
        .cache_buf_len  = 0,                                        // Length of the buffer for the cache
        .max_lpn_num    = 0                                         // Max number of Low Power Nodes with established friendship. Must be > 0 if Friend feature is supported.
    },
    .low_power          =                                           // Configuration of the Low Power Feature
    {
        .rssi_factor           = 2,                                 // contribution of the RSSI measured by the Friend node used in Friend Offer Delay calculations.
        .receive_window_factor = 2,                                 // contribution of the supported Receive Window used in Friend Offer Delay calculations.
        .min_cache_size_log    = 3,                                 // minimum number of messages that the Friend node can store in its Friend Cache.
        .receive_delay         = 100,                               // Receive delay in 1 ms units to be requested by the Low Power node.
        .poll_timeout          = 36000                              // Poll timeout in 100ms units to be requested by the Low Power node.
    },
#else
    .features           = WICED_BT_MESH_CORE_FEATURE_BIT_FRIEND | WICED_BT_MESH_CORE_FEATURE_BIT_RELAY | WICED_BT_MESH_CORE_FEATURE_BIT_GATT_PROXY_SERVER,   // In Friend mode support friend, relay
    .friend_cfg         =                                           // Configuration of the Friend Feature(Receive Window in Ms, messages cache)
    {
        .receive_window        = 20,
        .cache_buf_len         = 300,                               // Length of the buffer for the cache
        .max_lpn_num           = 4                                  // Max number of Low Power Nodes with established friendship. Must be > 0 if Friend feature is supported.
    },
    .low_power          =                                           // Configuration of the Low Power Feature
    {
        .rssi_factor           = 0,                                 // contribution of the RSSI measured by the Friend node used in Friend Offer Delay calculations.
        .receive_window_factor = 0,                                 // contribution of the supported Receive Window used in Friend Offer Delay calculations.
        .min_cache_size_log    = 0,                                 // minimum number of messages that the Friend node can store in its Friend Cache.
        .receive_delay         = 0,                                 // Receive delay in 1 ms units to be requested by the Low Power node.
        .poll_timeout          = 0                                  // Poll timeout in 100ms units to be requested by the Low Power node.
    },
#endif
    .gatt_client_only          = WICED_FALSE,                       // Can connect to mesh over GATT or ADV
    .elements_num  = (uint8_t)(sizeof(mesh_elements) / sizeof(mesh_elements[0])),   // number of elements on this device
    .elements      = mesh_elements                                  // Array of elements for this device
};

/*
 * Mesh application library will call into application functions if provided by the application.
 */
wiced_bt_mesh_app_func_table_t wiced_bt_mesh_app_func_table =
{
    mesh_app_init,                  // application initialization
    NULL,                           // Default SDK platform button processing
    NULL,                           // GATT connection status
    NULL,                           // attention processing
    mesh_app_notify_period_set,     // notify period set
    NULL,                           // WICED HCI command
    mesh_app_lpn_sleep,             // LPN sleep
    mesh_app_factory_reset,         // factory reset
};

 /******************************************************
 *               Function Definitions
 ******************************************************/
void mesh_app_init(wiced_bool_t is_provisioned)
{
#if 0
    // Set Debug trace level for mesh_models_lib and mesh_provisioner_lib
    wiced_bt_mesh_models_set_trace_level(WICED_BT_MESH_CORE_TRACE_INFO);
#endif
#if 0
    // Set Debug trace level for all modules but Info level for CORE_AES_CCM module
    wiced_bt_mesh_core_set_trace_level(WICED_BT_MESH_CORE_TRACE_FID_ALL, WICED_BT_MESH_CORE_TRACE_DEBUG);
    wiced_bt_mesh_core_set_trace_level(WICED_BT_MESH_CORE_TRACE_FID_CORE_AES_CCM, WICED_BT_MESH_CORE_TRACE_INFO);
#endif
    uint32_t        cur_time = wiced_bt_mesh_core_get_tick_count();
    wiced_result_t  result;
    wiced_bt_mesh_core_config_sensor_t *p_sensor;

    wiced_bt_cfg_settings.device_name = (uint8_t *)"Temperature Sensor";
    wiced_bt_cfg_settings.gatt_cfg.appearance = APPEARANCE_SENSOR_TEMPERATURE;

    mesh_prop_fw_version[0] = 0x30 + (WICED_SDK_MAJOR_VER / 10);
    mesh_prop_fw_version[1] = 0x30 + (WICED_SDK_MAJOR_VER % 10);
    mesh_prop_fw_version[2] = 0x30 + (WICED_SDK_MINOR_VER / 10);
    mesh_prop_fw_version[3] = 0x30 + (WICED_SDK_MINOR_VER % 10);
    mesh_prop_fw_version[4] = 0x30 + (WICED_SDK_REV_NUMBER / 10);
    mesh_prop_fw_version[5] = 0x30 + (WICED_SDK_REV_NUMBER % 10);
    // convert 12 bits of BUILD_NUMMBER to two base64 characters big endian
    mesh_prop_fw_version[6] = wiced_bt_mesh_base64_encode_6bits((uint8_t)(WICED_SDK_BUILD_NUMBER >> 6) & 0x3f);
    mesh_prop_fw_version[7] = wiced_bt_mesh_base64_encode_6bits((uint8_t)WICED_SDK_BUILD_NUMBER & 0x3f);

    WICED_BT_TRACE("Temp App Init provisioned:$D\n", is_provisioned);

    // Adv Data is fixed. Spec allows to put URI, Name, Appearance and Tx Power in the Scan Response Data.
    if (!is_provisioned)
    {
        wiced_bt_ble_advert_elem_t  adv_elem[3];
        uint8_t                     buf[2];
        uint8_t                     num_elem = 0;

        adv_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE;
        adv_elem[num_elem].len         = (uint16_t)strlen((const char*)wiced_bt_cfg_settings.device_name);
        adv_elem[num_elem].p_data      = wiced_bt_cfg_settings.device_name;
        num_elem++;

        adv_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_APPEARANCE;
        adv_elem[num_elem].len         = 2;
        buf[0]                         = (uint8_t)wiced_bt_cfg_settings.gatt_cfg.appearance;
        buf[1]                         = (uint8_t)(wiced_bt_cfg_settings.gatt_cfg.appearance >> 8);
        adv_elem[num_elem].p_data      = buf;
        num_elem++;

        wiced_bt_mesh_set_raw_scan_response_data(num_elem, adv_elem);

        wiced_bt_mesh_model_sensor_server_init(MESH_SENSOR_SERVER_ELEMENT_INDEX, mesh_sensor_server_report_handler, mesh_sensor_server_config_change_handler, is_provisioned);
        return;
    }

    p_sensor = &mesh_config.elements[MESH_SENSOR_SERVER_ELEMENT_INDEX].sensors[MESH_TEMPERATURE_SENSOR_INDEX];

    // When we are coming out of HID OFF and if we are provisioned, need to send data
    thermistor_init();

    // read the initial temperature
    mesh_sensor_current_value = mesh_sensor_get_temperature_8();

    // initialize the cadence timer.  Need a timer for each element because each sensor model can be
    // configured for different publication period.  This app has only one sensor.
    wiced_init_timer(&mesh_sensor_cadence_timer, &mesh_sensor_publish_timer_callback, (TIMER_PARAM_TYPE)&mesh_config.elements[MESH_SENSOR_SERVER_ELEMENT_INDEX].sensors[MESH_TEMPERATURE_SENSOR_INDEX], WICED_MILLI_SECONDS_TIMER);

    //restore the cadence from NVRAM
    wiced_hal_read_nvram(MESH_TEMPERATURE_SENSOR_CADENCE_NVRAM_ID, sizeof(wiced_bt_mesh_sensor_config_cadence_t), (uint8_t*)(&p_sensor->cadence), &result);

    wiced_bt_mesh_model_sensor_server_init(MESH_SENSOR_SERVER_ELEMENT_INDEX, mesh_sensor_server_report_handler, mesh_sensor_server_config_change_handler, is_provisioned);

    mesh_sensor_pub_value = mesh_sensor_current_value;
    mesh_sensor_pub_time  = cur_time;

    WICED_BT_TRACE("Pub value:%d time:%d\n", mesh_sensor_pub_value, mesh_sensor_pub_time);
    wiced_bt_mesh_model_sensor_server_data(MESH_SENSOR_SERVER_ELEMENT_INDEX, WICED_BT_MESH_PROPERTY_PRESENT_AMBIENT_TEMPERATURE, NULL);
}

/*
 * New publication period is set. If it is for the sensor model, this application should take care of it.
 * The period may need to be adjusted based on the divisor.
 */
wiced_bool_t mesh_app_notify_period_set(uint8_t element_idx, uint16_t company_id, uint16_t model_id, uint32_t period)
{
    if ((element_idx != MESH_TEMPERATURE_SENSOR_INDEX) || (company_id != MESH_COMPANY_ID_BT_SIG) || (model_id != WICED_BT_MESH_CORE_MODEL_ID_SENSOR_SRV))
    {
        return WICED_FALSE;
    }
    mesh_sensor_publish_period = period;
    WICED_BT_TRACE("Sensor data send period:%dms\n", mesh_sensor_publish_period);
    mesh_sensor_server_restart_timer(&mesh_config.elements[element_idx].sensors[MESH_TEMPERATURE_SENSOR_INDEX]);
    return WICED_TRUE;
}

/*
 * Application is notified that core enters LPN mode.
 */
void mesh_app_lpn_sleep(uint32_t timeout_ms)
{
#if defined(LOW_POWER_NODE) && (LOW_POWER_NODE == 1)
    if (wiced_sleep_enter_hid_off(timeout_ms, WICED_HAL_GPIO_PIN_UNUSED, WICED_GPIO_ACTIVE_LOW) != WICED_SUCCESS)
    {
        WICED_BT_TRACE("Entering HID-Off failed\n\r");
    }
#endif
}

/*
 * Application is notified that factory reset is executed.
 */
void mesh_app_factory_reset(void)
{
    wiced_hal_delete_nvram(MESH_TEMPERATURE_SENSOR_CADENCE_NVRAM_ID, NULL);
}

/*
 * Start periodic timer depending on the publication period, fast cadence divisor and minimum interval
 */
void mesh_sensor_server_restart_timer(wiced_bt_mesh_core_config_sensor_t *p_sensor)
{
    // If there are no specific cadence settings, publish every publish period.
    uint32_t timeout = mesh_sensor_publish_period;

    wiced_stop_timer(&mesh_sensor_cadence_timer);
    if (mesh_sensor_publish_period == 0)
    {
        // The thermistor is not interrupt driven.  If client configured sensor to send notification when
        // the value changes, we will need to check periodically if the condition has been satisfied.
        timeout = p_sensor->cadence.min_interval < mesh_sensor_measure_min_interval ? p_sensor->cadence.min_interval : mesh_sensor_measure_min_interval;
    }
    else
    {
        // If fast cadence period divisor is set, we need to check temperature more
        // often than publication period.  Publish if measurement is in specified range
        if (p_sensor->cadence.fast_cadence_period_divisor > 1)
        {
            mesh_sensor_fast_publish_period = mesh_sensor_publish_period / p_sensor->cadence.fast_cadence_period_divisor;
            timeout = mesh_sensor_fast_publish_period;
        }
        else
        {
            mesh_sensor_fast_publish_period = 0;
        }
        // The thermistor is not interrupt driven.  If client configured sensor to send notification when
        // the value changes, we may need to check value more often not to miss the trigger.
        // The cadence.min_interval can be used because we do not need to send data more often than that.
        if ((p_sensor->cadence.min_interval < timeout) &&
            ((p_sensor->cadence.trigger_delta_up != 0) || (p_sensor->cadence.trigger_delta_down != 0)))
        {
            timeout = p_sensor->cadence.min_interval;
        }
    }
    WICED_BT_TRACE("sensor restart timer:%d\n", timeout);
    wiced_start_timer(&mesh_sensor_cadence_timer, timeout);
}

/*
 * Helper function to read temperature from the thermistor and convert temperature in celsius
 * to Temperature 8 format.  Unit is degree Celsius with a resolution of 0.5. Minimum: -64.0 Maximum: 63.5.
 */
int8_t mesh_sensor_get_temperature_8(void)
{
    thermistor_cfg_t thermistor_cfg;
    memset(&thermistor_cfg, 0, sizeof(thermistor_cfg_t));
#if defined (CYBLE_343072_MESH) // this BSP uses thermistor_ncp15xv103_lib
    thermistor_cfg.high_pin       = ADC_INPUT_P14;
    thermistor_cfg.low_pin        = ADC_INPUT_P8;
    thermistor_cfg.adc_power_pin  = WICED_P07;
#elif defined (CYBT_213043_MESH) // this BSP uses thermistor_ncp15xv103_lib
    thermistor_cfg.high_pin       = ADC_INPUT_P14;
    thermistor_cfg.low_pin        = ADC_INPUT_P11;
    thermistor_cfg.adc_power_pin  = WICED_P09;
#else
    thermistor_cfg.high_pin = THERMISTOR_PIN; /* Input channel to measure DC voltage(temperature)-> GPIO 10 -> J12.1, J14.1 */
#endif

    int16_t temp_celsius_100 = thermistor_read(&thermistor_cfg);

    if (temp_celsius_100 < -6400)
    {
        return 0x80;
    }
    else if (temp_celsius_100 >= 6350)
    {
        return 0x7F;
    }
    else
    {
        return (int8_t)(temp_celsius_100 / 50);
    }
}

/*
 * Process the configuration changes set by the Sensor Client.
 */
void mesh_sensor_server_config_change_handler(uint8_t element_idx, uint16_t event, void *p_data)
{
    WICED_BT_TRACE("mesh_sensor_server_config_change_handler msg: %d\n", event);

    switch (event)
    {
    case WICED_BT_MESH_SENSOR_CADENCE_STATUS:
        mesh_sensor_server_process_cadence_changed(element_idx, (wiced_bt_mesh_sensor_cadence_status_data_t*) p_data);
        break;

    case WICED_BT_MESH_SENSOR_SETTING_STATUS:
        mesh_sensor_server_process_setting_changed(element_idx, (wiced_bt_mesh_sensor_setting_status_data_t*) p_data);
        break;
    }
}

/*
 * Process get request from Sensor Client and respond with sensor data
 */
void mesh_sensor_server_report_handler(uint16_t event, uint8_t element_idx, void *p_get, void *p_ref_data)
{
    wiced_bt_mesh_sensor_get_t *p_sensor_get = (wiced_bt_mesh_sensor_get_t *)p_get;
    WICED_BT_TRACE("mesh_sensor_server_report_handler msg: %d\n", event);

    switch (event)
    {
    case WICED_BT_MESH_SENSOR_GET:
        // measure the temperature and update it to mesh_config
        mesh_sensor_sent_value = mesh_sensor_get_temperature_8();

        // tell mesh models library that data is ready to be shipped out, the library will get data from mesh_config
        wiced_bt_mesh_model_sensor_server_data(element_idx, p_sensor_get->property_id, p_ref_data);
        break;

    case WICED_BT_MESH_SENSOR_COLUMN_GET:
        break;

    case WICED_BT_MESH_SENSOR_SERIES_GET:
        break;

    default:
        WICED_BT_TRACE("unknown\n");
        break;
    }
}

/*
 * Process cadence change
 */
void mesh_sensor_server_process_cadence_changed(uint8_t element_idx, wiced_bt_mesh_sensor_cadence_status_data_t* p_data)
{
    wiced_bt_mesh_core_config_sensor_t *p_sensor;
    uint8_t written_byte = 0;
    wiced_result_t status;
    p_sensor = &mesh_config.elements[element_idx].sensors[MESH_TEMPERATURE_SENSOR_INDEX];

    WICED_BT_TRACE("cadence changed property id:%04x\n", p_data->property_id);
    WICED_BT_TRACE("Fast cadence period divisor:%d\n", p_sensor->cadence.fast_cadence_period_divisor);
    WICED_BT_TRACE("Is trigger type percent:%d\n", p_sensor->cadence.trigger_type_percentage);
    WICED_BT_TRACE("Trigger delta up:%d\n", p_sensor->cadence.trigger_delta_up);
    WICED_BT_TRACE("Trigger delta down:%d\n", p_sensor->cadence.trigger_delta_down);
    WICED_BT_TRACE("Min Interval:%d\n", p_sensor->cadence.min_interval);
    WICED_BT_TRACE("Fast cadence low:%d\n", p_sensor->cadence.fast_cadence_low);
    WICED_BT_TRACE("Fast cadence high:%d\n", p_sensor->cadence.fast_cadence_high);

    /* save cadence to NVRAM */
    written_byte = wiced_hal_write_nvram(MESH_TEMPERATURE_SENSOR_CADENCE_NVRAM_ID, sizeof(wiced_bt_mesh_sensor_config_cadence_t), (uint8_t*)(&p_sensor->cadence), &status);
    WICED_BT_TRACE("NVRAM write: %d\n", written_byte);

    mesh_sensor_server_restart_timer(p_sensor);
}

/*
 * Publication timer callback.  Need to send data if publish period expired, or
 * if value has changed more than specified in the triggers, or if value is in range
 * of fast cadence values.
 */
void mesh_sensor_publish_timer_callback(TIMER_PARAM_TYPE arg)
{
    wiced_bt_mesh_event_t *p_event;
    wiced_bt_mesh_core_config_sensor_t *p_sensor = (wiced_bt_mesh_core_config_sensor_t *)arg;
    wiced_bool_t pub_needed = WICED_FALSE;
    uint32_t cur_time = wiced_bt_mesh_core_get_tick_count();

    mesh_sensor_current_value = mesh_sensor_get_temperature_8();

    if ((cur_time - mesh_sensor_pub_time) < p_sensor->cadence.min_interval)
    {
        WICED_BT_TRACE("time since last pub:%d less then cadence interval:%d\n", cur_time - mesh_sensor_pub_time, p_sensor->cadence.min_interval);
    }
    else
    {
        // check if publication timer expired
        if ((mesh_sensor_publish_period != 0) && (cur_time - mesh_sensor_pub_time >= mesh_sensor_publish_period))
        {
            WICED_BT_TRACE("Pub needed period\n");
            pub_needed = WICED_TRUE;
        }
        // still need to send if publication timer has not expired, but triggers are configured, and value
        // changed too much
        if (!pub_needed && ((p_sensor->cadence.trigger_delta_up != 0) || (p_sensor->cadence.trigger_delta_down != 0)))
        {
            if (!p_sensor->cadence.trigger_type_percentage)
            {
                WICED_BT_TRACE("Native cur value:%d sent:%d delta:%d/%d\n",
                        mesh_sensor_current_value, mesh_sensor_pub_value, p_sensor->cadence.trigger_delta_up, p_sensor->cadence.trigger_delta_down);

                if (((p_sensor->cadence.trigger_delta_up != 0)   && ((int32_t)mesh_sensor_current_value >= (int32_t)(mesh_sensor_pub_value + p_sensor->cadence.trigger_delta_up))) ||
                    ((p_sensor->cadence.trigger_delta_down != 0) && ((int32_t)mesh_sensor_current_value <= (int32_t)(mesh_sensor_pub_value - p_sensor->cadence.trigger_delta_down))))
                {
                    WICED_BT_TRACE("Pub needed native value\n");
                    pub_needed = WICED_TRUE;
                }
            }
            else
            {
                // need to calculate percentage of the increase or decrease.  The deltas are in 0.01%.
                if ((p_sensor->cadence.trigger_delta_up != 0) && (mesh_sensor_current_value > mesh_sensor_pub_value))
                {
                    WICED_BT_TRACE("Delta up:%d\n", ((uint32_t)(mesh_sensor_current_value - mesh_sensor_pub_value) * 10000 / mesh_sensor_current_value));
                    if (((uint32_t)(mesh_sensor_current_value - mesh_sensor_pub_value) * 10000 / mesh_sensor_current_value) > p_sensor->cadence.trigger_delta_up)
                    {
                        WICED_BT_TRACE("Pub needed percent delta up:%d\n", ((mesh_sensor_current_value - mesh_sensor_pub_value) * 10000 / mesh_sensor_current_value));
                        pub_needed = WICED_TRUE;
                    }
                }
                else if ((p_sensor->cadence.trigger_delta_down != 0) && (mesh_sensor_current_value < mesh_sensor_pub_value))
                {
                    WICED_BT_TRACE("Delta down:%d\n", ((uint32_t)(mesh_sensor_pub_value - mesh_sensor_current_value) * 10000 / mesh_sensor_current_value));
                    if (((uint32_t)(mesh_sensor_pub_value - mesh_sensor_current_value) * 10000 / mesh_sensor_current_value) > p_sensor->cadence.trigger_delta_down)
                    {
                        WICED_BT_TRACE("Pub needed percent delta down:%d\n", ((mesh_sensor_pub_value - mesh_sensor_current_value) * 10000 / mesh_sensor_current_value));
                        pub_needed = WICED_TRUE;
                    }
                }
            }
        }
        // may still need to send if fast publication is configured
        if (!pub_needed && (mesh_sensor_fast_publish_period != 0))
        {
            // check if fast publish period expired
            if (cur_time - mesh_sensor_pub_time >= mesh_sensor_fast_publish_period)
            {
                // if cadence high is more than cadence low, to publish, the value should be in range
                if (p_sensor->cadence.fast_cadence_high > p_sensor->cadence.fast_cadence_low)
                {
                    if ((mesh_sensor_current_value > p_sensor->cadence.fast_cadence_low) &&
                        (mesh_sensor_current_value <= p_sensor->cadence.fast_cadence_high))
                    {
                        WICED_BT_TRACE("Pub needed in range\n");
                        pub_needed = WICED_TRUE;
                    }
                }
                else if (p_sensor->cadence.fast_cadence_high < p_sensor->cadence.fast_cadence_low)
                {
                    if ((mesh_sensor_current_value > p_sensor->cadence.fast_cadence_low) ||
                        (mesh_sensor_current_value < p_sensor->cadence.fast_cadence_high))
                    {
                        WICED_BT_TRACE("Pub needed out of range\n");
                        pub_needed = WICED_TRUE;
                    }
                }
                else
                {
                    // p_sensor->cadence.fast_cadence_high == p_sensor->cadence.fast_cadence_low)
                    // publish if current value is the same as cadence high/low
                    if (mesh_sensor_current_value == p_sensor->cadence.fast_cadence_low)
                    {
                        WICED_BT_TRACE("Pub needed equal\n");
                        pub_needed = WICED_TRUE;
                    }
                }
            }
        }
        // We will still send publication if Deltas are not set, but measured value has changed.
        if (!pub_needed && (mesh_sensor_publish_period == 0) && (p_sensor->cadence.trigger_delta_up == 0) && (p_sensor->cadence.trigger_delta_down == 0))
        {
            if (mesh_sensor_current_value != mesh_sensor_pub_value)
            {
               WICED_BT_TRACE("Pub needed new value no deltas\n");
               pub_needed = WICED_TRUE;
            }
        }
        if (pub_needed)
        {
            mesh_sensor_sent_value = mesh_sensor_current_value;
            mesh_sensor_pub_value  = mesh_sensor_current_value;
            mesh_sensor_pub_time   = cur_time;

            WICED_BT_TRACE("Pub value:%d time:%d\n", mesh_sensor_sent_value, mesh_sensor_pub_time);
            wiced_bt_mesh_model_sensor_server_data(MESH_SENSOR_SERVER_ELEMENT_INDEX, WICED_BT_MESH_PROPERTY_PRESENT_AMBIENT_TEMPERATURE, NULL);
        }
    }
    mesh_sensor_server_restart_timer(p_sensor);
}

/*
 * Process setting change
 */
void mesh_sensor_server_process_setting_changed(uint8_t element_idx, wiced_bt_mesh_sensor_setting_status_data_t* p_data)
{
    WICED_BT_TRACE("settings changed property id of sensor = %x , sensor prop id = %x \n", p_data->property_id, p_data->setting.setting_property_id);
}
