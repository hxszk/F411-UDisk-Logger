#include "fatfs.h"
#include "led.h"
#include "jsmn.h"
#include "uart.h"
#include <string.h>
#include <stdbool.h>
#include <unistd.h>


#ifndef MIN
#define MIN(a,b) \
	({ __typeof__ (a) _a = (a); \
	 __typeof__ (b) _b = (b); \
	 _a < _b ? _a : _b; })
#endif

#define CFGFILE_NAME "logging.cfg"
#define LOGNAME_FMT "log000.txt"

/**
 * {
 *      "startupMosrse":"",
 *      "useSPI":false,
 *      "baudRate":2000000,
 *      "preallocBytes":104857600,
 *      "preallocGrow":false
 * }
 * 
 */
const unsigned char lager_cfg[] = {
  0x7b, 0x0a, 0x09, 0x22, 0x73, 0x74, 0x61, 0x72, 0x74, 0x75, 0x70, 0x4d,
  0x6f, 0x72, 0x73, 0x65, 0x22, 0x20, 0x3a, 0x20, 0x22, 0x22, 0x2c, 0x0a,
  0x09, 0x22, 0x75, 0x73, 0x65, 0x53, 0x50, 0x49, 0x22, 0x20, 0x3a, 0x20,
  0x66, 0x61, 0x6c, 0x73, 0x65, 0x2c, 0x0a, 0x09, 0x22, 0x62, 0x61, 0x75,
  0x64, 0x52, 0x61, 0x74, 0x65, 0x22, 0x20, 0x3a, 0x20, 0x32, 0x30, 0x30,
  0x30, 0x30, 0x30, 0x30, 0x2c, 0x0a, 0x09, 0x22, 0x70, 0x72, 0x65, 0x61,
  0x6c, 0x6c, 0x6f, 0x63, 0x42, 0x79, 0x74, 0x65, 0x73, 0x22, 0x20, 0x3a,
  0x20, 0x31, 0x30, 0x34, 0x38, 0x35, 0x37, 0x36, 0x30, 0x30, 0x2c, 0x0a,
  0x09, 0x22, 0x70, 0x72, 0x65, 0x61, 0x6c, 0x6c, 0x6f, 0x63, 0x47, 0x72,
  0x6f, 0x77, 0x22, 0x20, 0x3a, 0x20, 0x66, 0x61, 0x6c, 0x73, 0x65, 0x0a,
  0x7d, 0x0a
};
unsigned int lager_cfg_len = 122;

static uint32_t cfg_baudrate = 115200;
static uint32_t cfg_prealloc = 0;
static bool cfg_prealloc_grow = false;
static bool cfg_bist = false;

static uint8_t rx_buf[24 * 4096];

#define NELEMENTS(x) (sizeof(x) / sizeof(*(x)))

/* Configuration functions */
static int parse_num(const char *cfg_buf, jsmntok_t *t) {
	int value = 0;
	bool neg = false;

	for (int pos = t->start; pos < t->end; pos++) {
		char c = cfg_buf[pos];

		if ((c == '-') && (pos == t->start)) {
			neg = true;
			continue;
		}

		if ((c < '0') || (c > '9')) {
			led_panic("?");
		}

		value *= 10;

		value += c - '0';
	}

	if (neg) {
		return -value;
	}

	return value;
}

static bool parse_bool(const char *cfg_buf, jsmntok_t *t) {
	switch (cfg_buf[t->start]) {
		case 't':
		case 'T':
			return true;
		case 'f':
		case 'F':
			return false;
		default:
			led_panic("?");
	}

	return false;	// Unreachable
}

static inline bool compare_key(const char *cfg_buf, jsmntok_t *t,
		const char *value, jsmntype_t typ) {
	if ((t->end - t->start) != strlen(value)) {
		return false;
	}

	// Technically we should be case sensitive, but.. Meh!
	if (strncasecmp(value, cfg_buf + t->start,
				t->end - t->start)) {
		return false;
	}

	// OK, the key matches.  Is the value of the expected type? 
	// if not, panic (invalid config)
	if (typ != t[1].type) {
		led_panic("?");
	}

	return true;
}


static bool is_digit(char c) {
	if (c < '0') return false;
	if (c > '9') return false;

	return true;
}


// Try to load a config file.  If it doesn't exist, create it.
// If we can't load after that, PANNNNIC.
void process_config() {
	FIL cfg_file;

	FRESULT res = f_open(&cfg_file, CFGFILE_NAME, FA_WRITE | FA_CREATE_NEW);

	if (res == FR_OK) {
		UINT wr_len = sizeof(lager_cfg);
		UINT written;
		res = f_write(&cfg_file, lager_cfg, wr_len, &written);

		if (res != FR_OK) {
			led_panic("WCFG");
		}

		if (written != wr_len) {
			led_panic("FULL");
		}

		f_close(&cfg_file);
	} else if (res != FR_EXIST) {
		led_panic("WCFG2");
	}

	res = f_open(&cfg_file, CFGFILE_NAME, FA_READ | FA_OPEN_EXISTING);

	if (res != FR_OK) {
		led_panic("RCFG");
	}

	char cfg_buf[4096];

	char cfg_morse[128];
	cfg_morse[0] = 0;

	UINT amount;

        if (FR_OK != f_read(&cfg_file, cfg_buf, sizeof(cfg_buf), &amount)) {
		led_panic("RCFG");
	}

	if (amount == 0 || amount >= sizeof(cfg_buf)) {
		led_panic("RCFG");
	}

	jsmntok_t tokens[100];
	jsmn_parser parser;

	jsmn_init(&parser);

	/* parse config */
	int num_tokens = jsmn_parse(&parser, cfg_buf, amount, tokens, 100);

	// Minimal should be JSMN_OBJECT 
	if (num_tokens < 1) {
		// ..--..
		led_panic("?");
	}

	int skip_count = 0;

	if (tokens[0].type != JSMN_OBJECT) {
		// ..--..
		led_panic("?");
	}

	// tokens-1 to guarantee that there's always room for a value after
	// our string key..
	for (int i=1; i<(num_tokens-1); i++) {
		jsmntok_t *t = tokens + i;
		jsmntok_t *next = t + 1;

		if (skip_count) {
			skip_count--;

			if ((t->type == JSMN_ARRAY) ||
					(t->type == JSMN_OBJECT)) {
				skip_count += t->size;
			}

			continue;
		}

		if (t->type != JSMN_STRING) {
			led_panic("?");
		}

		/* OK, it's a string. */

		if (compare_key(cfg_buf, t, "startupMorse", JSMN_STRING)) {
			int len = MIN(next->end - next->start, sizeof(cfg_morse)-1);

			memcpy(cfg_morse, cfg_buf + next->start, len);
			cfg_morse[len] = 0;
		} else if (compare_key(cfg_buf, t, "useSPI", JSMN_PRIMITIVE)) {
			if (parse_bool(cfg_buf, next)) {
				// XXX SPI not supported yet
				led_panic("?SPI?");
			}
		} else if (compare_key(cfg_buf, t, "baudRate", JSMN_PRIMITIVE)) {
			cfg_baudrate = parse_num(cfg_buf, next);
		} else if (compare_key(cfg_buf, t, "preallocBytes", JSMN_PRIMITIVE)) {
			cfg_prealloc = parse_num(cfg_buf, next);
		} else if (compare_key(cfg_buf, t, "preallocGrow", JSMN_PRIMITIVE)) {
			cfg_prealloc_grow = parse_bool(cfg_buf, next);
		} else if (compare_key(cfg_buf, t, "builtInSelfTest", JSMN_PRIMITIVE)) {
			cfg_bist = parse_bool(cfg_buf, next);
		}

		i++;	// Skip the value too on next iter.
	}

	f_close(&cfg_file);

	if (cfg_morse[0]) {
		led_send_morse(cfg_morse);
	}
}

static int advance_filename(char *f) {
	while (*f && (!is_digit(*f))) {
		f++;
	}

	if (!*f) return -1;	// No digits in string

	while (is_digit(*f)) {
		f++;
	}

	f--;			// Scan backwards

	// Now we're at the last digit.  Increment the number, with ripple
	// carry.

	while (true) {
		// If we're not pointing to a digit currently, we lose
		if (!is_digit(*f)) {
			return -1;
		}

		(*f)++;

		if (is_digit(*f)) {
			// We incremented and don't need to carry.  we win.
			return 0;
		}

		*f = '0';

		f--;
	}

}

static void open_log(FIL *fil) {
	char filename[] = LOGNAME_FMT;
	FRESULT res;

	res = f_open(fil, filename, FA_WRITE | FA_CREATE_NEW);

	while (res == FR_EXIST) {
		if (advance_filename(filename)) {
			// ..-. .. .-.. . ...
			led_panic("FILES");
		}

		// This is likely n^2 or worse with number of config files
		res = f_open(fil, filename, FA_WRITE | FA_CREATE_NEW);
	}

	if (res != FR_OK) {
		// --- .-... --- --.
		led_panic("OLOG");
	}

	if (cfg_prealloc > 0) {
		// Attempt to preallocate contig space for the logfile
		// Best effort only-- figure it's better to keep going if
		// we can't alloc it at all.

		f_expand(fil, cfg_prealloc, cfg_prealloc_grow ? 1 : 0);
	}

}


void blackbox_logging_process(void)
{
    
    retUSER = f_mount(&USERFatFS, USERPath, 1);
    if (retUSER != FR_OK)
    {
        led_panic("DATA ");
    }
    
    process_config();
    uart_init(cfg_baudrate, rx_buf, sizeof(rx_buf));

    FIL log_file;
    open_log(&log_file);

    while(1)
    {
        const char *pos;
		unsigned int amt;

		// 50 ticks == 200ms, prefer 512 byte sector alignment,
		// and >= 2560 byte chunks are best
		// Never get more than about 2/5 of the buffer (40 * 1024)--
		// because we want to finish the IO and free it up
        // 上面是OpenLager的原本设置
        // 本案例的tick = 1ms，flash的sector size = 4096
		pos = usart_receive_chunk(200, 4096, 1*4096,
				10*4096, &amt);

		// Could consider if pos is short, waiting a little longer
		// (400-600ms?) next time...

		led_set(true);	// Illuminate LED during IO

		FRESULT res;

		if (!amt) {
			// If nothing has happened in 200ms, flush our
			// buffers.
			res = f_sync(&log_file);

			if (res != FR_OK) {
				// . .-. .-.
				led_panic("SERR");
			}
		} else {
			UINT written;

			res = f_write(&log_file, pos, amt, &written);

			if (res != FR_OK) {
				// . .-. .-.
				led_panic("WERR");
			}

			if (written != amt) {
				// ..-. ..- .-.. .-..
				led_panic("FULL");
			}
		}

		led_set(false);
    }
}
