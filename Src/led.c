#include "stm32f4xx_hal.h"
#include "morsel.h"
#include "stdbool.h"

#define LED_PIN         GPIO_PIN_13
#define LED_GPIO_PORT   GPIOC

static int led_time_per_dot = 100;

/**
	* @brief GPIO Initialization Function
	* @param None
	* @retval None
	*/
static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_RESET);

	/*Configure GPIO pin : PC13 */
	GPIO_InitStruct.Pin = LED_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(LED_GPIO_PORT, &GPIO_InitStruct);
}

inline void led_toggle()
{
    HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);
}

void led_set(bool light)
{
	if (light) {
        HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_RESET);
	} else {
		HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_SET);
	}
}

void led_send_morse(char *string)
{
	char *pos = string;
	int val;

	uint32_t state = 0;


	while ((val = morse_send(&pos, &state)) != -1) {

        HAL_Delay(led_time_per_dot);

		led_set(val > 0);
	}
}

void led_panic(char *string)
{
	while (true) {
		led_send_morse(string);
		led_send_morse("  ");
	}
}

void led_set_morse_speed(int time_per_dot)
{
	led_time_per_dot = time_per_dot;
}

void led_init()
{
    MX_GPIO_Init();

	led_set(false);
}
