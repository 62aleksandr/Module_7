// Архітектура, суперцикл (Super-Loop / Bare-Metal)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdbool.h>
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_rom_sys.h" // Необхідний заголовок для використання esp_rom_delay_us

#define PID_PER_US 1000
#define Display_Up_US 50000

// Імітація функцій периферії із затримками для симуляції навантаження
void Hardware_Init(void)
{
	printf("Периферія ініціалізована\n");
}

void PID_Calculate(void)
{
	// Імітуємо складні математичні розрахунки на 150 мікросекунд
	esp_rom_delay_us(150);
}

void Display_Update(void)
{
	// Імітуємо відправку даних на екран на 5 мілісекунд (5000 мкс)
	esp_rom_delay_us(5000);
}

void app_main(void)
{
	// Ініціалізація апаратної частини
	Hardware_Init();

	// --- НАЛАШТУВАННЯ АПАРАТНОГО WATCHDOG ---
	esp_task_wdt_config_t twdt_config = {
		.timeout_ms = 2000, // Спрацювати через 2 секунди зависання
		.idle_core_mask = 0,
		.trigger_panic = true // Перезавантажити процесор у разі аварії
	};
	esp_task_wdt_init(&twdt_config);
	esp_task_wdt_add(NULL); // Слідкувати за головним циклом app_main

	// Змінні для відстеження часу останнього виконання задач (у мікросекундах)
	uint64_t last_time_1ms = esp_timer_get_time();
	uint64_t last_time_50ms = esp_timer_get_time();

	// 2. ГОЛОВНИЙ КЛАСИЧНИЙ СУПЕРЦИКЛ (Super-Loop)
	while (1)
	{
		uint64_t current_time = esp_timer_get_time(); // Отримуємо поточний час чіпа

		// Перевірка задачі 1: Стабілізація (кожні 1 000 мкс = 1 мс)
		if ((current_time - last_time_1ms) >= PID_PER_US)
		{
			printf("[Super-Loop] Виконується PID_Calculate.\n");
			PID_Calculate();
			last_time_1ms += PID_PER_US; // Зсуваємо часову мітку строго на крок завдання
		}

		// Перевірка задачі 2: Інтерфейс та логування (кожні 50 000 мкс = 50 мс)
		if ((current_time - last_time_50ms) >= Display_Up_US)
		{
			printf("[Super-Loop] Виконується Display_Update.\n");
			Display_Update();
			last_time_50ms += Display_Up_US;

			// Скидаємо апаратний Watchdog строго в повільному циклі.
			// Якщо будь-яка задача зависне довше ніж на 2 секунди — відбудеться ресет.
			esp_task_wdt_reset();
		}

		vTaskDelay(1);
	}
}