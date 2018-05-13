/*
  Weather station with the esp8266
    Get the Weather from intenet with esp8266 and display with the ssd3316

  weather data from:
  get data from 心知天气：http://www.thinkpage.cn/
  api 文档说明：http://www.thinkpage.cn/doc
  city id list download ：http://www.thinkpage.cn/data/thinkpage_cities.zip

  Use the library:
  SSD1306 oled driver library, https://github.com/adafruit/Adafruit_SSD1306
  arduino Json library, https://github.com/bblanchon/ArduinoJson
  Simple Timer library, http://playground.arduino.cc/Code/SimpleTimer
  
  Created by yfrobot, 2016.8.23
  Contact: 
  QQ - 2912630748
  email - finalvalue@yfrobot.com
  address - www.yfrobot.com
*/

#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <SimpleTimer.h>
#include "display_data.h"
#include <WiFiUdp.h>
#include <TimeLib.h>

#define DISPLAY_MODE 2 // 显示模式: 0--当前天气 , 1--3天天气预报 ,2--交替显示
#define NO_WIFI 31

WiFiClient client;
SimpleTimer timer;

const char *ssid = "你连我试试";      // XXXXXX -- 使用时请修改为当前你的 wifi ssid
const char *password = "dxw2dmy1314"; // XXXXXX -- 使用时请修改为当前你的 wifi 密码

const char *HOST = "api.thinkpage.cn";
const char *VERSION = "/v3/weather";
// API TEST  --  程序运行前点击下面链接，测试是否可以正常获取数据
// https://api.thinkpage.cn/v3/weather/now.json?key=dnowamq6txr8lzs6&location=dongguan&language=en
// https://api.thinkpage.cn/v3/weather/daily.json?key=dnowamq6txr8lzs6&location=huaian&language=en&start=0&days=5 接口变了

const char *NowJson = "/now.json?";     // 当前天气情况
const char *DailyJson = "/daily.json?"; // 3天天气预报
//dnowamq6txr8lzs6 hzxeeutv7puc0f4k
const char *APIKEY = "dnowamq6txr8lzs6"; // API KEY --- 心知天气 KEY 可以自行申请（15天免费试用 -- 有效期：9/5~9/20 ）
const char *CITY = "dongguan";           // city list -- 需要查询的城市
const char *LANGUAGE = "en";             // language -- 返回数据语言

const unsigned long BAUD_RATE = 115200;     // Baud rate
const unsigned long HTTP_TIMEOUT = 2100;    // max respone time from server
const size_t MAX_CONTENT_SIZE = 2048;       // max size of the HTTP response
const int32_t update_rate = 1000 * 60 * 30; // 获取网络数据频率  -- 默认每半小时获取一次数据（调试时可以更改此值）
const int16_t now_display_time = 5000;      // 当前天气显示时长
const int16_t daily_display_time = 5000;    // 预报天气显示时长
int32_t old_rssi = 10;                      // WIFI RSSI -- 初始值为10
boolean isConnected = false;                // WIFI connect flag
boolean isDisplayNow = false;               //判断是不是显示当前天气

//NTP对时服务器地址
static const char ntpServerName[] = "us.pool.ntp.org";
const int timeZone = 8; // 北京时区+8
//udp 协议
WiFiUDP Udp;
unsigned int localPort = 8888; // UDP本地监听端口

struct U_Date
{
  String date_d;
  String date_t;
};

// The type of data that we want to extract from the page -- 我们要从此网页中提取的数据的类型
struct UserData
{

  char city[16]; // 城市
  char cnty[16]; // 国家
  // Daily
  char date_1[16];          // 明天 日期
  char date_1_text_day[32]; // 白天天气文字
  int date_1_code_day;      // 白天天气代码
  char date_2[16];          // 后天 日期
  char date_2_text_day[32]; // 白天天气文字
  int date_2_code_day;      // 白天天气代码
  char date_3[16];          // 大后天 日期
  char date_3_text_day[32]; // 白天天气文字
  int date_3_code_day;      // 白天天气代码
  char udate_daily[32];     // 最后更新时间及日期
  // now
  char weather[32];   // 天气
  int weatherCode;    // 天气代码
  char temp[16];      // 温度
  char feel[16];      // 体感温度
  char pressure[8];   // 大气压力
  char hum[16];       // 相对湿度
  char visi[16];      // 能见度
  char wind[8];       // 风向                      http://file.yfrobot.com/weather/wind_directions.jpg-yf
  int windScale;      // 风力等级                  http://baike.baidu.com/view/465076.htm
  char udate_now[32]; // 最后更新时间及日期
  //... 风速等(更多资讯需要收费哦)
};

UserData userData_now;
UserData userData_daily;

// 0.96 OLED initialize
#define OLED_RESET D4 //4
Adafruit_SSD1306 display(OLED_RESET);

//确定OLED高度 -- 更改高度在 "Adafruit_SSD1306.h"
#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

void init_oled()
{
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
}

// 写警示文字
void drawWarning(String warn)
{
  // graphic commands to redraw the complete screen should be placed here
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(1);
  int x = (SSD1306_LCDWIDTH - warn.length() * 2 * 5) / 2;
  x < 0 ? 0 : x;
  display.setCursor(0, 0);
  display.println(warn);
  display.display();
}

// 画 YFROBOT logo
void drawlogo()
{
  // graphic commands to redraw the complete screen should be placed here
  display.clearDisplay();
  drawWarning("YFROBOT");
  display.drawXbm(20, (SSD1306_LCDHEIGHT - 16 - logoHeight) / 2 + 16,
                  logo, logoWidth, logoHeight, 1);
  display.display();
}

// 连接wifi 图标
void drawWifi(int x)
{
  // graphic commands to redraw the complete screen should be placed here
  if (x >= wifiLen)
  {
    display.drawXbm(96, 16, wifi[wifiLen - 1], wifiWidth, wifiHeight, 0);
  }
  else
  {
    display.drawXbm(96, 16, wifi[x], wifiWidth, wifiHeight, 1);
  }
  display.display();
}
// 无网络
void drawnoWifi()
{
  display.drawXbm(96, 16, wifi[wifiLen - 1], wifiWidth, wifiHeight, 0);
  display.display();
  display.drawXbm(96, 16, wifi_no, wifiWidth, wifiHeight, 1);
  display.display();
}
/*
  WIFI RSSI 信号强度对应关系
  High quality:     90% ~= -55db
  Medium quality:   50% ~= -75db
  Low quality:      30% ~= -85db
  Unusable quality: 8% ~= -96db
*/
void drawWifi_s(int x)
{
  for (int i = 0; i < 4; i++)
  {
    if (i <= x)
    {
      display.drawXbm(SSD1306_LCDWIDTH - miniWidth, 0, wifi_mini[i], miniWidth, miniHeight, 1);
    }
    else
    {
      display.drawXbm(SSD1306_LCDWIDTH - miniWidth, 0, wifi_mini[i], miniWidth, miniHeight, 0);
    }
  }
}
void drawMiniWifi(int32_t rssi)
{
  if (old_rssi >= 10)
  {
    display.drawXbm(SSD1306_LCDWIDTH - miniWidth, 0, wifi_mini_clear, miniWidth, miniHeight, 0);
    display.display();
  }
  if (rssi < 10)
  { // 查询成功
    //    Serial.println(rssi);
    if (rssi >= -55)
      drawWifi_s(3);
    else if (rssi >= -70 && rssi < -55)
      drawWifi_s(2);
    else if (rssi >= -85 && rssi < -70)
      drawWifi_s(1);
    else if (rssi >= -95 && rssi < -85)
      drawWifi_s(0);
  }
  else
  { // 查询失败 --  错误码 31 -- NO_WIFI
    display.drawXbm(SSD1306_LCDWIDTH - miniWidth, 0, wifi_mini_clear, miniWidth, miniHeight, 0);
    display.display();
    display.drawXbm(SSD1306_LCDWIDTH - miniWidth, 0, wifi_mini_no, miniWidth, miniHeight, 1);
  }
  display.display();
  old_rssi = rssi;
}

// 显示区域划分
void drawpartition()
{
  drawTop();
}
void drawpart_now()
{ //当前资讯 -- 区域划分
  display.drawLine(0, 48, 75, 48, 1);
  display.drawLine(75, 16, 75, 64, 1);
  display.drawLine(0, 63, 128, 63, 1);
  display.display();
}

void drawTop()
{
  display.drawLine(0, 15, 128, 15, 1);
  display.display();
}

int update_h = 0;
int update_v = 0;
void drawUpdate()
{ // -- 更新符号
  display.drawXbm(update_h, update_v, update_mini_clear, miniWidth, miniHeight, 0);
  display.display();
  for (int i = 0; i < 2; i++)
  {
    display.drawXbm(update_h, update_v, update_mini[i], miniWidth, miniHeight, 1);
    display.display();
    delay(300);
  }
}
void drawUpdateDown()
{ // 已更新
  display.drawXbm(update_h, update_v, update_mini_clear, miniWidth, miniHeight, 0);
  display.display();
  display.drawXbm(update_h, update_v, update_mini_ok, miniWidth, miniHeight, 1);
  display.display();
}
void drawUpdateF()
{ // 更新失败
  display.drawXbm(update_h, update_v, update_mini_clear, miniWidth, miniHeight, 0);
  display.display();
  display.drawXbm(update_h, update_v, update_mini_failure, miniWidth, miniHeight, 1);
  display.display();
}

// temperature 温度
void drawTemp(int x, int y, int t_size, const struct UserData *userData)
{
  display.setTextSize(t_size);
  display.setTextColor(1);
  display.setCursor(x, y);
  display.println(userData->temp);
  //  display.println(-10);
  int x_pos = strlen(userData->temp) * 5 * t_size + x;
  t_size = t_size > 2 ? 2 : t_size;
  display.drawBitmap(x_pos, y, tempChar[t_size - 1], tempCharWidth * t_size, tempCharHeight * t_size, 1); //drawXbm
  //  Serial.println(x_pos + 18);
  display.display();
}

U_Date *getUpDate(const char udate[])
{
  //get the last update
  U_Date *u_date = new U_Date;
  int split = 0;
  for (int i = 0; i < strlen(udate); i++)
  {
    if (split == 0 && udate[i] != 'T')
    {
      u_date->date_d += udate[i];
    }
    else if (udate[i] == 'T')
    {
      split++;
    }
    else if (split == 1 && udate[i] != '+')
    {
      u_date->date_t += udate[i];
    }
    else if (udate[i] == '+')
    {
      break;
    }
  }
  u_date->date_d.replace('-', '/');
  return u_date;
}


// 天气显示
void drawWeather_now(const struct UserData *userData)
{
  // graphic commands to redraw the complete screen should be placed here
  //  display.clearDisplay();
  //  display.setFont(NULL); // u8g_font_unifont
  drawpart_now();
  U_Date *u_date = getUpDate(userData->udate_now);
  display.setTextSize(1);
  //  display.setTextColor(0, 1);
  display.setCursor(35, 3);
  display.println(u_date->date_d);

  // temperature
  drawTemp(37, 20, 2, userData);
  display.setTextSize(1);
  display.setTextColor(1);// SET THE TEXT attribute

    /* 下面是VIP账户能获取的信息，我们是普通账号
    // humidity
  int hum_h = 16 + 1;
  display.setCursor(78, hum_h);
  display.println("H:");
  display.setCursor(90, hum_h);
  display.println(userData->hum);
  display.setCursor(120, hum_h);
  display.println("%");
  // visibility 能见度
  int visi_h = 24 + 2;
  display.setCursor(78, visi_h);
  display.println("V:");
  display.setCursor(90, visi_h);
  display.println(userData->visi);
  display.setCursor(115, visi_h);
  display.println("km");
  // wind direction
  int wind_d_h = 32 + 3;
  display.setCursor(78, wind_d_h);
  display.println("WD:");
  display.setCursor(95, wind_d_h);
  display.println(userData->wind);
  // wind scale
  int wind_s_h = 40 + 4;
  display.setCursor(78, wind_s_h);
  display.println("WS:");
  display.setCursor(95, wind_s_h);
  display.println(userData->windScale);
  //userData->pressure
  int pres_h = 48 + 5;
  display.setCursor(78, pres_h);
  display.println("P:");
  display.setCursor(90, pres_h);
  display.println(userData->pressure);
  display.setCursor(115, pres_h);
  display.println("mb");
  display.display();
  */
  display.setCursor(78, 54);
  display.println(userData->city);
  display.display();
  
  display.drawXbm(80, 16, hzw, hzwWidth, hzwHeight, 1);

  if (userData->weatherCode <= 38)
    display.drawXbm(0, 16, weather[userData->weatherCode], weatherIconWidth, weatherIconHeight, 1);
  else
  {
    display.drawXbm(0, 16, unknown, weatherIconWidth, weatherIconHeight, 1);
  }
  display.display();
}

String getDate(const char date[])
{
  String date_y;
  String date_d;
  int split = 0;
  for (int i = 0; i < strlen(date); i++)
  {
    if (split == 0 && date[i] != '-')
    {
      date_y += date[i];
    }
    else if (split == 0 && date[i] == '-')
    {
      split++;
    }
    else if (split == 1)
    {
      date_d += date[i];
    }
  }
  date_d.replace('-', '/');
  return date_d;
}
// 天气显示
void drawWeather_daily(const struct UserData *userData)
{
  // graphic commands to redraw the complete screen should be placed here
  //  display.clearDisplay();
  //  display.setFont(NULL); // u8g_font_unifont
  display.setTextSize(1);
  display.setTextColor(1);
  U_Date *u_date = getUpDate(userData->udate_daily);
  display.setCursor(35, 3);
  display.println(u_date->date_d);
  display.setCursor(43, 57);
  display.println(u_date->date_t);

  int weather1_h = 8 - 5;
  int weather2_h = 48;
  int weather3_h = 88 + 5;
  int weather_v = 16;
  int date1_h = 8 + 3 - 5;
  int date2_h = 48 + 3;
  int date3_h = 88 + 3 + 5;
  int date_v = 48;

  display.setCursor(date1_h, date_v);
  display.println(getDate(userData->date_1));
  display.setCursor(date2_h, date_v);
  display.println(getDate(userData->date_2));
  display.setCursor(date3_h, date_v);
  //  Serial.println(getDate(userData->date_1));
  //  Serial.println(getDate(userData->date_2));
  //  Serial.println(getDate(userData->date_3));
  display.println(getDate(userData->date_3));

  if (userData->date_1_code_day <= 38)
    display.drawXbm(weather1_h, weather_v, weather[userData->date_1_code_day], weatherIconWidth, weatherIconHeight, 1);
  else
  {
    display.drawXbm(weather1_h, weather_v, unknown, weatherIconWidth, weatherIconHeight, 1);
  }
  if (userData->date_2_code_day <= 38)
    display.drawXbm(weather2_h, weather_v, weather[userData->date_2_code_day], weatherIconWidth, weatherIconHeight, 1);
  else
  {
    display.drawXbm(weather2_h, weather_v, unknown, weatherIconWidth, weatherIconHeight, 1);
  }
  if (userData->date_3_code_day <= 38)
    display.drawXbm(weather3_h, weather_v, weather[userData->date_3_code_day], weatherIconWidth, weatherIconHeight, 1);
  else
  {
    display.drawXbm(date3_h, weather_v, unknown, weatherIconWidth, weatherIconHeight, 1);
  }

  display.display();
}
// 清楚天气显示区域
void clearRect(int16_t x, int16_t y, int16_t w, int16_t h)
{
  display.fillRect(x, y, w, h, 0);
  display.display();
}

// Skip HTTP headers so that we are at the beginning of the response's body
//  -- 跳过 HTTP 头，使我们在响应正文的开头
bool skipResponseHeaders()
{
  // HTTP headers end with an empty line
  char endOfHeaders[] = "\r\n\r\n";

  client.setTimeout(HTTP_TIMEOUT);
  bool ok = client.find(endOfHeaders);

  if (!ok)
  {
    Serial.println("No response or invalid response!");
  }

  return ok;
}
// 当前天气资讯
// https://api.thinkpage.cn/v3/weather/now.json?key=24qbvr1mjsnukavo&location=huaian&language=en
String weather_now()
{
  String GetUrl = "";
  GetUrl += VERSION;
  GetUrl += NowJson;
  GetUrl += "key=";
  GetUrl += APIKEY;
  GetUrl += "&location=";
  GetUrl += CITY;
  GetUrl += "&language=";
  GetUrl += LANGUAGE;
  return GetUrl;
}
// 未来3天天气预报
// https://api.thinkpage.cn/v3/weather/daily.json?key=24qbvr1mjsnukavo&location=huaian&language=en&start=0&days=3
String weather_daily()
{
  String GetUrl = "";
  GetUrl += VERSION;
  GetUrl += DailyJson;
  GetUrl += "key=";
  GetUrl += APIKEY;
  GetUrl += "&location=";
  GetUrl += CITY;
  GetUrl += "&language=";
  GetUrl += LANGUAGE;
  GetUrl += "&start=";
  GetUrl += "0"; // -1 昨天开始 0 今天开始 1 明天开始
  GetUrl += "&days=";
  GetUrl += "3"; // 预报天数（免费账户免费预报3天）
  return GetUrl;
}

// send request -- 发送请求
bool sendRequest(const char *host, String url)
{
  // Check if a client has connected
  if (!client.connect(HOST, 80))
  {
    Serial.println("connecting to server failure!");
    drawUpdateF();
    return false;
  }
  else
  {
    Serial.println("connected to server");
    // We now create a URI for the request - 心知天气
    drawUpdate();
    // This will send the request to the server
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");
    return true;
  }
}

// Read the body of the response from the HTTP server -- 从HTTP服务器响应中读取正文
void readReponseContent(char *content, size_t maxSize)
{
  //  size_t length = client.peekBytes(content, maxSize);
  Serial.println("content");
  Serial.println(content);
  size_t length = client.readBytes(content, maxSize);
  delay(20);
  Serial.println(length);
  Serial.println("Get the data from Internet!");

  content[length] = 0;
  Serial.println(content);
  Serial.println("Read Over!");
}

// 解析数据
bool parseUserData_now(char *content, struct UserData *userData)
{
  // Compute optimal size of the JSON buffer according to what we need to parse.
  //  -- 根据我们需要解析的数据来计算JSON缓冲区最佳大小
  // This is only required if you use StaticJsonBuffer. -- 如果你使用StaticJsonBuffer时才需要
  //  const size_t BUFFER_SIZE = 1024;

  // Allocate a temporary memory pool on the stack -- 在堆栈上分配一个临时内存池
  //  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  //  -- 如果堆栈的内存池太大，使用 DynamicJsonBuffer jsonBuffer 代替
  // If the memory pool is too big for the stack, use this instead:
  DynamicJsonBuffer jsonBuffer;

  JsonObject &root = jsonBuffer.parseObject(content);

  if (!root.success())
  {
    Serial.println("JSON parsing failed!");
    return false;
  }

  // Here were copy the strings we're interested in -- 复制我们感兴趣的字符串
  strcpy(userData->city, root["results"][0]["location"]["name"]);
  strcpy(userData->cnty, root["results"][0]["location"]["country"]);
  strcpy(userData->weather, root["results"][0]["now"]["text"]);
  userData->weatherCode = root["results"][0]["now"]["code"];
  strcpy(userData->temp, root["results"][0]["now"]["temperature"]);
  //下面是付费版的数据，
  // strcpy(userData->feel, root["results"][0]["now"]["feels_like"]);
  // strcpy(userData->pressure, root["results"][0]["now"]["pressure"]);
  // strcpy(userData->hum, root["results"][0]["now"]["humidity"]);
  // strcpy(userData->visi, root["results"][0]["now"]["visibility"]);
  // strcpy(userData->wind, root["results"][0]["now"]["wind_direction"]);
  // userData->windScale = root["results"][0]["now"]["wind_scale"];
  strcpy(userData->udate_now, root["results"][0]["last_update"]);
  // It's not mandatory to make a copy, you could just use the pointers
  // Since, they are pointing inside the "content" buffer, so you need to make
  // sure it's still in memory when you read the string
  //  -- 这不是强制复制，你可以使用指针，因为他们是指向“内容”缓冲区内，所以你需要确保
  //   当你读取字符串时它仍在内存中
  return true;
}

bool parseUserData_daily(char *content, struct UserData *userData)
{
  // Compute optimal size of the JSON buffer according to what we need to parse.
  //  -- 根据我们需要解析的数据来计算JSON缓冲区最佳大小
  // This is only required if you use StaticJsonBuffer. -- 如果你使用StaticJsonBuffer时才需要
  //  const size_t BUFFER_SIZE = 1024;

  // Allocate a temporary memory pool on the stack -- 在堆栈上分配一个临时内存池
  //  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  //  -- 如果堆栈的内存池太大，使用 DynamicJsonBuffer jsonBuffer 代替
  // If the memory pool is too big for the stack, use this instead:
  DynamicJsonBuffer jsonBuffer;

  JsonObject &root = jsonBuffer.parseObject(content);

  if (!root.success())
  {
    Serial.println("JSON parsing failed!");
    return false;
  }
  //  const char* x = root["results"][0]["location"]["name"];//
  //  Serial.println(x);
  // Here were copy the strings we're interested in -- 复制我们感兴趣的字符串
  strcpy(userData->city, root["results"][0]["location"]["name"]);
  strcpy(userData->cnty, root["results"][0]["location"]["country"]);
  strcpy(userData->date_1, root["results"][0]["daily"][0]["date"]);
  strcpy(userData->date_1_text_day, root["results"][0]["daily"][0]["text_day"]);
  userData->date_1_code_day = root["results"][0]["daily"][0]["code_day"];

  strcpy(userData->date_2, root["results"][0]["daily"][1]["date"]);
  strcpy(userData->date_2_text_day, root["results"][0]["daily"][1]["text_day"]);
  userData->date_2_code_day = root["results"][0]["daily"][1]["code_day"];
  strcpy(userData->date_3, root["results"][0]["daily"][2]["date"]);

  strcpy(userData->date_3_text_day, root["results"][0]["daily"][2]["text_day"]);

  userData->date_3_code_day = root["results"][0]["daily"][2]["code_day"];

  strcpy(userData->udate_daily, root["results"][0]["last_update"]);
  // It's not mandatory to make a copy, you could just use the pointers
  // Since, they are pointing inside the "content" buffer, so you need to make
  // sure it's still in memory when you read the string
  //  -- 这不是强制复制，你可以使用指针，因为他们是指向“内容”缓冲区内，所以你需要确保
  //   当你读取字符串时它仍在内存中
  return true;
}

// Print the data extracted from the JSON -- 打印从JSON中提取的数据
void printUserData_now(const struct UserData *userData)
{
  Serial.println("Print parsed data :");
  Serial.print("City : ");
  Serial.print(userData->city);
  Serial.print(", \t");
  Serial.print("Country : ");
  Serial.println(userData->cnty);
  Serial.print("Weather : ");
  Serial.print(userData->weather);
  Serial.print(",\t");
  Serial.print("Temp : ");
  Serial.print(userData->temp);
  Serial.print(" C");
  Serial.print(",\t");
  Serial.print("Feel : ");
  Serial.print(userData->feel);
  Serial.print(" C");
  Serial.print(",\t");
  Serial.print("Humidity : ");
  Serial.print(userData->hum);
  Serial.print(" %");
  Serial.print(",\t");
  Serial.print("visibility : ");
  Serial.print(userData->visi);
  Serial.println(" km");
  Serial.print("Last Updata : ");
  Serial.print(userData->udate_now);
  Serial.println("");
}

// Print the data extracted from the JSON -- 打印从JSON中提取的数据
void printUserData_daily(const struct UserData *userData)
{
  Serial.println("Print parsed data :");
  Serial.print("City : ");
  Serial.print(userData->city);
  Serial.print(", \t");
  Serial.print("Country : ");
  Serial.println(userData->cnty);
  Serial.print(userData->date_1);
  Serial.print(":");
  Serial.print(userData->date_1_text_day);
  Serial.print(", ");
  Serial.print(userData->date_1_code_day);
  Serial.println("; ");
  Serial.print(userData->date_2);
  Serial.print(":");
  Serial.print(userData->date_2_text_day);
  Serial.print(", ");
  Serial.print(userData->date_2_code_day);
  Serial.println("; ");
  Serial.print(userData->date_3);
  Serial.print(":");
  Serial.print(userData->date_3_text_day);
  Serial.print(", ");
  Serial.print(userData->date_3_code_day);
  Serial.println("; ");
  Serial.print("Last Updata : ");
  Serial.print(userData->udate_daily);
  Serial.println("");
}

// Close the connection with the HTTP server -- 关闭与HTTP服务器连接
void stopConnect()
{
  // if (client != NULL) {
  Serial.println("Disconnect");
  //   client.stop();
  // }
}

// 获取网络更新数据 -- 当前天气情况
void update_weather_now()
{
  if (isConnected)
  { // WIFI 已连接
    if (sendRequest(HOST, weather_now()) && skipResponseHeaders())
    { //  发送请求
      char response[MAX_CONTENT_SIZE];
      readReponseContent(response, sizeof(response));
      if (parseUserData_now(response, &userData_now))
      {
        drawUpdateDown();
        //     printUserData_now(&userData_now);
        Serial.println("now data parse OK!");
      }
    }
  }
}
// 获取网络更新数据 -- 天气预报
void update_weather_daily()
{
  if (isConnected)
  { // WIFI 已连接
    if (sendRequest(HOST, weather_daily()) && skipResponseHeaders())
    { //  发送请求
      char response[MAX_CONTENT_SIZE];
      readReponseContent(response, sizeof(response));
      Serial.println("come Here Here!");
      if (parseUserData_daily(response, &userData_daily))
      {
        drawUpdateDown();
        //        printUserData_daily(&userData_daily);
        Serial.println("daily data parse OK!");
      }
    }
  }
}

// 获取网络更新数据 -- 当前天气情况 + 3天天气预报
void update_weather()
{
  if (isConnected)
  { // WIFI 已连接
    switch (DISPLAY_MODE)
    {
    case 0: // 更新当前天气数据
      update_weather_now();
      break;
    case 1: // 更新预报天气数据
      update_weather_daily();
      break;
    case 2: // 更新当前天气+预报数据
      update_weather_daily();
      update_weather_now();
      break;
    }
  }
  Serial.println(millis());
  //  stopConnect();
}

String twoDigits(int digits){
  if(digits < 10) {
    String i = '0'+String(digits);
    return i;
  }
  else {
    return String(digits);
  }
}

void display_now()
{
  isDisplayNow = true;
  Serial.println("display now weather");  
  clearRect(0, 16, 128, 48);
  drawWeather_now(&userData_now);
  uint32_t beginWait = millis(); 
  while (millis() - beginWait < now_display_time) {
      //格式化当前时间
    clearRect(50,53,20,10); //防止闪烁只刷新秒
    display.setCursor(20, 53);
    display.println(twoDigits(hour()) + ':'+ twoDigits(minute()) +  ':'+ twoDigits(second()));
    display.display();
    delay(1000);
  }
 // delay(now_display_time);
}
void display_daily()
{
  isDisplayNow = false;
  Serial.println("display daily weather");
  clearRect(0, 16, 128, 48);
  drawWeather_daily(&userData_daily);
  delay(daily_display_time);
}

void update_weather_display(int8_t dis)
{
  switch (dis)
  {
  case 0: // 当前天气 显示
    display_now();
    break;
  case 1: // 3天预报 显示
    display_daily();
    break;
  case 2: // 当前天气资讯+3天预报 交替显示
    display_daily();
    display_now();
    break;
  }
}

//NTP 对时代码
/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48;     // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0)
    ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500)
  {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE)
    {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE); // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 = (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;          // Stratum, or type of clock
  packetBuffer[2] = 6;          // Polling Interval
  packetBuffer[3] = 0xEC;       // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

void setup(void)
{
  //wifi Initialize
  WiFi.mode(WIFI_STA);
  Serial.begin(115200);
  init_oled();
  drawlogo();
  delay(500);
  Serial.println();
  Serial.print("connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password); // connecting the wifi
  int wifi_s = 0;
  int timeout = millis();
  while (millis() - timeout <= 8000)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      delay(300);
      Serial.print(".");
      drawWifi(wifi_s);
      if (wifi_s >= wifiLen)
        wifi_s = 0;
      else
        wifi_s++;
      isConnected = false;
    }
    else
    {
      isConnected = true;
      break;
    }
  }

  if (!isConnected)
  {
    Serial.println();
    Serial.println("WiFi connect failure, Please check the account number or password!");
    drawnoWifi();
  }
  else
  {
    Serial.println("");
    Serial.println("WiFi connected");
    drawWifi(wifiLen - 1); //显示已连接

    Serial.println("\nStarting connection to server...");
    display.clearDisplay(); // 清屏
    drawpartition();        // 分区
  }
  //开启UDP监听
  Udp.begin(localPort);
  //NTP对时
  setSyncProvider(getNtpTime);
  //    display.dim(true);  //低功耗显示 -- 屏幕暗
  timer.setInterval(update_rate, update_weather);
  timer.setTimer(10, update_weather, 1); //  trigger only once after 10ms
}

void loop(void)
{

  if (isConnected) // wifi 信号
    drawMiniWifi(WiFi.RSSI());
  else
    drawMiniWifi(NO_WIFI);
  isConnected = WiFi.status() == WL_CONNECTED;
  timer.run();
  update_weather_display(DISPLAY_MODE);

}
