//*************************************************************************
//	OV7670 (non FIFO) Sample program for ESP32 
//
//  OUTPUT:	ILI9341 TFT(320x240) SPI 
//					option XPT2046 Touchscreen
//
//*************************************************************************
#include <Wire.h>
#include <SPI.h>
#include <OV7670.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// For the Adafruit shield, these are the default.
#define TFT_DC 		16
#define TFT_CS 		 5
#define TFT_RST 	17
#define TFT_MISO 	19
#define TFT_MOSI 	23
#define TFT_CLK 	18

#define TS_CS			 2
#define TS_IRQ		-1

const camera_config_t cam_conf = {
	.D0	= 36,
	.D1 = 39,
	.D2 = 34,
	.D3 = 35,
	.D4 = 32,
	.D5 = 33,
	.D6 = 25,
	.D7 = 26,
	.XCLK = 27,
	.PCLK = 14,
	.VSYNC = 13,
	.xclk_freq_hz = 10000000,			// XCLK 10MHz
	.ledc_timer		= LEDC_TIMER_0,
	.ledc_channel = LEDC_CHANNEL_0	
};
//	SSCB_SDA(SIOD) 	--> 21(ESP32)
//	SSCB_SCL(SIOC) 	--> 22(ESP32)
//	RESET   --> 3.3V
//	PWDN		--> GND
//	HREF		--> NC

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TS_CS);
OV7670 cam;						// camera

void setup() {
  Serial.begin(115200);
	Wire.begin();
	Wire.setClock(400000);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(0);
	ts.begin();				// タッチスクリーン
	
  Serial.println("OV7670 camera Init...");     
  esp_err_t err = cam.init(&cam_conf, QVGA, RGB565);		// カメラを初期化
	if(err != ESP_OK)		Serial.println("cam.init ERROR");

	cam.setPCLK(2, DBLV_CLK_x4);	// PCLK 設定 : 10MHz / (pre+1) * 4 --> 13.3MHz  
	cam.vflip( false );		// 画面１８０度回転
 
  Serial.printf("cam MID = %X\n\r",cam.getMID());
  Serial.printf("cam PID = %X\n\r",cam.getPID());

//	cam.colorbar(true);			// カラーバーを表示する場合

}

void loop(void) {
	uint16_t y, *buf;

	while(1){
		for( y = 0; y < 240; y++){			
			buf = cam.getLine( y+1 );								// カメラから１ライン分読み込む LineNo(1~240)
			tft.drawRGBBitmap( 0, y, buf, 320, 1);	// TFT に１ライン出力
  	}
  	if( ts.touched() ){
  		TS_Point p= ts.getPoint();
			Serial.print("->("); 
			Serial.print(p.x); Serial.print(", "); 
			Serial.print(p.y); Serial.print(", ");
			Serial.print(p.z);			
			Serial.println(")");
		}
	}
}

