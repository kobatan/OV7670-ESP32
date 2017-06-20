//*************************************************************************
//	OV7670 (non FIFO) Web Adjustment for ESP32 
//
//	line 22,23,82,83,84 は環境に合わせて各自設定変更してください。
//  line 48~71 でカメラ解像度を指定（コメントを外す）
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
#include "esp32-hal-ledc.h"

IPAddress myIP 			= IPAddress(192,168,1,99);	// 固定IPアドレス（各自に合わせて変更して下さい）
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
	.XCLK = 15,		// 27 にすると何故かwebsocket通信時に動かなくなる。
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

OV7670 cam;						// camera
WiFiServer server(80);
WiFiClient 	WSclient;	
bool WS_on = false;		// WS設定が済んだかどうかのフラグ
WiFiMulti wifiMulti;

bool wifi_connect(){
	wifiMulti.addAP("your-ssid_1", "password_1");		// 接続予定のWiFiアクセスポイントを登録
	wifiMulti.addAP("your-ssid_2", "password_2");
	wifiMulti.addAP("your-ssid_3", "password_3");

	Serial.println("Connecting Wifi...");
	if(wifiMulti.run() == WL_CONNECTED) {
		WiFi.config( myIP, myGateway, IPAddress(255,255,255,0));	//固定IPアドレスにする
		
		Serial.println(F("--- WiFi connected ---"));
 		Serial.print("SSID: ");
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

uint8_t v_gain = 0;
uint8_t v_awbb = 0;
uint8_t v_awbr = 0;
uint8_t v_awbg = 0;
uint16_t v_aec = 0;
int8_t v_bright = 0;
uint8_t v_cnt = 0;
uint16_t v_hstart = 0;
uint16_t v_vstart = 0;
bool b_agc = false;
bool b_awb = false;
bool b_aec = false;

const char *html_head = "HTTP/1.1 200 OK\r\n"
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
	"	.auto_width { display: inline-block;}\n"
	"</style>\n";

const char *html_script = ""	
	"var socket = null;\n"
	"var tms;\n"		
	"var msgIn;\n"
	"var msg;\n"		
	"var ctx;\n"
	"var width;\n"
	"var imageData;\n"		
	"var pixels;\n"	
				
	"window.onload = function(){\n"
 	"	msgIn = document.getElementById('msgIn');\n"
 	"	msg = document.getElementById('msg');\n"
 	"	var c = document.getElementById('cam_canvas');\n"
 	"	ctx = c.getContext('2d');\n"
 	"	width = c.width;\n"
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
  "}\n"
    
  "function onOpen(evt){ Msg('CONNECTED');doSend('m','WebSocket Open');}\n"
  "function onClose(evt){ Msg('WS.Close.DisConnected ' + evt.code +':'+ evt.reason); WS_close();}\n"
  "function onError(evt){ Msg(evt.data);}\n"

 	"function onMessage(evt){\n"
  "	var data = evt.data;\n"
 	"	if( typeof data == 'string'){\n"
 	"		var	vreg = document.getElementById('v_regdata');\n" 
 	"		var suji = parseInt(evt.data);\n"		// 文字を数値に変換
 	"		vreg.value = suji.toString(16);\n "	// 数値を１６進文字に変換
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

 	"function WS_send(str){\n"
	"	if( socket.readyState == 1){\n"
	"		if(	socket.bufferedAmount == 0){\n"
	"			socket.send(str);}}}\n"				

  "function WS_close(){\n"
  "	socket.close();\n"
  "	socket = null;\n"
  "	setTimeout('ws_connect()', 1);\n"	// 1m秒後に再接続を試みる
  "}\n"

  "function checkSend(id, boolEnable){if(boolEnable){ doSend(id,'1');}else{doSend(id,'0');}}\n"	

	"function toggleDisable(checkbox, field_id ) {\n"
	"	var toggle = document.getElementById(field_id);\n"
	"	checkbox.checked ? toggle.disabled = true : toggle.disabled = false;\n"
	"}\n"
	  					
	"function regRead(){\n"
	"	var hex = document.getElementById('v_regadr').value;\n"
	"	var adr = parseInt(hex,16);\n"
	"	doSend('D',adr);\n"
	"}\n"
	
	"function regWrite(){\n"
	"	var hex = document.getElementById('v_regadr').value;\n"
	"	var adr = parseInt(hex,16);\n"
	"	var hex = document.getElementById('v_regdata').value;\n"
	"	var d = parseInt(hex,16);\n"	
	"	var wd = (adr * 256) | d;\n"
	"	doSend('d',wd);\n"
	"}\n"
	  	
  "function drawLine(data){\n"
	"	var buf = new Uint16Array(data);\n" 	
  "	var lineNo = buf[0];\n"
	"	Msg(String(lineNo));\n"
	"	for(var y = 0; y < (buf.length-1)/width; y+=1){\n"   	
	"		var base = 0;\n"
	"		for(var x = 0; x < width; x += 1){\n"
  "			var c = 1 + x + y * width;\n"   		
  "			pixels[base+0] = (buf[c] & 0xf800) >> 8 | (buf[c] & 0xe000) >> 13;\n"			// Red
  "			pixels[base+1] = (buf[c] & 0x07e0) >> 3 | (buf[c] & 0x0600) >> 9;\n"			// Green
  "			pixels[base+2] = (buf[c] & 0x001f) << 3 | (buf[c] & 0x001c) >> 2;\n"			// Blue
  "			pixels[base+3] = 255;\n"  				// Alpha
	"			base += 4;\n"
  "		}\n"
	"		ctx.putImageData(imageData, 0, lineNo + y);\n"
	"	}\n"
  "}\n"
//	"-->\n"   	
	"</script>\n"    
	"</body>\n"
	"</html>\n";

void printHTML(WiFiClient &client){
		Serial.println(F("sendHTML ..."));

		client.println(html_head);
		Serial.println(F("HTML head done"));
	
		client.print(F("<canvas id='cam_canvas' width='"));
		client.print( CAM_WIDTH );
		client.print(F("' height='"));
		client.print( CAM_HEIGHT );
		client.println(F("'></canvas>\n"));

		client.print(F("<div id='msg' style='font-size:25px; color:#FF0000;'> Websocket connecting... </div>\n"));
		client.print(F("<div style='font-size:25px; color:#0080FF;'>Camera Setting</div>\n"));
		
		client.print(F("Bright:<input type='number' id='v_brt' name='BRT' style='width:56px' min='-128' max='127' value='"));
		client.print(v_bright);
		client.println(F("' onchange='doSend(\"t\",this.value);'>"));
	 	client.print(F("<input type='range' id='brt_slider' ontouchmove='document.getElementById(\"v_brt\").value=this.value;doSend(\"t\",this.value);' oninput='document.getElementById(\"v_brt\").value=this.value;doSend(\"t\",this.value);' style='width:100px' min='-128' max='127' step='1'	value='"));
	 	client.print(v_bright);
	 	client.println(F("'><br>"));

		client.print(F("Contrast:<input type='number' id='v_cnt' name='CNT' style='width:56px' min='0' max='255' value='"));
		client.print(v_cnt);
		client.println(F("' onchange='doSend(\"c\",this.value);'>"));
	 	client.print(F("<input type='range' id='cnt_slider' ontouchmove='document.getElementById(\"v_cnt\").value=this.value;doSend(\"c\",this.value);' oninput='document.getElementById(\"v_cnt\").value=this.value;doSend(\"c\",this.value);' style='width:100px' min='0' max='255' step='1'	value='"));
	 	client.print(v_cnt);
	 	client.println(F("'><br>"));

		client.print(F("ColorBar:<input type='checkbox' id='checkCLB' name='check_V' value='0' onchange=\"checkSend('l',this.checked);\" ><br>"));
		
		client.print(F("AGC:<input type='checkbox' id='checkAGC' name='check_V' value='1' onchange=\"checkSend('A',this.checked);toggleDisable(this,'field_gain');\" "));
		if( b_agc ) client.print(F("checked='checked'"));
		client.println(F("><br>"));
		
		client.print(F("<fieldset id='field_gain' class='auto_width' disabled>"));
		client.print(F("Gain:<input type='number' name='GAIN' id='v_gain' style='width:48px' min='0' max='1023' value='"));
		client.print(v_gain);
		client.println(F("' onchange='doSend(\"a\",this.value);'>"));
	 	client.print(F("<input type='range' id='gain_slider' ontouchmove='document.getElementById(\"v_gain\").value=this.value;doSend(\"a\",this.value);' oninput='document.getElementById(\"v_gain\").value=this.value;doSend(\"a\",this.value);' style='width:100px' min='0' max='1023' step='1'	value='"));
	 	client.print(v_gain);
	 	client.println(F("'><br>"));
		client.print(F("</fieldset><br>"));
		
		client.print(F("AWB:<input type='checkbox' id='checAWB' name='check_V' value='2' onchange=\"checkSend('W',this.checked);toggleDisable(this,'field_awb');\" "));
		if( b_awb ) client.print(F("checked='checked'"));		
		client.println(F("><br>"));
		
		client.print(F("<fieldset id='field_awb' class='auto_width' disabled>"));
		client.print(F(" R:<input type='number' id='v_awbr' name='AWBR' style='width:40px' min='0' max='255' value='"));
		client.print(v_awbr);
		client.println(F("' onchange='doSend(\"R\",this.value);'>"));
	 	client.print(F("<input type='range' id='awbr_slider' ontouchmove='document.getElementById(\"v_awbr\").value=this.value;doSend(\"R\",this.value);' oninput='document.getElementById(\"v_awbr\").value=this.value;doSend(\"R\",this.value);' style='width:100px' min='0' max='255' step='1'	value='"));
	 	client.print(v_awbr);
	 	client.println(F("'><br>"));

		client.print(F(" G:<input type='number' id='v_boxg' name='AWBG' style='width:40px' min='0' max='255' value='"));
		client.print(v_awbg);
		client.println(F("' onchange='doSend(\"G\",this.value);'>"));
	 	client.print(F("<input type='range' id='awbg_slider' ontouchmove='document.getElementById(\"v_boxg\").value=this.value;doSend(\"G\",this.value);' oninput='document.getElementById(\"v_boxg\").value=this.value;doSend(\"G\",this.value);' style='width:100px' min='0' max='255' step='1'	value='"));
	 	client.print(v_awbg);
	 	client.println(F("'><br>"));

	 	client.print(F(" B:<input type='number' id='v_awbb' name='AWBB' style='width:40px' min='0' max='255' value='"));
		client.print(v_awbb);
		client.println(F("' onchange='doSend(\"B\",this.value);'>"));
	 	client.print(F("<input type='range' id='awbb_slider' ontouchmove='document.getElementById(\"v_awbb\").value=this.value;doSend(\"B\",this.value);' oninput='document.getElementById(\"v_awbb\").value=this.value;doSend(\"B\",this.value);' style='width:100px' min='0' max='255' step='1'	value='"));
	 	client.print(v_awbb);
	 	client.println(F("'><br>"));
		client.print(F("</fieldset><br>"));
			
		client.print(F("AEC:<input type='checkbox' id='checkAEC' name='check_V' value='3' onchange=\"checkSend('E',this.checked);toggleDisable(this,'field_exp');\" "));
		if( b_aec ) client.print(F("checked='checked'"));		
		client.println(F("><br>"));
		
		client.print(F("<fieldset id='field_exp' class='auto_width' disabled>"));
		client.print(F("Exp:<input type='number' id='v_exp' name='EXP' style='width:56px' min='0' max='1000' value='"));
		client.print(v_aec);
		client.println(F("' onchange='doSend(\"e\",this.value);'>"));
	 	client.print(F("<input type='range' id='aec_slider' ontouchmove='document.getElementById(\"v_exp\").value=this.value;doSend(\"e\",this.value);' oninput='document.getElementById(\"v_exp\").value=this.value;doSend(\"e\",this.value);' style='width:100px' min='0' max='1000' step='1'	value='"));
	 	client.print(v_aec);
	 	client.println(F("'><br>"));
		client.print(F("</fieldset><br>"));
		
		client.print(F("HSTRT: <input type='number' id='v_boxhs' name='HSTART' style='width:56px' min='0' max='2047' value='"));
		client.print(v_hstart);
		client.println(F("' onchange='doSend(\"h\",this.value);'>"));
	 	client.print(F("<input type='range' id='hst_slider' ontouchmove='document.getElementById(\"v_boxhs\").value=this.value;doSend(\"h\",this.value);' oninput='document.getElementById(\"v_boxhs\").value=this.value;doSend(\"h\",this.value);' style='width:100px' min='0' max='2047' step='1'	value='"));
	 	client.print(v_hstart);
	 	client.println(F("'><br>"));

		client.print(F("VSTRT: <input type='number' id='v_boxvs' name='VSTART' style='width:50px' min='0' max='1023' value='"));
		client.print(v_vstart);
		client.println(F("' onchange='doSend(\"v\",this.value);'>"));
	 	client.print(F("<input type='range' id='vst_slider' ontouchmove='document.getElementById(\"v_boxvs\").value=this.value;doSend(\"v\",this.value);' oninput='document.getElementById(\"v_boxvs\").value=this.value;doSend(\"v\",this.value);' style='width:100px' min='0' max='1023' step='1'	value='"));
	 	client.print(v_vstart);
	 	client.println(F("'><br><br>"));

		client.println(F("REG:<input type='text' id='v_regadr' name='REGADR' style='width:40px' value='' title='16進数で入力'>"));
		client.println(F("<input type='button' value='Read' onclick='regRead()' style='width:50px; height:20px; font-size:12px;'>"));
		client.println(F("<input type='text' id='v_regdata' name='REGDATA' style='width:40px' value='' title='16進数で入力'>"));
		client.println(F("<input type='button' value='Write' onclick='regWrite()' style='width:50px; height:20px; font-size:12px;'>"));
			 	
		client.println(F("<br><input type='button' value='CAM Reset' onclick='doSend(\"i\",0);' id='WS_close' style='width:100px; height:20px; font-size:12px;'><br>"));
		client.println(F("<font size='2'><span id='msgIn' style='font-size:45px; color:#FF0000;'>      </span></font>"));

		client.println(F("<script language='javascript' type='text/javascript'>"));
//		client.println(F("<!--"));		

		client.print(F("var wsUri = 'ws://"));
		client.print( WiFi.localIP() );
		client.println(F("/';"));

		client.println(html_script);
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

	Serial.println("-----from Browser HTTP WebSocket Request---------");
	//ブラウザからのリクエストで空行（\r\nが先頭になる）まで読み込む
	do{
		req = client.readStringUntil('\n');		//\nまで読み込むが\n自身は文字列に含まれず、捨てられる
		Serial.println(req);
		if(req.indexOf("Sec-WebSocket-Key")>=0){
			hash_req_key = req.substring(req.indexOf(':')+2,req.indexOf('\r'));
			Serial.println();
			Serial.print(F("hash_req_key ="));
			Serial.println(hash_req_key);
		}        
	}while(req.indexOf("\r") != 0);
	req ="";
	delay(10);

	//-------ここからHTTPレスポンスのHTMLとJavaScriptコード
	Serial.println("---send WS HTML...");
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

long PingLastTime;
long PongLastTime;
long CountTestTime;
volatile byte cnt = 0;

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
			delay(1);
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
	WScamData[1] = 126;												// 126:この後に続く２バイトがデータ長。127なら８バイトがデータ長
	WScamData[2] = (uint8_t)(data_size / 256);	// 送信バイト数(Hi)
	WScamData[3] = (uint8_t)(data_size % 256);	// 送信バイト数(Lo)
	return true;	
}

#define UNIT_SIZE	1414							// websocketで１度に送信する最大バイト数

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
		WSclient.write(pData, send_size );									// websocketデータ送信 ( UNITサイズ以下に区切って送る )
		len -= send_size;
		pData += send_size;
	}
}

void WS_sendData(uint8_t data){
	char s[3];
	sprintf(s, "%03d", data);
	WSclient.write(OP_TEXT);	//データ送信ヘッダ
	WSclient.write(3);				//送信文字数
	WSclient.write(s, 3);
}

void WS_cmdCheck(void){
	uint8_t b=0;
	uint8_t data_len;
	uint8_t mask[4];
	uint8_t data_b;
	uint16_t data;
	uint8_t i;
	char id; 
	String str;
    
	if(!WSclient.available()) return;
    
	b = WSclient.read();
	if(b == OP_TEXT ){		 // textデータ受信
		Serial.println(F("Text data Receive *****"));

		b = WSclient.read();		// data length read
		data_len = b & 0x7f;	// マスクビット削除
		Serial.printf("data length=%d\r\n",data_len);
		if(data_len == 0) return;
		
		for(i=0; i<4; i++){					//マスクキーを読み込む
			mask[i] = WSclient.read();
			Serial.printf("mask:%0X\r\n",mask[i]);
		}
		byte m_data[data_len];	
		char data_c[data_len];
					
		Serial.print(F("Receive Data = "));            
		for(i = 0; i < data_len; i++){	// data read
			data_c[i] = WSclient.read();
			data_c[i] ^= mask[i%4];				//マスクキーとマスクデータをXOR演算すると実テキストデータが得られる
			Serial.print(data_c[i]);
		}
		Serial.println();

		id = data_c[0];
		Serial.print(F("ID = "));
		Serial.println(id);		
		data_len--;
		if(id != 'm'){
				data = toInt16( &data_c[1], data_len );
				Serial.printf("data = %d\r\n",data);
		}
		switch(id){
			case('m'):
				str = toStr( &data_c[1], data_len);
				Serial.println(str);			
				break;
			case('A'):
				cam.setAGC((uint8_t)data);
				break;
			case('a'):
				cam.setGain(data);
				break;
			case('W'):
				cam.setAWB((uint8_t)data);
				break;
			case('R'):
				cam.setAWBR((uint8_t)data);
				break;
			case('G'):
				cam.setAWBG((uint8_t)data);
				break;
			case('B'):
				cam.setAWBB((uint8_t)data);
				break;
			case('E'):
				cam.setAEC((uint8_t)data);
				break;
			case('e'):
				cam.setExposure(data);
				break;
			case('t'):
				cam.setBright((int8_t)data);
				break;
			case('c'):
				cam.setContrast(data);
				break;
			case('l'):
				cam.colorbar_super((bool)data);
				break;
			case('h'):
				cam.setHStart(data);
				break;
			case('v'):
				cam.setVStart(data);
				break;
			case('D'):
				data_b = cam.rdReg(data);
				WS_sendData(data_b);
				break;
			case('i'):
				cam.reset();
				break;
			case('d'):
//				Serial.printf("wrReg ad:%d data:%d\n",data/256, data%256);
				cam.wrReg((uint8_t)(data / 256), (uint8_t)(data % 256));
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
    
void setup() {
  Serial.begin(115200);
  Serial.println(F("OV7670 Web Adjust")); 
	Wire.begin();
	Wire.setClock(400000);
	
 	WS_on = false;
 	if(wifi_connect()){ 
			server.begin();			// クライアントの接続待ち状態にする
 	}
 	
  esp_err_t err = cam.init(&cam_conf, CAM_RES, RGB565);		// カメラを初期化 (PCLK 20MHz)
	if(err != ESP_OK)		Serial.println(F("cam.init ERROR"));
	cam.setPCLK(2, DBLV_CLK_x4);												// PCLK変更 : 10MHz / (pre+1) * 4 --> 13.3MHz  
	cam.vflip( false );		// 画面１８０度回転
	 
  Serial.printf("cam MID = %X\n\r",cam.getMID());
  Serial.printf("cam PID = %X\n\r",cam.getPID());

	v_gain = cam.getGain();
	v_awbb = cam.rdReg(REG_BLUE);
	v_awbr = cam.rdReg(REG_RED);
	v_bright = cam.getBright();		
	v_cnt = cam.getContrast();	
	v_hstart = cam.getHStart();
	v_vstart = cam.getVStart();
	b_agc = cam.getAGC();
	b_awb = cam.getAWB();
	b_aec = cam.getAEC();

//	cam.colorbar(true);

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
					WS_sendImg(y);							// Websocket 画像送信
					WS_cmdCheck();							// clientからのコマンドのやり取り
				}
				else{
					WSclient.stop();						// 接続が切れたら、ブラウザとコネクション切断する。
					WS_on = false;
   				Serial.println("Client Stop--------------------");
				}
			}
		}
	  if(!WS_on){
  	  Ini_HTTP_Response();
  	}
	}
	free(WScamData);
}

