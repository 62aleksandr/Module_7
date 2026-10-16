// Архітектура, Time-Triggered Super-Loop

#include <stdbool.h>
#include <stdint.h>
#include "esp_rom_sys.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h" // Підключаємо заголовок для роботи з Watchdog

// Прапорці задач (volatile, оскільки змінюються в колбеці таймера)
static volatile bool flag_1ms = false;
static volatile bool flag_50ms = false;

// 1. Апаратний колбек таймера (чіткий таймінг, аналог ISR)
static void timer_callback(void *arg)
{
	static uint8_t counter = 0;

	flag_1ms = true; // Сигнал для швидкої задачі (1 мс)

	counter++;
	if (counter >= 50)
	{
		flag_50ms = true; // Сигнал для повільнішої задачі (50 мс)
		counter = 0;
	}
}

// Імітація функцій периферії
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

	// --- НАЛАШТУВАННЯ WATCHDOG (СТОРOЖОВОГО ТАЙМЕРА) ---
	// Конфігурація TWDT (Task Watchdog Timer) на 2 секунди
	esp_task_wdt_config_t twdt_config = {
		.timeout_ms = 2000,	  // Час спрацьовування (2000 мс)
		.idle_core_mask = 0,  // Маска ядер (0 — поточне ядро)
		.trigger_panic = true // Перезавантажувати ESP32 у разі зависання
	};

	// Замість esp_task_wdt_init використовуємо RECONFIGURE
	esp_task_wdt_reconfigure(&twdt_config);

	// Слідкувати за головним циклом app_main
	esp_task_wdt_add(NULL);

	// --- НАЛАШТУВАННЯ ПЕРІОДИЧНОГО ТАЙМЕРА ---
	// Налаштування таймера на 1 мс (1000 мікросекунд)
	const esp_timer_create_args_t periodic_timer_args = {
		.callback = &timer_callback,
		.name = "super_loop_timer"};
	esp_timer_handle_t periodic_timer;
	esp_timer_create(&periodic_timer_args, &periodic_timer);

	// Запуск таймера з періодом 1000 мкс (1 мс)
	esp_timer_start_periodic(periodic_timer, 1000);

	// 2. Головний архітектурний цикл (Super-Loop)
	while (1)
	{
		// Задача 1: Стабілізація (кожні 1 мс)
		if (flag_1ms)
		{
			flag_1ms = false;
			printf("[Time-Triggered Super-Loop] PID_Calculate.\n");
			PID_Calculate();
		}

		// Задача 2: Інтерфейс та логування (кожні 50 мс)
		if (flag_50ms)
		{
			flag_50ms = false;
			printf("[Time-Triggered Super-Loop] Display_Update.\n");
			Display_Update();

			// Скидаємо сторожовий таймер ТУТ (кожні 50 мс)
			// Якщо цикл зависне в задачі 1 або десь іще, Watchdog перезавантажить мікроконтролер
			esp_task_wdt_reset();
		}

		// Повертаємо керування операційній системі FreeRTOS на 1 тік,
		// щоб запобігти блокуванню фонових системних тасків
		vTaskDelay(1);
	}
}