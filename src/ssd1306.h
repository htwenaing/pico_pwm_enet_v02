//ssd1306.h
/*
#ifndef SSD1306_H
#define SSD1306_H

#include "pico/stdlib.h"

#define SSD1306_I2C_ADDR 0x3C
#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 32	//was 64

void ssd1306_init(void);
void ssd1306_clear(void);
void ssd1306_show(void);
void ssd1306_draw_char(int x, int y, char c);
void ssd1306_draw_string(int x, int y, const char* str);

#endif

*/

#ifndef SSD1306_H
#define SSD1306_H

#include "pico/stdlib.h"

#define SSD1306_I2C_ADDR 0x3C
#define SSD1306_WIDTH    128

// ====================================================================
// HARDWARE SWITCH: Set to 64 for 128x64 display, or 32 for 128x32 display
// ====================================================================
#define SSD1306_HEIGHT   32  

// Global scaling configuration (1 = Normal 5x7 font, 2 = Double-sized 10x14 font)
extern int text_scale;

void ssd1306_init(void);
void ssd1306_clear(void);
void ssd1306_show(void);
void ssd1306_draw_char(int x, int y, char c);
void ssd1306_draw_string(int x, int y, const char* str);

#endif
