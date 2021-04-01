#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"
#include "board.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "common.h"

// 列印目錄檔案用
void SPIFFS_Directory(char * path) {
    DIR* dir = opendir(path);
    assert(dir != NULL);
    while (true) {
        struct dirent*pe = readdir(dir);
        if (!pe) break;
        // log 顯示目錄內各檔案
        ESP_LOGI(__FUNCTION__,"d_name=%s d_ino=%d d_type=%x", pe->d_name,pe->d_ino, pe->d_type);
    }
    closedir(dir);
}

// tft 測試 lcd 用
void FillTest(TFT_t * dev, int width, int height) {
	// lcdFillScreen(dev, RED);
    lcdFillScreen(dev, RED);
	vTaskDelay(50);
	lcdFillScreen(dev, GREEN);
	vTaskDelay(50);
	lcdFillScreen(dev, BLUE);
	vTaskDelay(50);
}
void FillTestBlack(TFT_t * dev, int width, int height) {
	lcdFillScreen(dev, BLACK);
}
void FillTestRed(TFT_t * dev, int width, int height) {
	lcdFillScreen(dev, RED);
}
void FillTestGreen(TFT_t * dev, int width, int height) {
	lcdFillScreen(dev, GREEN);
}
void FillTestBlue(TFT_t * dev, int width, int height) {
	lcdFillScreen(dev, BLUE);
}

uint8_t fontWidth;
uint8_t fontHeight;
uint8_t ascii[30];
int lines;
int ymax;
uint16_t vsp;
uint16_t ypos;
int i = 0;

void logger(char * str, uint16_t color) {
	ESP_LOGD(__FUNCTION__, "i=%d ypos=%d", i, ypos);

    int width = CONFIG_WIDTH;
    int height = CONFIG_HEIGHT;
    FontxFile * fx = fx16G;

    sprintf((char *)ascii, "(%d) %s", i, str);

    if (i < lines) {
        lcdDrawString(&dev, fx, 0, ypos, ascii, color);
    } else {
        lcdDrawFillRect(&dev, 0, ypos-fontHeight, width-1, ypos, BLACK);
        lcdSetScrollArea(&dev, fontHeight, (height-fontHeight), 0);
        lcdScroll(&dev, vsp);
        vsp = vsp + fontHeight;
        if (vsp > ymax) vsp = fontHeight*2;
        lcdDrawString(&dev, fx, 0, ypos, ascii, color);
    }
    ypos = ypos + fontHeight;
    if (ypos > ymax) ypos = fontHeight*2-1;
    i++;
}

TickType_t ScrollTest(TFT_t * dev, FontxFile *fx, int width, int height) {
	TickType_t startTick, endTick, diffTick;
	startTick = xTaskGetTickCount();

	// get font width & height
	uint8_t buffer[FontxGlyphBufSize];
	GetFontx(fx, 0, buffer, &fontWidth, &fontHeight);
	ESP_LOGD(__FUNCTION__,"fontWidth=%d fontHeight=%d",fontWidth,fontHeight);

	lines = (height - fontHeight) / fontHeight;
	ESP_LOGD(__FUNCTION__, "height=%d fontHeight=%d lines=%d", height, fontHeight, lines);
	ymax = (lines+1) * fontHeight;
	ESP_LOGD(__FUNCTION__, "ymax=%d",ymax);

	lcdSetFontDirection(dev, 0);
	lcdFillScreen(dev, BLACK);

	strcpy((char *)ascii, "IOT DEVICE");
	lcdDrawString(dev, fx, 0, fontHeight-1, ascii, RED);

    vsp = fontHeight*2;
    ypos = fontHeight*2-1;

	// Initialize scroll area
	//lcdSetScrollArea(dev, 0, 0x0140, 0);

	endTick = xTaskGetTickCount();
	diffTick = endTick - startTick;
	ESP_LOGI(__FUNCTION__, "elapsed time[ms]:%d",diffTick*portTICK_RATE_MS);
	return diffTick;
}