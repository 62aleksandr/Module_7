// Архітектура - Interrupt-Driven
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_rom_sys.h"

#define MAX_SAFE_VOLTAGE 6000 // Максимально безпечна напруга

// Глобальні змінні стану (обов'язково volatile)
static volatile uint16_t current_voltage = 0;
static volatile uint16_t target_voltage = 5000;

// Прапорці для обробки важкого коду у фоні (Background) за ініціативою переривань
static volatile bool event_pid = false;
static volatile bool event_ui = false;

// Заглушки периферії
void Hardware_Init(void) { printf("Периферія ініціалізована\n"); }
void PWM_Hardware_Off(void) { /* Гасимо ШІМ на рівні заліза миттєво */ }
void Set_Led_Error_On(void) { /* Вмикаємо аварійний світлодіод */ }
uint16_t ADC_Get_Result(void) { return 4980; }
uint16_t PID_Compute(uint16_t target, uint16_t current) { return 128; }
void PWM_Set_Duty(uint16_t duty) { /* Встановлюємо заповнення ШІМ */ }
void UI_Scan_Buttons(void) { /* Опитуємо кнопки */ }
void UI_Update_Display(uint16_t voltage) { /* Оновлюємо екран */ }

// ==========================================
// 1. НАДКРИТИЧНЕ ПЕРЕРИВАННЯ (Приоритет 0 - Найвищий)
// ==========================================
/*
 * ПРАВИЛО БЕЗПЕКИ СИЛОВИКІВ
 * У силовій інженерії при критичній аварії процесор вводять у нескінченний цикл while(1),
 * а не перезавантажують мікроконтролер відразу. Це робиться для того, щоб МК після ресету
 * випадково не ініціалізував заново драйвери і не подав струм на закорочене залізо
 * (запобігання "шторму повторних вибухів").
 *
 * НЮАНС ДЛЯ ESP32: Оскільки у нас увімкнено Task Watchdog Timer (TWDT), цей чистий
 * цикл while(1) через 2 секунди все одно викличе апаратний Panic/Reset. У реальних
 * комерційних пристроях у цьому місці Watchdog або вимикають на рівні регістрів, або
 * апаратно конфігурують ніжки ШІМ так, щоб при старті МК вони залишалися строго
 * притиснутими до землі (Pull-Down) і не могли самовільно відкрити транзистори.
 */

void Emergency_Analog_Hardware_ISR(void)
{
	PWM_Hardware_Off(); // Критична дія — миттєво на рівні заліза
	Set_Led_Error_On();
	while (1)
		; // Блокування системи в безпечному режимі
}

// ==========================================
// 2. РЕГУЛЯТОРНЕ ПЕРЕРИВАННЯ (Кожні 1 мс) - ФОН (Foreground)
// ==========================================
static void System_Tick_Timer_ISR(void *arg)
{
	// Швидка апаратна дія: зчитуємо датчик прямо в ISR
	current_voltage = ADC_Get_Result();

	// Миттєвий захист заліза — залишається в перериванні
	if (current_voltage > MAX_SAFE_VOLTAGE)
	{
		PWM_Hardware_Off();
		return;
	}

	// Замість важких обчислень та printf — просто даємо сигнал головному циклу
	event_pid = true;
}

// ==========================================
// 3. ІНТЕРФЕЙСНЕ ПЕРЕРИВАННЯ (Кожні 50 мс) - ФОН (Foreground)
// ==========================================
static void UI_Timer_ISR(void *arg)
{
	// Швидка апаратна дія: опитування кнопок (читання регістрів GPIO)
	UI_Scan_Buttons();

	// Даємо сигнал головному циклу оновити важкий дисплей та скинути ватчдог
	event_ui = true;
}

// ==========================================
// ГОЛОВНА ФУНКЦІЯ - ПРОЦЕДУРА (Background)
// ==========================================
void app_main(void)
{
	// 1. ОДРАЗУ при старті зчитуємо причину перезавантаження
	esp_reset_reason_t reason = esp_reset_reason();

	// 2. Якщо пристрій впав по Watchdog — блокуємо запуск силової частини!
	if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT)
	{
		printf("\n!!! УВАГА !!! КРИТИЧНА ПОМИЛКА БЕЗПЕКИ !!!\n");
		printf("[SYSTEM] Попередній запуск завершився зависанням та спрацьовуванням Watchdog!\n");
		printf("[SYSTEM] Запуск силової частини ЗАБЛОКОВАНО задля запобігання вибуху.\n");

		PWM_Hardware_Off(); // Про всяк випадок тримаємо ШІМ вимкненим
		Set_Led_Error_On(); // Вмикаємо аварійну індикацію

		// Входимо в безпечний нескінченний цикл.
		// Вихід тільки через повне відключення живлення людиною.
		while (1)
		{
			esp_rom_delay_us(1000000); // Сидимо тут вічно
		}
	}

	Hardware_Init();

	// Налаштування Watchdog
	esp_task_wdt_config_t twdt_config = {
		.timeout_ms = 2000,
		.idle_core_mask = 0,
		.trigger_panic = true};
	esp_task_wdt_reconfigure(&twdt_config);
	esp_task_wdt_add(NULL);

	// Налаштування Таймера 1 (1 мс)
	const esp_timer_create_args_t timer_1ms_args = {
		.callback = &System_Tick_Timer_ISR,
		.name = "sys_tick_timer"};
	esp_timer_handle_t timer_1ms;
	esp_timer_create(&timer_1ms_args, &timer_1ms);
	esp_timer_start_periodic(timer_1ms, 1000);

	// Налаштування Таймера 2 (50 мс)
	const esp_timer_create_args_t timer_50ms_args = {
		.callback = &UI_Timer_ISR,
		.name = "ui_tick_timer"};
	esp_timer_handle_t timer_50ms;
	esp_timer_create(&timer_50ms_args, &timer_50ms);
	esp_timer_start_periodic(timer_50ms, 50000);

	printf("[Interrupt-Driven] Систему запущено. Реактивний головний цикл.\n");

	// Головний реактивний потік обробки
	while (1)
	{
		// Обробка математики PID за ініціативою переривання 1 мс
		if (event_pid)
		{
			event_pid = false; // Скидаємо запит

			// Важкі обчислення та логування виконуються ТУТ (у безпечному фоні)
			uint16_t duty = PID_Compute(target_voltage, current_voltage);
			PWM_Set_Duty(duty);
			printf("[Interrupt-Driven] PID OK.\n");
		}

		// Обробка важкого інтерфейсу за ініціативою переривання 50 мс
		if (event_ui)
		{
			event_ui = false;

			// Оновлення екрану та printf тепер не блокують інші переривання
			UI_Update_Display(current_voltage);
			printf("[Interrupt-Driven] Display Update.\n");

			// Скидаємо Watchdog у фоні. Якщо переривання інтерфейсу зникне,
			// прапорець не виставиться, код сюди не зайде, і пристрій ресетнеться.
			esp_task_wdt_reset();
		}

		// Процесор засинає і миттєво прокидається від БУДЬ-ЯКОГО переривання таймерів.
		// Завдяки мінімальному таймауту в 1 тік ми не затримуємо реакцію на прапорці.
		vTaskDelay(1);
	}
}