// https://github.com/jango175/esp_vl53l0x
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "vl53l0x.h"

#define I2C_PORT I2C_NUM_1
#define SDA_PIN GPIO_NUM_16
#define SCL_PIN GPIO_NUM_15

// Вимикаємо невикористовувані виводи, встановлюючи для них значення -1
#define GPIO1_PIN -1
#define XSHUT_PIN -1

#define I2C_FREQ 400000

static const char *TAG = "main";

void app_main(void)
{
	vl53l0x_conf_t vl53l0x_conf = {
		.i2c_port = I2C_PORT,
		.sda_pin = SDA_PIN,
		.scl_pin = SCL_PIN,
		.i2c_freq = I2C_FREQ,
		.xshut_pin = XSHUT_PIN,
		.gpio1_pin = GPIO1_PIN};
	vl53l0x_init(vl53l0x_conf);

	// measurement
	while (1)
	{
		uint16_t result_mm = 0;

		bool res = vl53l0x_read(vl53l0x_conf, &result_mm);

		if (res)
			ESP_LOGI(TAG, "Range: %hu [mm]", result_mm);
		else
			ESP_LOGE(TAG, "Measure failure");
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}