# OV7670-ESP32
## OV7670 (non FIFO) Library for ESP32 Arduino

カメラモジュール OV7670(FIFO無し) の ESP32 Arduino用 ライブラリーを作ってみました。

https://github.com/igrr/esp32-cam-demo を参考に、OV7670用に変更しています。

FIFO無しでどこまで出来るか挑戦してみました。

ESP32のメモリーでもカメラの1フレーム（1画面）分を処理するのは難しかったので、1ラインづつ読込み表示するようにしています。

サンプルのTFT液晶に表示では、結構リアルタイムに表示出来ていると思います。

Live Camera にも挑戦しています。サンプルを実行して見て下さい。

Web 送信は　Websocket を使いました。これは[mgo-tec電子工作さんのブログ](https://www.mgo-tec.com/blog-entry-websocket-handshake.html "mgo-tec")をかなり参考にさせて頂きました。


サーボサンプルは、webから画像を見ながらカメラを上下左右に動かせるようにしています。

配線方法はサンプルプログラムのコードを見てもらえれば解るかと思います。

