//*************************************************************************
//	OV7670 (non FIFO) Web streamer for ESP32 
//	and Servo x 2 control
//
//	line 22,23,25,27,28,90,91,92 は環境に合わせて各自設定変更してください。
//  line 51~81 でカメラ解像度を指定（コメントを外す）
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
#include "libb64/cdecode.h"			// base64.hにはencodeしかまだないのでdecode用にinclude
#include <OV7670.h>
#include "esp32-hal-ledc.h"
#include <ArduinoOTA.h>

IPAddress myIP 			= IPAddress(192,168,1,99);	// 固定IPアドレス（各自に合わせて変更して下さい）
IPAddress myGateway = IPAddress(192,168,1, 1);

String webUserPass = "UserName:Password";				// Web閲覧用 ユーザー名:パスワード を「:」 を付けて設定してください

const char *OTA_name = "myESP32";		// OTA名
const char *OTA_pass = "";					// OTA用パスワード

#define SERVO1_PIN	16
#define SERVO2_PIN	17

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
	.xclk_freq_hz = 10000000,				// XCLK 10MHz
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

WiFiMulti wifiMulti;
WiFiServer server(80);
WiFiClient 	WSclient;	
OV7670 cam;						// camera

bool WS_on = false;		// WS設定が済んだかどうかのフラグ

void wifi_connect(){
	wifiMulti.addAP("your-ssid_1", "password_1");		// 接続予定のWiFiアクセスポイントを登録
	wifiMulti.addAP("your-ssid_2", "password_2");
	wifiMulti.addAP("your-ssid_3", "password_3");

	Serial.println(F("Connecting Wifi..."));
	while(1){
		if(wifiMulti.run() == WL_CONNECTED) {
			WiFi.config( myIP, myGateway, IPAddress(255,255,255,0));	//固定IPアドレスにする
			
			ArduinoOTA.setPort(8266);
			ArduinoOTA.setHostname( OTA_name );
			ArduinoOTA.setPassword( OTA_pass );
			ArduinoOTA.onStart([]() {
				String type;
				if (ArduinoOTA.getCommand() == U_FLASH)
					type = "sketch";
				else // U_SPIFFS
					type = "filesystem";

				// NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
				Serial.println("Start updating " + type);
			});
			ArduinoOTA.onEnd([]() {
				Serial.println("\nEnd");
			});
			ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
				Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
			});
			ArduinoOTA.onError([](ota_error_t error) {
				Serial.printf("Error[%u]: ", error);
				if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
				else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
				else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
				else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
				else if (error == OTA_END_ERROR) Serial.println("End Failed");
			});
			ArduinoOTA.begin();
			Serial.println(F("--- WiFi connected ---"));
  		Serial.print("SSID: ");
  		Serial.println( WiFi.SSID() );
			Serial.print(F("IP Address: "));
			Serial.println( WiFi.localIP() );
  		Serial.print("signal strength (RSSI): ");
  		Serial.print( WiFi.RSSI() );		// 信号レベル
  		Serial.println("dBm");			
			break;;
	 	}else{
	 		delay(5000);
	 		ESP.restart();	
	 	}
	}
}

const char *html_401 = "HTTP/1.1 401 Authorization Required\r\n"
//	"Server: Apache/2.0.52 (FreeBSD)\r\n"
	"WWW-Authenticate: Basic realm='Secret File'\r\n"
	"Content-type:text/html; charset=utf-8\r\n"
	"Connection:close\r\n"		
	"\r\n"	//１行空行が必要
	"<!DOCTYPE html>\n"
	"<html lang='ja'>\n"
	"<head>\n"
	"<title>401 パスワード認証</title>\n"
	"</head>\n"
	"<body>\n"
	"<h1>Authorization Required</h1>\n"
	"<p>This server could not verify that you are authorized to access the document requested.</p>\n"
	"</body>\n"
	"</html>\n";

const char *html_head =	"HTTP/1.1 200 OK\r\n"
	"Content-type:text/html\r\n"
	"Connection:close\r\n"		
	"\r\n"	//１行空行が必要
	"<!DOCTYPE html>\n"
	"<html lang='ja'>\n"
	"<head>\n"
	"<meta charset='utf-8'>\n"
	"<meta name='viewport' content='width=device-width'>\n"
	"<title>OV7670 Live</title>\n"
	"</head>\n"
	"<body>\n"
	"<style type='text/css'>\n"
	"	#vrange{ position:relative; right:60px;  width:144px; height:144px; margin:0; padding:0; -webkit-transform: rotate(-90deg); transform: rotate(-90deg);}\n"
	"	#hrange{ width:176px; height:20px; margin:0; padding:0;}\n"
	"</style>\n";

const char *html_script =
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

 	"function doSend(ch,data){\n"
  "	var str = String(\"\");\n"
  "	str += String(ch) + String(data);\n"
 	"	var mms = new Date();\n"
	"	if(mms-tms > 100){\n"
	"		WS_send(str);\n"
	"		tms = new Date();\n"
	"	}\n"
	"}\n"

	"function doSendNow(ch,data){\n"
  "	var str = String(\"\");\n"
  "	str += String(ch) + String(data);\n"
	"	WS_send(str);\n"
	"}\n"

 	"function WS_send(str){\n"
	"	if( socket.readyState == 1){\n"
	"		if(	socket.bufferedAmount == 0){\n"
	"			socket.send(str);}}}\n"

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
	"	for(var y = 0; y < (buf.length-1)/width; y += 1){\n"
	"		var base = 0;\n"
	"		for(var x = 0; x < width; x += 1){\n"
  "			var c = 1 + x + y * width;\n"
  "			pixels[base+0] = (buf[c] & 0xf800) >> 8 | (buf[c] & 0xe000) >> 13;\n"			// Red
  "			pixels[base+1] = (buf[c] & 0x07e0) >> 3 | (buf[c] & 0x0600) >> 9;\n"			// Green
  "			pixels[base+2] = (buf[c] & 0x001f) << 3 | (buf[c] & 0x001c) >> 2;\n"			// Blue
  "			pixels[base+3] = 255;\n"  // Alpha
	"			base += 4;\n"
  "		}\n"
	"		ctx.putImageData( imageData, 0, lineNo + y);\n"
	"	}\n"
	"	if(lineNo + y == height) fps+=1;\n"	
  "}\n"
	"</script>\n"
	"</body>\n"
	"</html>\n";
	
void printHTML(WiFiClient &client){
		client.println( html_head );

		client.print(F("<table><tr>"));
		client.print(F("<td><canvas id='cam_canvas' width='"));
		client.print( CAM_WIDTH );
		client.print(F("' height='"));
		client.print( CAM_HEIGHT );
		client.println(F("'></canvas></td>"));
		client.println(F("<td valign='middle'><input type='range' name='vrange' id='vrange' min='-90' max='90' value='0' ontouchmove='doSend(\"V\",this.value);' oninput='doSend(\"V\",this.value);' onchange='doSendNow(\"V\",this.value);'></td></tr>"));
		client.println(F("<tr><td align='center'><input type='range' name='hrange' id='hrange' min='-90' max='90' value='0' ontouchmove='doSend(\"H\",this.value);' oninput='doSend(\"H\",this.value);' onchange='doSendNow(\"H\",this.value);'></td><td></td></tr></table>"));
		client.println(F("<div id='msg' style='font-size:25px; color:#FF0000;'> Websocket connecting... </div>"));
		client.println(F("<font size='2'><span id='msgIn' style='font-size:20px; color:#0000FF;'> </span></font>"));

		client.println(F("<script language='javascript' type='text/javascript'>"));
		client.print(F("var wsUri = 'ws://"));
		client.print( WiFi.localIP() );
		client.println(F("';"));
		client.println( html_script );
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

bool userCheck( String key){
	String user;
	char * buffer = (char *) malloc(30);	

	base64_decode_chars((char*)key.c_str(), key.length(), &buffer[0]);	// base64 をデコードする
 	user = String(buffer);
 	free(buffer);	
	Serial.println(user);

	if(user == webUserPass){
		Serial.println("User check OK!");
		return true;
	}
	else{
		Serial.println("User check NG!");
		return false;
	}
}

void WS_handshake( WiFiClient &client ){
	String req;
  String hash_req_key;

	Serial.println(F("-----from Browser HTTP WebSocket Request---------"));
	do{
		req = client.readStringUntil('\n');		// \nまで読み込むが\n自身は文字列に含まれず、捨てられる
		Serial.println(req);
		if(req.indexOf("Sec-WebSocket-Key") >= 0){
			hash_req_key = req.substring(req.indexOf(':')+2,req.indexOf('\r'));
			Serial.print(F("hash_req_key ="));
			Serial.println(hash_req_key);
		}        
	}while( req.indexOf("\r") != 0 );				// ブラウザからのリクエストで空行（\r\n）が先頭になるまで読み込む
	req ="";
	delay(10);

	//-------ここからHTTPレスポンス------------------------
	Serial.println(F("---send WS HTTP..."));
	String str = "HTTP/1.1 101 Switching Protocols\r\n";
	str += "Upgrade: websocket\r\n";
	str += "Connection: Upgrade\r\n";
	str += "Sec-WebSocket-Accept: ";
	str += 	Hash_Key( hash_req_key );		// hash -> BASE64エンコードキー
	str += "\r\n\r\n";									// 空行は必須
	Serial.println(str);
	client.print(str);									// client に送信
	str = "";
	WSclient = client;
}

IPAddress clientIP;

void Ini_HTTP_Response(void){
	int pos;
	bool valueRead = false;
	uint8_t gain;
	String req;
  String key;
  bool auth_flag = false;
  	
	WiFiClient client = server.available();				// サーバーに対して外部から接続があるかどうかを監視
	if(!client) return;														// クライアントからのアクセス要求があり、接続が確立し、読み取りが出来る状態になるとtrue

	Serial.print(F("---- Client Receive from : "));	
	clientIP = client.remoteIP();
	Serial.println( clientIP );										// 接続してきた相手のIPアドレスを表示

	while(client.connected()){										// クライアントが接続状態の間
		if(!client.available()) break;							// 読み取り可能バイトが無いなら終了

		req = client.readStringUntil('\n');					// 最初の１行読み込み
		Serial.println(req);
		if(req.indexOf("GET / HTTP") != -1){				// ブラウザからリクエストを受信したらこの文字列を検知する
			while(req.indexOf("\r") != 0){						// ブラウザからのリクエストで空行（\r\n）が先頭になるまで繰り返す
				req = client.readStringUntil('\n');			// １行読み込み
				Serial.println(req);
       	if(req.indexOf("Authorization: Basic") != -1){// "Authorization: Basic"と言う文字が見つかったらユーザ認証パスワード確認
     			Serial.println(F("\nPrint 401 request ---"));					
					key = req.substring(req.indexOf(':')+8,req.indexOf('\r'));	// [username:password] BASE64 key
					if( userCheck(key) ) auth_flag = true;		// ユーザー認証 
				}
       	if(req.indexOf("websocket") != -1){			// "websocket"と言う文字が見つかったらハンドシェイクを始める
     			Serial.println(F("\nPrint WS HandShake---"));					
					WS_handshake(client);									// WS 続きの読込 & 送信
					WS_on = true;													// WS 設定完了フラグ
					return;
				}
			}				
      delay(10);																// 10ms待ってレスポンスをブラウザに送信
   		if(!auth_flag){ 
   			Serial.println(F("\nPrint 401 HTML-----------"));
				client.println( html_401 );								// ユーザー認証401(HTML)を要求する
   			Serial.println(F("\nPrint 401 HTML end-------"));
   		}
   		else{
    		Serial.println(F("\nPrint HTML-----------"));
   			printHTML(client);												// レスポンス(HTML)を返す
   			Serial.println(F("\nPrint HTML end-------"));
   		}
		}
		else{																				// その他のリクエスト（faviconなど）全部読み飛ばす
			Serial.println(F("*** Anather Request ***"));
			while(client.available()){
				Serial.write(client.read());						// ブラウザからデータが送られている間読み込む
			}
		}
		if(!WS_on){
			delay(10);			//
			client.stop();	// 一旦ブラウザとコネクション切断する。
     	Serial.println("===== Client stop =====");
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
#define WS_MASK 	0x80

uint16_t toInt16(char *buf, uint8_t len){
	uint16_t data = 0;
	uint8_t i;
	
	for(i = 0; i < len; i++){						// data read
		data *= 10;
		data += buf[i] -'0';
	}
	return data;
}

String toStr(char *buf, uint8_t len){
	String str = "";
	uint8_t i;
	
	for(i = 0; i < len; i++){						// data read
		str += buf[i];
	}
	return str;
}

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
	WScamData[0] = OP_BIN;										// バイナリデータ送信ヘッダ
	WScamData[1] = 126;												// 126:この後に続く２バイトがデータ長,127なら８バイトがデータ長
	WScamData[2] = (uint8_t)(data_size / 256);	// 送信バイト数[Hi]
	WScamData[3] = (uint8_t)(data_size % 256);	// 送信バイト数[Lo]
	return true;	
}

#define UNIT_SIZE	1414	// websocketで１度に送信する最大データバイト数 (MSS)
												// こちらの値を参考にした http://www.speedguide.net/analyzer.php
void WS_sendImg(uint16_t lineNo)
{
	uint16_t len, send_size;
	uint8_t *pData;

	WScamData[4] = (uint8_t)(lineNo % 256);
	WScamData[5] = (uint8_t)(lineNo / 256);

	len = data_size + 4;	// 4 = header size
	pData =  WScamData;
	while(len){
		send_size = (len > UNIT_SIZE) ? UNIT_SIZE : len; 	// UNITサイズ以下に区切って送る	
		WSclient.write(pData, send_size );								// websocketデータ送信					
		len -= send_size;
		pData += send_size;
	}
}

void WS_cmdCheck(void){
	uint8_t b=0;
	uint8_t data_len;
	uint8_t mask[4];
	uint8_t i;
	char id; 
	
	if(!WSclient.available()) return;
    
	b = WSclient.read();
	if( b == OP_TEXT ){		 				// text データ受信
		Serial.println(F("Text data Receive *****"));
		
		b = WSclient.read();				// data length read
		data_len = b & 0x7f;				// マスクビット削除
		Serial.printf("data length=%d\r\n",data_len);
		if(data_len == 0) return;
		
		for(i=0; i<4; i++){					//マスクキーを読み込む
			mask[i] = WSclient.read();
			Serial.printf("mask:%0X\r\n",mask[i]);
		}	

		char data_c[data_len];
					
		Serial.print(F("Receive Data = "));            
		for(i = 0; i < data_len; i++){	// data read
			data_c[i] = WSclient.read();
			data_c[i] ^= mask[i%4];				//マスクキーとマスクデータをXOR演算すると実テキストデータが得られる
			Serial.print(data_c[i]);
		}
		Serial.println();

		id = data_c[0];
		Serial.print("ID = ");
		Serial.println(id);		
		data_len--;
		int data = atoi( &data_c[1] );
		Serial.printf("data = %d\r\n",data);
		switch(id){
			case('H'):
				servo_move( 0, (float)data );		// 縦方向サーボを動かす
				break;
			case('V'):
				servo_move( 1, (float)data );		// 横方向サーボを動かす
				break;
			default:
				break;
		}
	}else if(b == OP_CLOSE){
		Serial.print(F("OP_CLOSE Received ******** OP_CODE:"));
		Serial.println(b,BIN);
		delay(1);
		WSclient.write(OP_CLOSE);
		delay(1);
		WSclient.stop();
		delay(1);
		Serial.println();
		Serial.println(F("Client.STOP-----------------"));
		WS_on = false;
	}
}

typedef struct{
	uint8_t ch;
	float cur_angle;
	float dist_angle;
	float speed;
	float offset;
	float dir;
}servo;

volatile servo sv[2];

void servo_setup(uint8_t no, uint8_t pin, uint8_t ch, float offset, float dir){
	pinMode( pin, OUTPUT );
	digitalWrite( pin, LOW);
	sv[no].ch = ch;
	sv[no].cur_angle = 0;
	sv[no].dist_angle = 0;
	sv[no].speed = 0.5;
	sv[no].offset = offset;
	if(dir>0) sv[no].dir = 1;
	else sv[no].dir = -1; 

	ledcSetup( ch, 50, 16);			// 16bit 50Hz(20ms)
	ledcAttachPin( pin, ch );
	ledcWrite( ch, pulse(no, 0) );
}

void servo_move( uint8_t no, float angle ){
	if(angle < -90 || angle >90 ) return;
	sv[no].dist_angle = angle;
}

uint16_t pulse(uint8_t no, float angle){		// angle = -90 ~ 9
		float a = angle * sv[no].dir;
		float p = ( 1450+( a + sv[no].offset )*1900/180 ) * 65535/20000;		// SD92R: range=(2400-500)=1900us, center=1450us
		return (uint16_t)p;
}

void servoTask( void *pvParameters ){
	int8_t i;
	float dif,d,c;
	
	while(1){
		for(i=0; i<2; i++){
			d = sv[i].dist_angle;
			c = sv[i].cur_angle;
			dif = d - c;
			if( dif != 0 ){
				if( dif > 0 ){
					c += sv[i].speed;
					if( c > d ) c = d;
				}
				else{
					c -= sv[i].speed;
					if( c < d ) c = d;
				}
				sv[i].cur_angle = c;
				ledcWrite( sv[i].ch, pulse(i,c) );
			}
		}
		vTaskDelay( 40 / portTICK_RATE_MS);
	}
}

void initGPIO(void){
	uint8_t i;
	uint8_t gpio[]={0,1,2,3,4,5,12,13,14,15,16,17,18,19,20,21,22,23,25,26,27,32,33};
	for(i=0; i<sizeof(gpio); i++){
		pinMode(gpio[i], OUTPUT);
		digitalWrite(gpio[i], LOW);
	}
}

void setup() {
//	initGPIO();
  Serial.begin(115200);
	Wire.begin();
	Wire.setClock(400000);

	WS_on = false;
 	wifi_connect(); 
	server.begin();			// クライアントの接続待ち状態にする
	
	servo_setup(0, SERVO1_PIN, 2, 0, -1);	// 左右
	servo_setup(1, SERVO2_PIN, 3, 0, 1);	// 上下
	xTaskCreate( servoTask, "servoTask", 1024, NULL, 1, NULL);
	
  Serial.println("---- cam init...");
  esp_err_t err = cam.init(&cam_conf, CAM_RES, RGB565);		// カメラを初期化 (PCLK 20MHz)
	if(err != ESP_OK){
		Serial.println("cam.init ERROR");
		while(1);
	}
//	cam.setPCLK(3, DBLV_CLK_x4);	// PCLK変更 : 10MHz / (pre+1) * 4 --> 10MHz (web速度に合わせて、読み取り速度を遅くする)  
	cam.vflip( false );		// 画面１８０度回転
	 
  Serial.printf("cam MID = %X\n\r",cam.getMID());
  Serial.printf("cam PID = %X\n\r",cam.getPID());

//	cam.colorbar(true);					// カラーバー出力テスト用
  Serial.println("---- cam init done ----");
}

void loop(void) {
	uint16_t y,dy;
	WiFiClient client;
	bool cam_ok;
		
	dy = CAM_HEIGHT / CAM_DIV;					// １度に送るライン数
	setImgHeader( CAM_WIDTH, dy );			// Websocket用ヘッダを設定

	while(1){
		for( y = 0; y < CAM_HEIGHT; y += dy){			
			cam_ok = cam.getLines( y+1 , &WScamData[6] , dy);	// カメラから dyライン分得る。LineNo(top:1)

			if(WS_on){
				if( WSclient.connected() ){
					if( cam_ok ) WS_sendImg(y);	// Websocket 画像送信
					WS_cmdCheck();							// clientからのコマンドのやり取り
				}
				else{
					WSclient.stop();						// 接続が切れたら、ブラウザとコネクション切断する。
					WS_on = false;
   				Serial.println(F("Client Stop-------------"));
				}
			}
		}
		if(!WS_on){
  	  Ini_HTTP_Response();						// WiFi切れたらHTML再送信
  	}
		ArduinoOTA.handle();							// OTA対応
	}
	free(WScamData);
}

