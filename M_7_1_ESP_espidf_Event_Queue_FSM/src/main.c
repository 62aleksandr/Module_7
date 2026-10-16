// Архітектура- Event-Driven + Event Queue (Кільцевий буфер) + State Machine
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_rom_sys.h"
#include "esp_attr.h" // Для макросу IRAM_ATTR

#define MAX_SAFE_VOLTAGE 6000
#define QUEUE_SIZE 16 // Розмір кільцевого буфера подій

// ==========================================
// 1. ВИЗНАЧЕННЯ ПОДІЙ (Events Enum)
// ==========================================
typedef enum
{
	EVENT_NONE,				 // Подія відсутня (дефолтне)
	EVENT_TICK_10MS,		 // Подія від таймера стабілізації
	EVENT_UI_TICK_50MS,		 // Подія від таймера інтерфейсу
	EVENT_HARDWARE_EMERGENCY // Надкритична подія аварії заліза
} system_event_t;

static volatile bool ui_tick_50ms_flag = false; // Подія від таймера інтерфейсу

// ==========================================
// 2. ВИЗНАЧЕННЯ СТАНІВ (States Enum)
// ==========================================
typedef enum
{
	STATE_INIT,		// Стан ініціалізації
	STATE_NORMAL,	// Робочий (штатний) стан
	STATE_EMERGENCY // Аварійний стан
} system_state_t;

// Глобальні змінні стану системи
static volatile uint16_t current_voltage = 0;
static volatile uint16_t target_voltage = 5000;

// ==========================================
// 3. КІЛЬЦЕВИЙ БУФЕР / FIFO ЧЕРГА ПОДІЙ
// ==========================================
static system_event_t event_queue[QUEUE_SIZE];
static volatile uint8_t head = 0;
static volatile uint8_t tail = 0;

// Запис події в чергу (безпечна для переривань)
void IRAM_ATTR queue_push(system_event_t event)
{
	uint8_t next_head = (head + 1) % QUEUE_SIZE;
	if (next_head != tail)
	{ // Захист від переповнення
		event_queue[head] = event;
		head = next_head;
	}
}

// Читання події з черги в головному циклі
bool queue_pop(system_event_t *event)
{
	if (head == tail)
	{
		return false; // Черга порожня
	}
	*event = event_queue[tail];
	tail = (tail + 1) % QUEUE_SIZE;
	return true;
}

// Заглушки периферії
void Hardware_Init(void) { printf("Периферія ініціалізована\n"); }
void PWM_Hardware_Off(void) { /* Гасимо ШІМ миттєво */ }
void Set_Led_Error_On(void) { /* Вмикаємо аварійний світлодіод */ }
uint16_t ADC_Get_Result(void) { return 4980; }
uint16_t PID_Compute(uint16_t target, uint16_t current) { return 128; }
void PWM_Set_Duty(uint16_t duty) { /* Встановлюємо заповнення ШІМ */ }
void UI_Scan_Buttons(void) { /* Опитуємо кнопки */ }
void UI_Update_Display(uint16_t voltage) { /* Оновлюємо екран */ }

// ==========================================
// 4. АСИНХРОННІ ПЕРЕРИВАННЯ (Генератори Подій)
// ==========================================
void Emergency_Analog_Hardware_ISR(void)
{
	// Надкритична дія виконується миттєво на рівні заліза
	PWM_Hardware_Off();
	// Закидаємо подію аварії в чергу для обробки автоматом станів
	queue_push(EVENT_HARDWARE_EMERGENCY);
}

static void System_Tick_Timer_ISR(void *arg)
{
	// Просто фіксуємо подію часу без виконання логіки
	queue_push(EVENT_TICK_10MS);
}

static void UI_Timer_ISR(void *arg)
{
	// Просто фіксуємо подію часу без виконання логіки
	queue_push(EVENT_UI_TICK_50MS);
}

// ==========================================
// ГОЛОВНА ФУНКЦІЯ ТА ДИСПЕТЧЕР (Event Loop)
// ==========================================
void app_main(void)
{
	Hardware_Init();

	// Налаштування Watchdog
	esp_task_wdt_config_t twdt_config = {
		.timeout_ms = 2000,
		.idle_core_mask = 0,
		.trigger_panic = true};
	// esp_task_wdt_reconfigure(&twdt_config);
	// esp_task_wdt_add(NULL);
	ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));
	ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

	// Ініціалізація апаратних таймерів (Генераторів подій)
	const esp_timer_create_args_t timer_1ms_args =
		{
			.callback = &System_Tick_Timer_ISR,
			.name = "10ms"};
	esp_timer_handle_t timer_10ms;
	esp_timer_create(&timer_1ms_args, &timer_10ms);
	esp_timer_start_periodic(timer_10ms, 10000);

	const esp_timer_create_args_t timer_50ms_args =
		{
			.callback = &UI_Timer_ISR,
			.name = "50ms"};
	esp_timer_handle_t timer_50ms;
	esp_timer_create(&timer_50ms_args, &timer_50ms);
	esp_timer_start_periodic(timer_50ms, 50000);

	// Початковий стан системи (States Enum)
	system_state_t current_state = STATE_INIT;
	system_event_t current_event;

	printf("[Event-Driven] Систему запущено. Очікування подій...\n");
	current_state = STATE_NORMAL; // Переходимо в робочий режим

	// ГОЛОВНИЙ ЦИКЛ ПОДІЙ (Event Loop)
	while (1)
	{
		// 1. Опитування черги подій (FIFO Кільцевий буфер)
		if (queue_pop(&current_event))
		{

			// 2. КІНЦЕВИЙ АВТОМАТ (State Machine)
			switch (current_state)
			{

			case STATE_NORMAL:
				if (current_event == EVENT_TICK_10MS)
				{
					current_voltage = ADC_Get_Result();

					// Дублюючий захист
					if (current_voltage > MAX_SAFE_VOLTAGE)
					{
						current_state = STATE_EMERGENCY;
						PWM_Hardware_Off();
						Set_Led_Error_On();
						printf("[FSM] Аварія! Перенапруга: %d мВ\n", current_voltage);
						break;
					}

					uint16_t duty = PID_Compute(target_voltage, current_voltage);
					PWM_Set_Duty(duty);
					printf("10MS -> OK.\n");
				}

				else if (current_event == EVENT_UI_TICK_50MS)
				{
					UI_Scan_Buttons();
					UI_Update_Display(current_voltage);
					printf("50MS -> OK.\n");

					// // Скидаємо Watchdog тільки при успішній обробці подій інтерфейсу
					esp_task_wdt_reset();
				}

				else if (current_event == EVENT_HARDWARE_EMERGENCY)
				{
					current_state = STATE_EMERGENCY;
					printf("[FSM] Перехід у стан EMERGENCY через апаратну аварію.\n");
				}
				break;

			case STATE_EMERGENCY:
				// У стані аварії ігноруємо всі штатні події таймерів
				if (current_event == EVENT_HARDWARE_EMERGENCY)
				{
					Set_Led_Error_On();
				}
				// Watchdog не скидається, або система просто блокується заради безпеки
				break;

			default:
				break;
			}
		}

		// Повертаємо квант часу системі FreeRTOS, щоб процесор не перегрівався,
		// поки черга FIFO порожня
		// vTaskDelay(1);
	}
}