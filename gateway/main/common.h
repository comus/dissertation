#ifndef _COMMON_H_
#define _COMMON_H_

#include "fontx.h"
#include "ili9340.h"

void SPIFFS_Directory(char * path);

// tft 用
TFT_t dev;

// tft 顯示的字型
FontxFile fx16G[2];

void FillTest(TFT_t * dev, int width, int height);
void FillTestBlack(TFT_t * dev, int width, int height);
void FillTestRed(TFT_t * dev, int width, int height);
void FillTestGreen(TFT_t * dev, int width, int height);
void FillTestBlue(TFT_t * dev, int width, int height);
void logger(char * str, uint16_t color);

#endif
