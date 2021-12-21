#include <M5Stack.h>

#define AMG88_ADDR 0x69 //AMG8833 I2Cアドレス
#define FPSC 0x02 // フレームレートレジスター
#define INTC 0x03 // 割り込み制御レジスター
#define AVE 0x07 // アベレージレジスター
#define T01L 0x80 // 温度レジスター先頭

float gain = 10.0; //シグモイド関数 ゲイン
float offset_x = 0.2; // オフセット
float offset_green = 0.6; // 緑カーブ用オフセット

int min_t=0; //色表示温度下限(初期値）
int max_t=50; //色表示温度上限(初期値）
int modeSet=0; //釦操作モード　0:---　１：MAX　２：MIN

int realMAX=-50; //最大測定値(計算時スタート数値)
int realMIN=125; //最小測定値(計算時スタート数値)

float temp[64]; //計算結果の温度（℃）保存用配列 ０～６３
float temp2[8][8]; //1次元温度配列ー>2次元配列化　　０～７

const int scale=8; //拡大倍率 (30 -> 15 -> 8) 描画を早くする
const float scale_f=8.0; //　　　　（実数計算用） (30 -> 15 -> 8) 描画を早くする

//-----------　AMG8833　レジスターに書き込み　----------------
// id:AMG8833 I2Cアドレス
// reg:書き込み先レジスター
// data:書き込む内容
//
void write8(int id, int reg, int data) {
Wire.beginTransmission(id);
Wire.write(reg);
Wire.write(data);
uint8_t result = Wire.endTransmission();
}

//-----------　AMG8833　温度データ連続読み取り　----------------
// id:AMG8833 I2Cアドレス
// reg:読み出しレジスターの先頭位置
// *data:読み込んだデータを保存する配列名
// datasize:連続読み込みバイト数
//
void dataread(int id,int reg,int *data,int datasize) {
Wire.beginTransmission(id);
Wire.write(reg);
Wire.endTransmission();

Wire.requestFrom(id, datasize);
int i = 0;
while (Wire.available() && i < datasize) {
data[i++] = Wire.read();
}
}

//-----------　LCD右上に釦モード表示　-------------------------
void disp_mode(){
M5.Lcd.setTextSize(2); //文字サイズを一時的に大きくする
M5.Lcd.setCursor(250,10);
M5.Lcd.print("mode:");
M5.Lcd.setCursor(280,30);
switch(modeSet){
case 0:M5.Lcd.print("---");
break;
case 1:M5.Lcd.print("MAX");
break;
case 2:M5.Lcd.print("MIN");
break;
default:
break;
}
M5.Lcd.setTextSize(1); //文字サイズを元に戻す
}

//-----------　LCD右上に表示上限温度表示　-------------------------
void disp_max(){
M5.Lcd.setTextSize(2);
M5.Lcd.setCursor(250,50);
M5.Lcd.print("max:");
M5.Lcd.setCursor(280,70);
M5.Lcd.print(" ");
M5.Lcd.setCursor(280,70);
M5.Lcd.print( max_t);
M5.Lcd.setTextSize(1);
}

//-----------　LCD右上に表示下限温度表示　-------------------------
void disp_min(){
M5.Lcd.setTextSize(2);
M5.Lcd.setCursor(250,90);
M5.Lcd.print("min:");
M5.Lcd.setCursor(280,110);
M5.Lcd.print(" ");
M5.Lcd.setCursor(280,110);
M5.Lcd.print(min_t);
M5.Lcd.setTextSize(1);
}

//-----------　LCD右下に測定値(最大・最小）表示　-------------------------
void disp_realMaxMin(){
M5.Lcd.setTextSize(2); //文字サイズを一時的に大きくする
M5.Lcd.setCursor(280,190);
M5.Lcd.print(" ");
M5.Lcd.setCursor(280,190);
M5.Lcd.print(realMAX);
M5.Lcd.setCursor(280,220);
M5.Lcd.print(" ");
M5.Lcd.setCursor(280,220);
M5.Lcd.print(realMIN);
M5.Lcd.setTextSize(1); //文字サイズを元に戻す
}

//------------　conv() 1次元配列　2次元配列変換　------------------------------
void conv(){
for(int i=0;i<64;i++){
temp2[7-i/8][7-i%8]=temp[i]; //temp2[y][x] i=0 - 63
}
}

//------------　シグモイド曲線計算　------------------------------
// x:入力値
// g:ゲイン
// o:入力値オフセット
//
float sigmoid(float x, float g, float o) {
return (tanh((x + o) * g / 2) + 1) / 2;
}

//------------　Bilinear計算　------------------------------
float Bilinear(int y,int x){
int x0=x/scale;
int x1=x0+1;
int y0=y/scale;
int y1=y0+1;
float a=(float)(x%scale)/scale_f;
float b=(float)(y%scale)/scale_f;
float dotData=a*b*temp2[y1][x1]+(1-a)*b*temp2[y1][x0]+a*(1-b)*temp2[y0][x1]+(1-a)*(1-b)*temp2[y0][x0];
return dotData;
}

//------------　シグモイド変換ー>表示色 計算　------------------------------
uint16_t heat(float x) { //x: 0 ～1.0の間
x = x * 2 - 1; // -1 <= x < 1 に変換

float r = sigmoid(x, gain, -1 * offset_x);
float b = 1.0 - sigmoid(x, gain, offset_x);
float g = sigmoid(x, gain, offset_green) + (1.0 - sigmoid(x, gain, -1 * offset_green)) - 1;
//RGB565形式にデータ整形しリターン
return (((int)(r * 255)>>3)<<11) | (((int)(g * 255)>>2)<<5) | ((int)(b * 255)>>3);
}
//------------　温度ー>表示色　計算　------------------------------
uint16_t rgb565(float t){
return heat(map(constrain((int)t, min_t, max_t), min_t, max_t, 0, 100) / 100.0);
}

//------------　釦　検出と操作　------------------------------
void button(){
if(M5.BtnA.wasPressed()){ //左釦押して　釦無効／上限温度設定／下限温度設定　切替
modeSet++; //０：釦無効　１：上限温度　＋／－　２：下限温度　＋／－
if(modeSet >=3) modeSet=0;
disp_mode();
}
if(M5.BtnB.wasPressed()){ //B(真ん中）釦　押されたら　－１
switch(modeSet){
case 0:
break;
case 1: //上限温度 -1
if(max_t > 0) max_t--;
disp_max();
break;
case 2: //下限温度 -1
if(min_t > -25) min_t--;
disp_min();
break;
default:
break;
}
}

if(M5.BtnC.wasPressed()){ //C(右）釦　押されたら　＋１
switch(modeSet){
case 0:
break;
case 1: //上限温度 ＋1
if(max_t < 125) max_t++;
disp_max();
break;
case 2: //下限温度 ＋1
if(min_t < 50) min_t++;
disp_min();
break;
default:
break;
}
}
M5.update(); //釦操作後のおまじない
}

//------------　初期処理　--------------------------------------
void setup() {
M5.begin();
Wire.begin();
// Serial.begin(115200);
write8(AMG88_ADDR, FPSC, 0x00); // 10fps
write8(AMG88_ADDR, INTC, 0x00); // INT出力無効
write8(AMG88_ADDR, 0x1F, 0x50); // 移動平均出力モード有効
write8(AMG88_ADDR, 0x1F, 0x45);
write8(AMG88_ADDR, 0x1F, 0x57);
write8(AMG88_ADDR, AVE, 0x20); //0x20でon　0x00でOFF
write8(AMG88_ADDR, 0x1F, 0x00);

disp_mode(); //LCD右上に初期値表示
disp_max();
disp_min();
}

//------------　実行ループ　-----------------------------------
void loop() {

int sensorData[128]; //AMG8833から読み出したデータ
dataread(AMG88_ADDR, T01L, sensorData, 128);
//2バイトのセンサーデータから温度（℃）に変換
for (int i = 0 ; i < 64 ; i++) {
int16_t temporaryData = sensorData[i * 2 + 1] * 256 + sensorData[i * 2];
if(temporaryData > 0x200) {
temp[i] = (-temporaryData + 0xfff) * -0.25;
} else {
temp[i] = temporaryData * 0.25; //i=0　~　63　！
}
}

for (int i = 0 ; i < 64 ; i++) { //データの中から最小値・最大値抽出
if(temp[i] > realMAX) realMAX=temp[i];
if(temp[i] < realMIN) realMIN=temp[i];
}
disp_realMaxMin(); //LCD右下に表示
realMAX=-50; //1回ごとにクリアーする
realMIN=125; //コメントアウトするとピーク値となる
conv();
for(int j=8;j<56;j++){  // (0 - 240 -> 15 - 105 -> 8 - 56) 描画を早くする
for(int i=8;i<56;i++){  // (0 - 240 -> 15 - 105 -> 8 - 56) 描画を早くする
// M5.Lcd.drawPixel(i,j,rgb565(Bilinear(j,i))); //ここでRGB演算とドット表示
// M5.Lcd.fillRect(i*2,j*2,2,2,rgb565(Bilinear(j,i))); //ここでRGB演算とドット表示 (描画を早くする)
M5.Lcd.fillRect(i*4,j*4,2,2,rgb565(Bilinear(j,i))); //ここでRGB演算とドット表示 (描画を早くする)
}
button();
}
}
