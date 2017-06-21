# OV7670-ESP32
## OV7670 (non FIFO) Camera Library for ESP32 Arduino

<img src="https://user-images.githubusercontent.com/29562647/27361020-a03662d2-565f-11e7-810f-891ff1fdd118.jpg" width="300px">

カメラモジュール OV7670(FIFO無し) の ESP32 Arduino用 ライブラリーを作ってみました。

https://github.com/igrr/esp32-cam-demo を参考に、OV7670用に変更しています。

FIFO無しでどこまで出来るか挑戦してみました。

ESP32のメモリーでもカメラの1フレーム（1画面）分を処理するのは難しかったので、1ラインづつ読込み表示するようにしています。

<img src="https://user-images.githubusercontent.com/29562647/27361033-b3c29f0a-565f-11e7-800c-c119fbbc370f.jpg" width="300px"><img src="https://user-images.githubusercontent.com/29562647/27361038-b90c5d0c-565f-11e7-9309-e94112c63012.jpg" width="300px">

サンプルのTFT液晶に表示では、結構リアルタイムに表示出来ていると思います。

Live Camera にも挑戦しています。サンプルを実行して見て下さい。

Web 送信は　Websocket を使いました。これは[mgo-tec電子工作さんのブログ](https://www.mgo-tec.com/blog-entry-websocket-handshake.html "mgo-tec")をかなり参考にさせて頂きました。  
  
サーボサンプルは、webから画像を見ながらカメラを上下左右に動かせるようにしています。
  
配線方法はサンプルプログラムのコードを見てもらえれば解るかと思いますが一応。

| OV7670 | ESP32 |
|--------|-------|
|  D0    | IO36  |
|  D1    | IO39  |
|  D2    | IO34  |
|  D3    | IO35  |
|  D4    | IO32  |
|  D5    | IO33  |
|  D6    | IO25  |
|  D7    | IO26  |
|  XCLK  | IO27 or ather|
|  PCLK  | IO14  |
|  HREF  |  NC   |
|  VSYNC | IO13  |
|  SIOD  | IO21  |
|  SIOC  | IO22  |
|  RESET | 3.3V  |
|  PWDN  | GND   |

