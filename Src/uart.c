#include "stm32f4xx_hal.h"
#include "led.h"

UART_HandleTypeDef huart1;

static volatile char *usart_rx_buf;
static unsigned int usart_rx_buf_len; 
static volatile unsigned int usart_rx_spilled;
static volatile unsigned int usart_rx_buf_wpos;
static volatile unsigned int usart_rx_buf_rpos;
static unsigned int usart_rx_buf_next_rpos;

/**
	* @brief USART1 Initialization Function
	* @param baud  波特率设置
	* @retval None
	*/
void MX_USART1_UART_Init(uint32_t baud)
{

	/* USER CODE BEGIN USART1_Init 0 */

	/* USER CODE END USART1_Init 0 */

	/* USER CODE BEGIN USART1_Init 1 */

	/* USER CODE END USART1_Init 1 */
	huart1.Instance = USART1;
	huart1.Init.BaudRate = baud;         //115200;
	huart1.Init.WordLength = UART_WORDLENGTH_8B;
	huart1.Init.StopBits = UART_STOPBITS_1;
	huart1.Init.Parity = UART_PARITY_NONE;
	huart1.Init.Mode = UART_MODE_TX_RX;
	huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart1.Init.OverSampling = UART_OVERSAMPLING_16;
	if (HAL_UART_Init(&huart1) != HAL_OK)
	{
		led_panic("UART ");
	}
	/* USER CODE BEGIN USART1_Init 2 */

	/* USER CODE END USART1_Init 2 */

}

static inline unsigned int advance_pos(unsigned int cur_pos, unsigned int amt)
{
	cur_pos += amt;
	if (cur_pos >= usart_rx_buf_len) {
		cur_pos -= usart_rx_buf_len;
	}

	return cur_pos;
}

void usart1_rx_ISR(void)
{
    	// Receive the character ASAP.
    
	unsigned char c = (huart1.Instance->DR & 0xFF);

	unsigned int wpos = usart_rx_buf_wpos;
	unsigned int next_wpos = advance_pos(wpos, 1);

	if (next_wpos == usart_rx_buf_rpos) {
		usart_rx_spilled++;
		return;
	}

	usart_rx_buf[wpos] = c;
	usart_rx_buf_wpos = next_wpos;
}

void uart_init(uint32_t baud, void *rx_buf, uint32_t rx_buf_len)
{
    usart_rx_buf = rx_buf;
    usart_rx_buf_len = rx_buf_len;

    MX_USART1_UART_Init(baud);
}

// Logic for return here is as follows:
// 1) Always return in timeout time
// 1a) can return early if the amount exceeds min_preferred_chunk
// 1b) can also return early if we are at the end of the buffer
// 2) If, after timeout, we have at least some we can return that keeps us
// aligned with preferred_align, return it
// 3) else, return everything we have
// It's expected the buffer is a multiple of preferred_align.
// min_preferred_chunk should be >= 2x preferred_align; that way, if we
// are unaligned we can get a complete aligned chunk plus the offset
const char *usart_receive_chunk(unsigned int timeout,
		unsigned int preferred_align,
		unsigned int min_preferred_chunk,
		unsigned int max_preferred_chunk,
		unsigned int *bytes_returned)
{
	unsigned int expiration = HAL_GetTick() + timeout;

	// Release the previously read chunk, so receiving can proceed into it
	unsigned int rpos = usart_rx_buf_next_rpos;
	usart_rx_buf_rpos = rpos;

	unsigned int bytes;

	unsigned int unalign = rpos % preferred_align;

	// Busywait for a completion condition
	do {
		unsigned int wpos = usart_rx_buf_wpos;

		if (wpos < rpos) {
			bytes = usart_rx_buf_len - rpos;
			break;  // case 1b
		}

		bytes = wpos - rpos;

		if (bytes >= min_preferred_chunk) break;
	} while (HAL_GetTick() < expiration);

	if (bytes > max_preferred_chunk) {
		bytes = max_preferred_chunk;
	}

	if ((bytes + unalign) >= preferred_align) {
		// Fixup for align
		bytes += unalign;

		// Get integral number of align-sized chunks
		bytes /= preferred_align;
		bytes *= preferred_align;

		// Unfixup for align
		bytes -= unalign;
	}

	*bytes_returned = bytes;

	// Next time, we'll release these returned bytes.
	usart_rx_buf_next_rpos = advance_pos(rpos, bytes);

	return (const char *) (usart_rx_buf + rpos);
}
