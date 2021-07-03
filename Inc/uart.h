#ifndef _UART_H_
#define _UART_H_
extern void uart_init(uint32_t baud, void *rx_buf, uint32_t rx_buf_len);

const char *usart_receive_chunk(unsigned int timeout,
		unsigned int preferred_align,
		unsigned int min_preferred_chunk,
		unsigned int max_preferred_chunk,
		unsigned int *bytes_returned);

#endif // !_UART_H_
