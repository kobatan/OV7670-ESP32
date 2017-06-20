//*************************************************************************
//	OV7670 (non FIFO) Simple Web streamer for ESP32 
//
//	line 20,21,80.81.82 は環境に合わせて各自設定変更してください。
//  line 46~69 でカメラ解像度を指定（コメントを外す）
//
//*************************************************************************
#include <Wire.h>
#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include "hwcrypto/sha.h"
#include "base64.h"
#include <OV7670.h>

IPAddress myIP 			= IPAddress(192,168,1,99);	// 固定IPアドレス
IPAddress myGateway = IPAddress(192,168,1, 1);

const camera_config_t cam_conf = {
	.D0	= 36,
	.D1 = 39,
	.D2 = 34,
	.D3 = 35,
	.D4 = 32,
	.D5 = 33,
	.D6 = 25,
	.D7 = 26,
	.XCLK = 15,		// 27 にすると何故かWiFiが動かなくなる。
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

//********* カメラ解像度指定 ***************
//#define CAM_RES			VGA			// カメラ解像度
//#define CAM_WIDTH		640			// カメラ幅
//#define CAM_HEIGHT	480			// カメラ高さ
//#define CAM_DIV			 12			// １画面分割数

//#define CAM_RES			CIF			// カメラ解像度
//#define CAM_WIDTH		352			// カメラ幅
//#define CAM_HEIGHT	288			// カメラ高さ
//#define CAM_DIV				4			// １画面分割数

//#define CAM_RES			QVGA		// カメラ解像度
//#define CAM_WIDTH		320			// カメラ幅
//#define CAM_HEIGHT	240			// カメラ高さ
//#define CAM_DIV				3			// １画面分割数

#define CAM_RES			QCIF		// カメラ解像度
#define CAM_WIDTH		176			// カメラ幅
#define CAM_HEIGHT	144			// カメラ高さ
#define CAM_DIV				1			// １画面分割数

//#define CAM_RES			QQVGA		// カメラ解像度
//#define CAM_WIDTH		160			// カメラ幅
//#define CAM_HEIGHT	120			// カメラ高さ
//#define CAM_DIV				1			// １画面分割数

//******************************************

OV7670 cam;						// camera
WiFiServer server(80);
WiFiClient WSclient;
boolean WS_on = false;		// WS設定が済んだかどうかのフラグ
WiFiMulti wifiMulti;

bool wifi_connect(){
	wifiMulti.addAP("your-ssid_1", "password_1");		// 接続予定のWiFiアクセスポイントを登録
	wifiMulti.addAP("your-ssid_2", "password_2");
	wifiMulti.addAP("your-ssid_3", "password_3");


	Serial.println(F("Connecting Wifi..."));
	if(wifiMulti.run() == WL_CONNECTED) {
			WiFi.config( myIP, myGateway, IPAddress(255,255,255,0));	//固定IPアドレスにする
		
			Serial.println(F("--- WiFi connected ---"));
  		Serial.print(F("SSID: "));
  		Serial.println( WiFi.SSID() );
			Serial.print(F("IP Address: "));
			Serial.println( WiFi.localIP() );
  		Serial.print(F("signal strength (RSSI): "));
  		Serial.print( WiFi.RSSI() );		// 信号レベル
  		Serial.println(F("dBm"));			
		return true;
	}
	else return false;
}

const char *html_head = "HTTP/1.1 200 OK\r\n"
	"Content-type:text/html\r\n"
	"Connection:close\r\n"		
	"\r\n"		//１行空行
	"<!DOCTYPE html>\n"
	"<html lang='ja'>\n"
	"<head>\n"
	"<meta charset='UTF-8'>\n"
	"<meta name='viewport' content='width=device-width'>\n"
	"<title>OV7670 Live</title>\n"
	"</head>\n"
	"<body>\n";
	
const char *html_body =	
	"<div id='msg' style='font-size:25px; color:#FF0000;'> Websocket connecting... </div>\n"
	"<font size='2'><span id='msgIn' style='font-size:20px; color:#FF0000;'>      </span></font>\n"
	"<script language='javascript' type='text/javascript'>\n"
	"var wsUri = 'ws://";
	
const char *html_script = ""	
	"var socket = null;\n"
	"var tms;\n"		
	"var msgIn;\n"
	"var msg;\n"		
	"var ctx;\n"
	"var width;\n"
	"var height;\n"	
	"var imageData;\n"		
	"var pixels;\n"	
	"var fps = 0;\n"
					
	"window.onload = function(){\n"
 	"	msgIn = document.getElementById('msgIn');\n"
 	"	msg = document.getElementById('msg');\n"
 	"	var c = document.getElementById('cam_canvas');\n"
 	"	ctx = c.getContext('2d');\n"
 	"	width = c.width;\n"
 	"	height = c.height;\n" 	
  "	imageData = ctx.createImageData( width, 1 );\n"
  "	pixels = imageData.data;\n"   	
 	"	setTimeout('ws_connect()', 1000);\n"
  "}\n"
  	
  "function Msg(message){ msg.innerHTML = message;}\n"						
 	
	"function ws_connect(){\n"	
	"	tms = new Date();\n"		
	"	if(socket == null){\n"
  "		socket = new WebSocket(wsUri);\n"
	"		socket.binaryType = 'arraybuffer';\n"    
//"		socket.binaryType = 'Blob';\n"    
  "		socket.onopen 	= function(evt){ onOpen(evt) };\n"
  "		socket.onclose 	= function(evt){ onClose(evt) };\n"
  "		socket.onmessage = function(evt){ onMessage(evt) };\n"
  "		socket.onerror 	= function(evt){ onError(evt) };\n"
  "	}\n"
 	"	setTimeout('fpsShow()', 1000);\n"  
  "}\n"
    
  "function onOpen(evt){ Msg('CONNECTED');}\n"
  "function onClose(evt){ Msg('WS.Close.DisConnected ' + evt.code +':'+ evt.reason); WS_close();}\n"
  "function onError(evt){ Msg(evt.data);}\n"

 	"function onMessage(evt){\n"
  "	var data = evt.data;\n"
 	"	if( typeof data == 'string'){\n"
	"		msgIn.innerHTML = data;\n"   	
 	"	}else if( data instanceof ArrayBuffer){\n"
 	"		drawLine(evt.data);\n"
 	"	}else if( data instanceof Blob){\n"
  "		Msg('Blob data received');\n"   		
  "	}\n"
 	"}\n"		

  "function WS_close(){\n"
  "	socket.close();\n"
  "	socket = null;\n"
  "	setTimeout('ws_connect()', 1);\n"	// 1m秒後に再接続を試みる
  "}\n"
  
	"function fpsShow(){\n"		// 1秒間に何フレーム表示出来たかを表示
	"	msgIn.innerHTML = String(fps)+'fps';\n"
	" fps = 0;\n"
 	"	setTimeout('fpsShow()', 1000);\n"
 	"}\n"
  
  "function drawLine(data){\n"
	"	var buf = new Uint16Array(data);\n" 	
  "	var lineNo = buf[0];\n"
//	"	Msg(String(lineNo));\n"
	"	for(var y = 0; y < (buf.length-1)/width; y+=1){\n"   	
	"		var base = 0;\n"
	"		for(var x = 0; x < width; x += 1){\n"
  "			var c = 1 + x + y * width;\n"   		
  "			pixels[base+0] = (buf[c] & 0xf800) >> 8 | (buf[c] & 0xe000) >> 13;\n"			// Red
  "			pixels[base+1] = (buf[c] & 0x07e0) >> 3 | (buf[c] & 0x0600) >> 9;\n"			// Green
  "			pixels[base+2] = (buf[c] & 0x001f) << 3 | (buf[c] & 0x001c) >> 2;\n"			// Blue
  "			pixels[base+3] = 255;\n" 	// Alpha
	"			base += 4;\n"
  "		}\n"
	"		ctx.putImageData(imageData, 0, lineNo + y);\n"
	"	}\n"
	"	if(lineNo + y == height) fps+=1;\n"	
  "}\n"
	"</script>\n"    
	"</body>\n"
	"</html>\n";

void printHTML(WiFiClient &client){
		Serial.println("sendHTML ...");
		client.print(html_head);
		Serial.println("head done");

		client.print(F("<canvas id='cam_canvas' width='"));
		client.print( CAM_WIDTH );
		client.print(F("' height='"));
		client.print( CAM_HEIGHT );
		client.println(F("'></canvas>\n"));

		client.print( html_body );
		client.print( WiFi.localIP() );
		client.println(F("/';"));
		Serial.println("body done");
		client.println(html_script);
		Serial.println("sendHTML Done");		
}

//************ Hash sha1 base64 encord ****************************
String Hash_Key( String h_req_key ){
	unsigned char hash[20];

	String str = h_req_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";	
	esp_sha( SHA1, (unsigned char*)str.c_str(), str.length(), hash );
	str = base64::encode( hash,	20 );
	return str;	
}
//*****************************************************************

void WS_handshake( WiFiClient &client ){
	String req;
  String hash_req_key;
	
	Serial.println(F("-----from Browser HTTP WebSocket Request---------"));
	//ブラウザからのリクエストで空行（\r\nが先頭になる）まで読み込む
	do{
		req = client.readStringUntil('\n');		//\nまで読み込むが\n自身は文字列に含まれず、捨てられる
		Serial.println(req);
		if(req.indexOf("Sec-WebSocket-Key") >= 0){
			hash_req_key = req.substring(req.indexOf(':')+2,req.indexOf('\r'));
			Serial.println();
			Serial.print(F("hash_req_key ="));
			Serial.println( hash_req_key );
		}        
	}while(req.indexOf("\r") != 0);
	
	req ="";
	delay(10);

	//-------ここからHTTPレスポンスのHTMLとJavaScriptコード
	Serial.println(F("---send WS HTML..."));
	String str = "HTTP/1.1 101 Switching Protocols\r\n";
	str += "Upgrade: websocket\r\n";
	str += "Connection: Upgrade\r\n";
	str += "Sec-WebSocket-Accept: ";
	str += 	Hash_Key( hash_req_key );	// hash -> BASE64エンコードキー
	str += "\r\n\r\n";								// 空行は必須
	Serial.println(str);
	client.print(str);								// client に送信
	str = "";
	WSclient = client;
}

void Ini_HTTP_Response(void){
	int pos;
	bool valueRead = false;
	uint8_t gain;
	String req;
	
	WiFiClient client = server.available();				// サーバーに対して外部から接続があるかどうかを監視
	if(!client) return;														// クライアントからのアクセス要求があり、接続が確立し、読み取りが出来る状態になるとtrue

	while(client.connected()){										// クライアントが接続状態の間
		if(!client.available()) break;							// 読み取り可能バイトが無いなら終了
		Serial.println(F("----Client Receive----"));		
		req = client.readStringUntil('\n');					// １行読み込み

		if(req.indexOf("GET / HTTP") != -1){				// ブラウザからリクエストを受信したらこの文字列を検知する
			while(req.indexOf("\r") != 0){						// ブラウザからのリクエストで空行（\r\nが先頭になる）まで読み込む
				req = client.readStringUntil('\n');			// \nまで読み込むが\n自身は文字列に含まれず、捨てられる
				Serial.println(req);
       	if(req.indexOf("websocket") != -1){
     			Serial.println(F("\nPrint WS HandShake---"));					
					WS_handshake(client);									// WS 続きの読込 & 送信
					WS_on = true;													// ws 設定終了フラグ
					return;
				}
			}				
      delay(10);																// 10ms待ってレスポンスをブラウザに送信
   		Serial.println(F("\nPrint HTML-----------"));
   		printHTML(client);												// レスポンス(HTML)を返す
   		Serial.println(F("\nPrint HTML end-------"));
		}
		else{																				// その他のリクエスト（faviconなど）全部読み飛ばす
			Serial.println(F("*** Anather Request ***"));
			Serial.print(req);
			while(client.available()){
				Serial.write(client.read());						// ブラウザからデータが送られている間読み込む
			}
		}
		if(!WS_on){
			delay(1);				//これが重要！これが無いと切断できないかもしれない。
			client.stop();	//一旦ブラウザとコネクション切断する。
			delay(1);
     	Serial.println(F("===== Client stop ====="));
     	req = "";
		}
	}
}

#define WS_FIN		0x80
#define OP_TEXT		0x81
#define OP_BIN		0x82
#define OP_CLOSE	0x88
#define OP_PING		0x89
#define OP_PONG		0x8A
#define WS_MASK 	0x80;

uint8_t *WScamData;
uint16_t data_size;
uint16_t line_size;
uint16_t line_h;

bool setImgHeader(uint16_t w, uint16_t h){
	line_h = h;
	line_size = w * 2;
	data_size = 2 + line_size * h;								// (LineNo + img) バイト数
	WScamData = (uint8_t*)malloc(data_size + 4);	// + head size
	if(WScamData == NULL){
		Serial.println(F("******** Memory allocate Error! ***********"));
		return false;
	}
	Serial.println("WS Buffer Keep OK");
	WScamData[0] = OP_BIN;											// バイナリデータ送信ヘッダ
	WScamData[1] = 126;													// 126:この後に続く２バイトがデータ長。127なら８バイトがデータ長
	WScamData[2] = (uint8_t)(data_size / 256);	// 送信バイト数(Hi)
	WScamData[3] = (uint8_t)(data_size % 256);	// 送信バイト数(Lo)
	return true;	
}

#define UNIT_SIZE	2048

void WS_sendImg(uint16_t lineNo)
{
	uint16_t len, send_size;
	uint8_t *pData;

	WScamData[4] = (uint8_t)(lineNo % 256);
	WScamData[5] = (uint8_t)(lineNo / 256);

	len = data_size + 4;
	pData =  WScamData;
	while(len){
		send_size = (len > UNIT_SIZE) ? UNIT_SIZE : len;		
		WSclient.write(pData, send_size );				// websocketデータ送信 ( UNITサイズ以下に区切って送る )
		len -= send_size;
		pData += send_size;
	}
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("OV7670 Web")); 
	Wire.begin();
	Wire.setClock(400000);

	WS_on = false;
 	if(wifi_connect()){ 
			server.begin();			// クライアントの接続待ち状態にする
 	}
  Serial.println(F("---- cam init ----")); 	
  esp_err_t err = cam.init(&cam_conf, CAM_RES, RGB565);		// カメラを初期化
	if(err != ESP_OK){
		Serial.println(F("cam.init ERROR"));
		while(1);
	}
//	cam.setPCLK(2, DBLV_CLK_x4);					// PCLK = 10MHz / (pre+1) * 4 Hz  13.3MHz
	cam.vflip( false );		// 画面１８０度回転
	 
  Serial.printf("cam MID = %X\n\r",cam.getMID());
  Serial.printf("cam PID = %X\n\r",cam.getPID());
  
//	cam.colorbar(true);
  Serial.println(F("---- cam init done ----"));
}

void loop(void) {
	uint16_t y,dy;
	
	dy = CAM_HEIGHT / CAM_DIV;					// １度に送るライン数
	setImgHeader( CAM_WIDTH, dy );			// Websocket用ヘッダを用意

	while(1){
		for( y = 0; y < CAM_HEIGHT; y += dy){			
			cam.getLines( y+1 , &WScamData[6] , dy);	// カメラから dyライン分得る。LineNo(top:1)
			if(WS_on){
				if(WSclient){
					WS_sendImg(y);												// Websocket 画像送信
				}
				else{
					WSclient.stop();											// 接続が切れたら、ブラウザとコネクション切断する。
					WS_on = false;
   				Serial.println(F("====< Client Stop >===="));
				}
			}
		}
	  if(!WS_on){
  	  Ini_HTTP_Response();
  	}
	}
	free(WScamData);
}

