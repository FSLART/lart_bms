/*
 * bl_log.c
 *
 * Ver bl_log.h.
 */

#include "bl_log.h"
#include "bl_port.h"
#include <stdarg.h>
#include <stdbool.h>

#define LINE_MAX 120u

typedef struct {
	char     text[LINE_MAX];
	uint32_t length;
} line_t;

static void put(line_t *line, char character) {

	if (line->length < (LINE_MAX - 2u)) {   // fica espaco para o \r\n
		line->text[line->length++] = character;
	}
}

static void put_number(line_t *line, uint32_t value, uint32_t base, uint32_t width, char pad, bool negative) {

	char digits[12];
	uint32_t count = 0;

	do {
		uint32_t digit = value % base;
		digits[count++] = (char) ((digit < 10u) ? ('0' + digit) : ('a' + digit - 10u));
		value /= base;
	} while ((value > 0u) && (count < sizeof(digits)));

	uint32_t length = count + (negative ? 1u : 0u);

	if (negative && (pad == '0')) {
		put(line, '-');   // o sinal antes dos zeros: -0003
	}
	while (width > length) {
		put(line, pad);
		width--;
	}
	if (negative && (pad != '0')) {
		put(line, '-');
	}
	while (count > 0u) {
		put(line, digits[--count]);
	}
}

void BlLog(const char *format, ...) {

	line_t line = { .length = 0 };
	va_list arguments;

	for (const char *prefix = "[boot] "; *prefix != '\0'; prefix++) {
		put(&line, *prefix);
	}

	va_start(arguments, format);

	for (const char *cursor = format; *cursor != '\0'; cursor++) {

		if (*cursor != '%') {
			put(&line, *cursor);
			continue;
		}

		cursor++;
		char pad = ' ';
		uint32_t width = 0;

		if (*cursor == '0') {
			pad = '0';
			cursor++;
		}
		while ((*cursor >= '0') && (*cursor <= '9')) {
			width = (width * 10u) + (uint32_t) (*cursor - '0');
			cursor++;
		}

		switch (*cursor) {
		case 's': {
			const char *text = va_arg(arguments, const char*);
			while ((text != 0) && (*text != '\0')) {
				put(&line, *text++);
			}
			break;
		}
		case 'c':
			put(&line, (char) va_arg(arguments, int));
			break;
		case 'u':
			put_number(&line, va_arg(arguments, uint32_t), 10u, width, pad, false);
			break;
		case 'd': {
			int32_t value = va_arg(arguments, int32_t);
			put_number(&line, (value < 0) ? (0u - (uint32_t) value) : (uint32_t) value, 10u, width, pad, value < 0);   // -INT32_MIN nao existe
			break;
		}
		case 'x':
			put_number(&line, va_arg(arguments, uint32_t), 16u, width, pad, false);
			break;
		case '\0':
			cursor--;   // '%' no fim
			break;
		default:
			put(&line, *cursor);
			break;
		}
	}

	va_end(arguments);

	line.text[line.length++] = '\r';
	line.text[line.length++] = '\n';
	BlPort_Write(line.text, line.length);
}
