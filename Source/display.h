#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define LCD_WIDTH 240u
#define LCD_HEIGHT 320u

#define COLOR_BG 0x0843u
#define COLOR_PANEL 0x1926u
#define COLOR_EDGE 0x2b28u
#define COLOR_TEXT 0xef9fu
#define COLOR_DIM 0x9d35u
#define COLOR_ACCENT 0x5f5au
#define COLOR_TX 0xf9c7u
#define COLOR_AMBER 0xfda9u

typedef enum { FONT_SMALL = 1, FONT_MEDIUM, FONT_LARGE } DisplayFont;

void display_init(void);
void display_fill(unsigned x, unsigned y, unsigned width, unsigned height, uint16_t color);
void display_text(unsigned x, unsigned y, unsigned cells, DisplayFont font,
                  uint16_t color, uint16_t background, const char *text);

#endif
