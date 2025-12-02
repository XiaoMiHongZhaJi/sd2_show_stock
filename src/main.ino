#include <lvgl.h>
#include <TFT_eSPI.h>
#include <string>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Ticker.h>

using namespace std;

// WiFi配置
const char *ssid = "MYWIFI";
const char *password = "12222222";
// 服务器配置
const char *host = "your_cloudflare_page_url or your_local_server_ip";
const int httpPort = 80;
const char *url = "/getChartInfo";
uint8 stockIndex = 0;
// 屏幕亮度
#define BRIGHT_NESS 30  // 1-100
// 刷新间隔(毫秒)
const int refresh_time = 20000;
// 按钮
#define BUTTON_PIN  4
bool lastButtonState = HIGH;
// 蜂鸣器
#define BEEP_PIN  16
// 氛围灯
#define LED_PIN     12   // GPIO12
Adafruit_NeoPixel strip(1, LED_PIN, NEO_GRB + NEO_KHZ800);

// LVGL字体声明
LV_FONT_DECLARE(lv_font_zhixin_18)
LV_FONT_DECLARE(iconfont_symbol)

// TFT和LVGL显示缓冲区
TFT_eSPI tft = TFT_eSPI();
static lv_disp_buf_t disp_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];

// LVGL页面对象
static lv_obj_t *login_page = NULL;
static lv_obj_t *monitor_page = NULL;

// LVGL UI元素
static lv_obj_t *login_page_label;
static lv_obj_t *top_label_1;
static lv_obj_t *top_icon_2;
static lv_obj_t *top_label_2;
static lv_obj_t *chart_label_top_left;
static lv_obj_t *chart_label_top_right;
static lv_obj_t *chart_label_bottom_left;
static lv_obj_t *chart_label_bottom_right;
static lv_obj_t *bottom_label_1;
static lv_obj_t *bottom_label_2;
static lv_obj_t *bottom_label_3;
static lv_obj_t *chart;
static lv_obj_t *abscissa_hline = NULL; // 用于表示横线的对象，方便管理
static lv_task_t *t; // 自动刷新任务

static lv_chart_series_t *chart_series;

// LVGL样式(提前初始化并重用以节省内存和提高效率)
static lv_style_t iconfont_style;
static lv_style_t font_style;
static lv_style_t hline_style;

// 定时器
Ticker myTimer;
volatile bool needToRunTask = false;
void timerCallback() {
    needToRunTask = true;
}

// 设置屏幕亮度
void setBrightness(int value) {
    // 增加输入值校验
    if (value < 1) value = 1;
    if (value > 100) value = 100; // 假设PWM范围是0-255

    // 确保TFT_BL引脚配置正确
    pinMode(TFT_BL, OUTPUT);
    analogWriteRange(1023);
    analogWrite(TFT_BL, 1023 - (value * 10));
}

lv_obj_t *lv_obj_default(uint8 x, uint8 y, const char *text) {
    lv_obj_t *label = lv_label_create(monitor_page, NULL);
    lv_obj_add_style(label, LV_LABEL_PART_MAIN, &font_style); // 使用预定义样式
    lv_label_set_text(label, text); // 初始值
    lv_obj_set_pos(label, x, y);
    return label;
}

// 样式初始化函数，只运行一次
void initStyles() {
    lv_init();
    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10); // 初始化显示缓冲区

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LV_HOR_RES_MAX; // 使用宏
    disp_drv.ver_res = LV_VER_RES_MAX; // 使用宏
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // Icon Font Style
    lv_style_init(&iconfont_style);
    lv_style_set_text_font(&iconfont_style, LV_STATE_DEFAULT, &iconfont_symbol);
    // 默认颜色可以在创建对象时设置，或者这里设置一个通用默认值
    lv_style_set_text_color(&iconfont_style, LV_STATE_DEFAULT, LV_COLOR_WHITE); // 示例默认白色

    // Font 22 Style
    lv_style_init(&font_style);
    lv_style_set_text_font(&font_style, LV_STATE_DEFAULT, &lv_font_zhixin_18);
    lv_style_set_text_color(&font_style, LV_STATE_DEFAULT, LV_COLOR_WHITE); // 示例默认白色

    // Horizontal Line Style for chart
    lv_style_init(&hline_style);
    lv_style_set_line_color(&hline_style, LV_STATE_DEFAULT, LV_COLOR_GRAY);
    lv_style_set_line_width(&hline_style, LV_STATE_DEFAULT, 2);
    lv_style_set_line_opa(&hline_style, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_style_set_line_dash_width(&hline_style, LV_STATE_DEFAULT, 4);
    lv_style_set_line_dash_gap(&hline_style, LV_STATE_DEFAULT, 4);
}

// 页面初始化
void setupPages() {
    // 登录页面
    if (login_page == NULL) { // 避免重复创建
        login_page = lv_cont_create(lv_scr_act(), NULL);
        lv_obj_set_size(login_page, LV_HOR_RES_MAX, LV_VER_RES_MAX); // 使用宏定义确保覆盖全屏
        lv_obj_set_style_local_bg_color(login_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_obj_set_style_local_border_color(login_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_obj_set_style_local_radius(login_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

        // 创建Spinner并应用样式
        lv_obj_t *preload = lv_spinner_create(login_page, NULL);
        lv_obj_set_size(preload, 100, 100);
        lv_obj_align(preload, NULL, LV_ALIGN_CENTER, 0, -20);

        login_page_label = lv_label_create(login_page, NULL);
        lv_obj_add_style(login_page_label, LV_LABEL_PART_MAIN, &font_style); // 使用预定义样式
    }
    // 监控页面
    if (monitor_page == NULL) { // 避免重复创建
        monitor_page = lv_cont_create(lv_scr_act(), NULL);
        lv_obj_set_size(monitor_page, LV_HOR_RES_MAX, LV_VER_RES_MAX);
        // Monitor page背景由bg对象设置，这里可以设置一个默认背景或者透明
        lv_obj_set_style_local_bg_opa(monitor_page, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
    }

    // 构建monitor_page的UI元素
    // 背景
    lv_obj_t *bg = lv_obj_create(monitor_page, NULL);
    lv_obj_clean_style_list(bg, LV_OBJ_PART_MAIN);
    lv_obj_set_style_local_bg_opa(bg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_100);
    lv_color_t bg_color = lv_color_hex(0x7381a2); // 保持原有颜色
    lv_obj_set_style_local_bg_color(bg, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, bg_color);
    lv_obj_set_size(bg, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_pos(bg, 0, 0); // 确保从(0,0)开始

    lv_obj_t *cont = lv_cont_create(monitor_page, NULL);
    lv_obj_set_auto_realign(cont, true);
    lv_color_t cont_color = lv_color_hex(0x081418);
    lv_obj_set_width(cont, 230);
    lv_obj_set_height(cont, 210);
    lv_obj_set_pos(cont, 5, 5);
    lv_cont_set_fit(cont, LV_FIT_TIGHT);
    lv_cont_set_layout(cont, LV_LAYOUT_COLUMN_MID);
    lv_obj_set_style_local_border_color(cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, cont_color);
    lv_obj_set_style_local_bg_color(cont, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, cont_color);

    // 顶部标签
    top_label_1 = lv_obj_default(15, 12, "...");
    top_label_2 = lv_obj_default(142, 12, "...");

    top_icon_2 = lv_label_create(monitor_page, NULL);
    lv_obj_add_style(top_icon_2, LV_LABEL_PART_MAIN, &iconfont_style); // 使用预定义样式
    lv_label_set_text(top_icon_2, CUSTOM_SYMBOL_UPLOAD); // 默认图标
    lv_obj_set_pos(top_icon_2, 120, 15);

    // 图中
    chart_label_top_left = lv_obj_default(15, 40, "");
    chart_label_top_right = lv_obj_default(180, 40, "");
    chart_label_bottom_left = lv_obj_default(15, 190, "");
    chart_label_bottom_right = lv_obj_default(170, 190, "");

    lv_obj_set_style_local_text_color(chart_label_top_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_text_color(chart_label_top_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_text_color(chart_label_bottom_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_obj_set_style_local_text_color(chart_label_bottom_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

    // 底部
    bottom_label_1 = lv_obj_default(10, 220, "");
    bottom_label_2 = lv_obj_default(140, 220, "");
    bottom_label_3 = lv_obj_default(190, 220, "");

    // 绘制曲线图
    chart = lv_chart_create(monitor_page, NULL);
    lv_obj_set_size(chart, 220, 175);
    lv_obj_align(chart, NULL, LV_ALIGN_CENTER, 0, 2);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);

    lv_obj_set_style_local_bg_opa(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_OPA_50);
    lv_obj_set_style_local_bg_grad_dir(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_obj_set_style_local_bg_main_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 255);
    lv_obj_set_style_local_bg_grad_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0);

    chart_series = lv_chart_add_series(chart, LV_COLOR_RED);

    lv_chart_refresh(chart);
}


// 连接WiFi
void connectWiFi() {
    lv_obj_set_hidden(login_page, false);
    lv_obj_set_hidden(monitor_page, true);

    Serial.printf("Connecting to %s ...", ssid);
    if (login_page_label) {
        lv_label_set_text_fmt(login_page_label, "Connecting to %s ...", ssid);
        lv_obj_align(login_page_label, NULL, LV_ALIGN_CENTER, 0, 70);
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    myTimer.once(10, timerCallback);
}

boolean checkWiFiStatus() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnection established!");
        Serial.print("IP address:    ");
        Serial.println(WiFi.localIP().toString());
        // WiFi连接成功后切换页面
        if (login_page && monitor_page) {
            lv_obj_set_hidden(login_page, true);
            lv_obj_set_hidden(monitor_page, false);
        }
        // 更新IP地址显示
        if (bottom_label_1) {
            lv_label_set_text(bottom_label_1, WiFi.localIP().toString().c_str());
        }
        lv_task_reset(t); // 清空倒计时
        task_cb(NULL);
        return true;
    }
    Serial.println("\nWiFi connection failed! Reconnecting...");
    lv_task_reset(t); // 清空倒计时
    connectWiFi();
    return false;
}

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    // 增加区域有效性检查
    if (!area || !color_p || area->x1 > area->x2 || area->y1 > area->y2) {
        lv_disp_flush_ready(disp);
        return;
    }

    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors(&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

// 存储解析结果的结构体
struct ChartData {
    String top_label_1_str;
    String top_label_2_str;
    String bottom_label_1_str;
    String bottom_label_2_str;
    String bottom_label_3_str;
    String chart_max_val_str;
    String chart_min_val_str;
    String chart_max_per_str;
    String chart_min_per_str;
    String chart_abscissa_str;

    float chart_abscissa = 0.0f;
    float chart_max_val = 0.0f;
    float chart_min_val = 0.0f;
    int top_icon_2_val = 0;
    int chart_point_length = 0;
    vector<lv_coord_t> chart_points; // 使用vector动态存储点
};

bool getChartInfo(ChartData &data);
// 获取图表信息
bool getChartInfo(ChartData &data) {
    WiFiClient client;

    // 使用非阻塞连接，并设置超时
    if (!client.connect(host, httpPort)) {
        Serial.println("Connection to host failed!");
        return false;
    }

    client.print("GET ");
    client.print(url);
    client.print("?index=");
    client.print(stockIndex++);
    client.print(" HTTP/1.1\r\nHost: ");
    client.print(host);
    client.print("\r\nConnection: close\r\n\r\n");

    // 等待响应头，设置读取超时
    Serial.println(client.readStringUntil('\n'));

    if (!client.find("\r\n\r\n")) {
        Serial.println("Header End not found. Invalid response.");
        client.stop();
        return false;
    }

    // 使用ArduinoJson解析
    const size_t capacity = JSON_ARRAY_SIZE(50) + JSON_OBJECT_SIZE(15) + 1000; // 预估JSON大小
    DynamicJsonDocument doc(capacity);

    // 增加读取超时
    client.setTimeout(5000); // 5秒读取超时
    DeserializationError error = deserializeJson(doc, client);

    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        client.stop();
        return false;
    }

    // JSON数据有效性检查及提取
    JsonObject chart_info = doc["chart_info"];
    JsonArray chart_array = doc["chart_array"];
    JsonObject beep_info = doc["beep_info"];

    if (chart_info.isNull()) {
        Serial.println("Error: 'chart_info' is missing or invalid in JSON response.");
        client.stop();
        return false;
    }
    // 提取并校验每个字段
    data.top_label_1_str = chart_info["top_label_1"].as<String>();
    data.top_label_2_str = chart_info["top_label_2"].as<String>();
    data.bottom_label_1_str = chart_info["bottom_label_1"].as<String>();
    data.bottom_label_2_str = chart_info["bottom_label_2"].as<String>();
    data.bottom_label_3_str = chart_info["bottom_label_3"].as<String>();
    data.chart_max_val_str = chart_info["chart_max_val"].as<String>();
    data.chart_min_val_str = chart_info["chart_min_val"].as<String>();
    data.chart_max_per_str = chart_info["chart_max_per"].as<String>();
    data.chart_min_per_str = chart_info["chart_min_per"].as<String>();
    data.chart_abscissa_str = chart_info["chart_abscissa"].as<String>();

    data.chart_abscissa = chart_info["chart_abscissa"].as<float>() * 100;
    data.chart_max_val = chart_info["chart_max_val"].as<float>() * 100;
    data.chart_min_val = chart_info["chart_min_val"].as<float>() * 100;
    data.top_icon_2_val = chart_info["top_icon_2"].as<int>();
    data.chart_point_length = chart_info["chart_point_length"].as<int>();

    int chart_array_length = chart_array.size(); // 实际数组长度

    // 校验 chart_point_length
    if (data.chart_point_length <= 0) {
        Serial.println("Warning: chart_point_length is invalid (<=0). Setting to actual array size.");
        data.chart_point_length = chart_array_length; // 如果无效，使用实际数组长度
    }

    // 动态填充图表数据，并进行数据类型校验
    data.chart_points.reserve(data.chart_point_length); // 预分配内存
    for (int i = 0; i < data.chart_point_length; i++) {
        if (i < chart_array_length) {
            data.chart_points.push_back(chart_array[i].as<float>() * 100);
        } else {
            data.chart_points.push_back(LV_CHART_POINT_DEF); // 填充默认值
        }
    }
    client.stop();

    if (!beep_info.isNull()) {

        uint8 beepCount = beep_info["beepCount"] | 1;     // 默认 1 次
        uint16 pauseTime = beep_info["pauseTime"] | 0;    // 默认 0 ms
        uint16 beepFreq = beep_info["beepFreq"] | 2500;   // 默认 2500 Hz
        uint16 beepTime = beep_info["beepTime"] | 100;    // 默认 100 ms

        playBeepPattern(beepCount, pauseTime, beepFreq, beepTime);
    }
    return true;
}

// task循环执行的函数
static void task_cb(lv_task_t *task) {
    // 每次循环都检查并确保WiFi连接
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi(); // 尝试重新连接
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Still not connected after attempt. Skipping data fetch.");
            // 可以在这里更新UI显示连接失败状态
            if (bottom_label_1) {
                lv_label_set_text(bottom_label_1, "WiFi Disconnected !");
                lv_label_set_text(bottom_label_2, "");
                lv_label_set_text(bottom_label_3, "");
            }
            return;
        }
    }
    // WiFi连接成功后切换页面
    if (login_page && monitor_page) {
        lv_obj_set_hidden(login_page, true);
        lv_obj_set_hidden(monitor_page, false);
    }

    ChartData res_data;
    if (!getChartInfo(res_data)) {
        Serial.println("Failed to get or parse chart info. Displaying old data or defaults.");
        Serial.printf("Free Heap Memory: %u bytes\n", ESP.getFreeHeap());
        if (bottom_label_1) {
            lv_label_set_text(bottom_label_1, "chart info error !");
            lv_label_set_text(bottom_label_2, "");
            lv_label_set_text(bottom_label_3, "");
        }
        return;
    }
    // 更新UI元素时，检查对象是否为NULL，防止空指针解引用
    if (top_label_1) lv_label_set_text(top_label_1, res_data.top_label_1_str.c_str());
    if (top_label_2) lv_label_set_text(top_label_2, res_data.top_label_2_str.c_str());
    if (bottom_label_1) lv_label_set_text(bottom_label_1, res_data.bottom_label_1_str.c_str());
    if (bottom_label_2) lv_label_set_text(bottom_label_2, res_data.bottom_label_2_str.c_str());
    if (bottom_label_3) lv_label_set_text(bottom_label_3, res_data.bottom_label_3_str.c_str());

    if (chart_label_top_left) lv_label_set_text(chart_label_top_left, res_data.chart_max_val_str.c_str());
    if (chart_label_bottom_left) lv_label_set_text(chart_label_bottom_left, res_data.chart_min_val_str.c_str());

    if (chart_label_top_right) lv_label_set_text(chart_label_top_right, res_data.chart_max_per_str.c_str());
    if (chart_label_bottom_right) lv_label_set_text(chart_label_bottom_right, res_data.chart_min_per_str.c_str());

    // 根据数值大小设置颜色
    if (res_data.chart_max_val < res_data.chart_abscissa) {
        lv_obj_set_style_local_text_color(chart_label_top_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
        lv_obj_set_style_local_text_color(chart_label_top_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
    } else if (res_data.chart_max_val > res_data.chart_abscissa) {
        lv_obj_set_style_local_text_color(chart_label_top_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
        lv_obj_set_style_local_text_color(chart_label_top_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
    } else {
        lv_obj_set_style_local_text_color(chart_label_top_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_obj_set_style_local_text_color(chart_label_top_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    }

    if (res_data.chart_min_val < res_data.chart_abscissa) {
        lv_obj_set_style_local_text_color(chart_label_bottom_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
        lv_obj_set_style_local_text_color(chart_label_bottom_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
    } else if (res_data.chart_min_val > res_data.chart_abscissa) {
        lv_obj_set_style_local_text_color(chart_label_bottom_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
        lv_obj_set_style_local_text_color(chart_label_bottom_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
    } else {
        lv_obj_set_style_local_text_color(chart_label_bottom_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_obj_set_style_local_text_color(chart_label_bottom_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    }

    // 处理图标
    if (res_data.top_icon_2_val == 1) {
        chart_series->color = LV_COLOR_GREEN;
        lv_obj_set_style_local_text_color(top_icon_2, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GREEN);
        lv_label_set_text(top_icon_2, CUSTOM_SYMBOL_DOWNLOAD);
        setRandomColor(1);
    } else if (res_data.top_icon_2_val == 2) {
        chart_series->color = LV_COLOR_RED;
        lv_obj_set_style_local_text_color(top_icon_2, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
        lv_label_set_text(top_icon_2, CUSTOM_SYMBOL_UPLOAD);
        setRandomColor(2);
    } else { // 默认或其他情况
        chart_series->color = LV_COLOR_BLACK; // 默认黑色
        lv_obj_set_style_local_text_color(top_icon_2, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        lv_label_set_text(top_icon_2, CUSTOM_SYMBOL_UPLOAD); // 默认一个图标
        setRandomColor(0);
    }

    // 处理图表范围和横线
    // 先删除旧的横线，避免内存泄漏和重复绘制
    if (abscissa_hline) {
        lv_obj_del(abscissa_hline);
        abscissa_hline = NULL; // 清空指针
    }

    // 根据数据设置图表范围
    if (res_data.chart_min_val > res_data.chart_abscissa) {
        lv_chart_set_range(chart, res_data.chart_abscissa, res_data.chart_max_val);
        if (chart_label_bottom_left) {
            lv_label_set_text(chart_label_bottom_left, res_data.chart_abscissa_str.c_str());
            lv_obj_set_style_local_text_color(chart_label_bottom_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        }
        if (chart_label_bottom_right) {
            lv_label_set_text(chart_label_bottom_right, "0.00 %");
            lv_obj_set_style_local_text_color(chart_label_bottom_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        }
    } else if (res_data.chart_max_val < res_data.chart_abscissa) {
        lv_chart_set_range(chart, res_data.chart_min_val, res_data.chart_abscissa);
        if (chart_label_top_left) {
            lv_label_set_text(chart_label_top_left, res_data.chart_abscissa_str.c_str());
            lv_obj_set_style_local_text_color(chart_label_top_left, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        }
        if (chart_label_top_right) {
            lv_label_set_text(chart_label_top_right, "0.00 %");
            lv_obj_set_style_local_text_color(chart_label_top_right, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
        }
    } else {
        lv_chart_set_range(chart, res_data.chart_min_val, res_data.chart_max_val);

        // 创建并设置横线
        abscissa_hline = lv_line_create(chart, NULL);
        lv_obj_add_style(abscissa_hline, LV_LINE_PART_MAIN, &hline_style); // 使用预定义的样式

        static lv_point_t hline_points[2];
        hline_points[0].x = 0;
        hline_points[1].x = lv_obj_get_width(chart);

        // 计算Y轴位置，注意防止除零
        if (res_data.chart_max_val != res_data.chart_min_val) {
            int chart_h = lv_obj_get_height(chart);
            int y_px = chart_h - (int)((res_data.chart_abscissa - res_data.chart_min_val) * chart_h / (res_data.chart_max_val - res_data.chart_min_val));
            hline_points[0].y = y_px;
            hline_points[1].y = y_px;
        } else { // 最大值和最小值相同，横线放在中间
            hline_points[0].y = lv_obj_get_height(chart) / 2;
            hline_points[1].y = lv_obj_get_height(chart) / 2;
        }
        lv_line_set_points(abscissa_hline, hline_points, 2);
    }

    // 设置折线数据
    lv_chart_set_point_count(chart, res_data.chart_point_length);
    if (chart_series) {
        // 使用vector的数据
        lv_chart_set_points(chart, chart_series, res_data.chart_points.data());
    }

    // 将标签移到前景
    if (chart_label_top_left) lv_obj_move_foreground(chart_label_top_left);
    if (chart_label_top_right) lv_obj_move_foreground(chart_label_top_right);
    if (chart_label_bottom_left) lv_obj_move_foreground(chart_label_bottom_left);
    if (chart_label_bottom_right) lv_obj_move_foreground(chart_label_bottom_right);
    lv_chart_refresh(chart); // 刷新图表

    Serial.printf("Free Heap Memory: %u bytes\n", ESP.getFreeHeap());
}

void setRandomColor(int theme) {
    // 1:绿 2:红
    uint8 r = random(0, 256);
    uint8 g = random(0, 256);
    uint8 b = random(0, 256);
    if(theme == 1){
        g = 255;
    } else if (theme == 2) {
        r = 255;
    }
    strip.setPixelColor(0, strip.Color(r, g, b));
    strip.show();
}

void beepOnce(uint16 beepFreq, uint16 beepTime) {
  tone(BEEP_PIN, beepFreq);   // 发声
  delay(beepTime);
  noTone(BEEP_PIN);           // 停止
}

// 蜂鸣器 提示音次数, 间隔(毫秒), 频率(Hz), 持续时间(毫秒)
void playBeepPattern(uint8 beepCount, uint16 pauseTime, uint16 beepFreq, uint16 beepTime) {
  for (uint8 i = 0; i < beepCount; i++) {
    beepOnce(beepFreq, beepTime);
    delay(pauseTime);
  }
}

void button_handler() {
    bool currentState = digitalRead(BUTTON_PIN);
    // 检测按键状态变化(下降沿)
    if (lastButtonState == HIGH && currentState == LOW) {
        Serial.println("Button pressed!");
        delay(100);          // 防抖
        beepOnce(2000, 100); // 声音
        lv_task_reset(t);    // 清空倒计时
        task_cb(NULL);       // 立刻执行刷新
    }
    lastButtonState = currentState;
}

void setup() {
    Serial.begin(500000); // 提高波特率
    // srand((unsigned)time(NULL)); // ESP8266的time(NULL)可能需要ntp同步，或者用randomSeed(analogRead(A0))
    // 按钮使用内部上拉
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(BEEP_PIN, OUTPUT);

    tft.begin();
    tft.setRotation(0);
    setBrightness(BRIGHT_NESS);

    initStyles(); // 在创建UI元素前初始化所有样式
    setupPages(); // 设置登录和监控页面
    connectWiFi(); // 连接WiFi

    t = lv_task_create(task_cb, refresh_time, LV_TASK_PRIO_MID, 0);

    // 氛围灯
    strip.begin();
    strip.setBrightness(255);
    strip.show();
    randomSeed(analogRead(A0));
}

void loop() {
    lv_task_handler();
    button_handler();
    if (needToRunTask) {
        needToRunTask = false;
        checkWiFiStatus();
    }
}
