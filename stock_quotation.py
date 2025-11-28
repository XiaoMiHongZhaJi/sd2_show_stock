from flask import Flask, request, jsonify
import requests
import time
import base64
from functools import wraps

# 请求参数 ?code=xxxx&name=xxxx 或者 ?index=xxx
app = Flask(__name__)

stock_codes = [
    {"code": "603456", "name": "九洲药业"},
    {"code": "002557", "name": "洽洽食品"},
    {"code": "002415", "name": "海康威视"},
]

auth_username = "username"
auth_password = "password"

def fetch_data(code, name=""):
    url = "https://finance.pae.baidu.com/selfselect/getstockquotation"
    params = {
        "all": "1",
        "code": code,
        "isStock": "true",
        "newFormat": "1",
        "group": "quotation_minute_ab"
    }
    
    cookieString = "your cookie string";
    
    headers = {
        'accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7',
        'accept-language': 'zh-CN,zh;q=0.9',
        'cache-control': 'max-age=0',
        'priority': 'u=0, i',
        'upgrade-insecure-requests': '1',
        'user-agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36',
        'cookie': cookieString
    }
    speed_time = time.time()

    resp = requests.get(url, headers=headers, params=params)
    resp.raise_for_status()

    data = resp.json()

    chart_point_length = 0
    # 提取 price 数组
    priceinfo = data["Result"]["priceinfo"]
    #print(len(priceinfo))
    if not priceinfo or len(priceinfo) == 0:
        minbar = []
        chart_point_length = 0
    elif len(priceinfo) <= 121: #上午
        minbar = [item["price"] for item in priceinfo[::2]]
        chart_point_length = 60
    else: #下午
        minbar = [item["price"] for item in priceinfo[::4]]
        chart_point_length = 60

    # 提取 pankouinfos.origin_pankou
    pankou = data["Result"].get("pankouinfos", {}).get("origin_pankou", {})

    price_datetime = data["Result"]["priceinfo"][-1]["datetime"]
    stock_market_code = data["Result"]["basicinfos"]["stock_market_code"]
    pankou["updateDate"] = price_datetime.split(" ")[0]
    pankou["updateTime"] = price_datetime.split(" ")[1]
    pankou["stockCode"] = stock_market_code if not name else (stock_market_code[0:6] + " " + name)

    preClose = float(pankou["preClose"])
    currentPrice = float(pankou["currentPrice"])
    high = float(pankou["high"])
    low = float(pankou["low"])

    if not high or high == 0:
        high = preClose * 1.1
        chart_max_per = 9.99
    else:
        chart_max_per = (high - preClose) * 100 / preClose

    if not low or low == 0:
        low = preClose * 0.9
        chart_min_per = -9.99
    else:
        chart_min_per = (low - preClose) * 100 / preClose

    top_icon_2 = ""
    if currentPrice:
        if currentPrice < preClose: # 绿色
            top_icon_2 = 1
        elif currentPrice > preClose: # 红色
            top_icon_2 = 2

    speed_time = time.time() - speed_time
    return {
        "chart_info": {
            "top_label_1": f"{currentPrice:.2f}",       #左上
            "top_icon_2": top_icon_2,                   #中上图标
            "top_label_2": pankou["priceLimit"] + " %", #右上
            "chart_max_val": f"{high:.2f}",             #表格左上
            "chart_min_val": f"{low:.2f}",              #表格左下
            "chart_max_per": f"{chart_max_per:.2f}" + " %", #表格右上
            "chart_min_per": f"{chart_min_per:.2f}" + " %", #表格右下
            "chart_abscissa": f"{preClose:.2f}",        #表格横轴
            "bottom_label_1": pankou["stockCode"],      #左下
            "bottom_label_2": pankou["updateDate"],     #中下
            "bottom_label_3": pankou["updateTime"],     #右下
            "chart_point_length": max(chart_point_length, len(minbar)),   #表格横轴点数
            "chart_array_length": len(minbar),          #数组长度（用不到）
        },
        "chart_array": minbar,
        "speed_time": f"{speed_time:.2f}"
    }
    
    
# HTTP 认证装饰器
def requires_auth(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        auth = request.headers.get("Authorization")
        if not auth or not auth.startswith("Basic "):
            return (
                jsonify({"message": "Authentication required"}),
                401,
                {"WWW-Authenticate": "Basic realm='Login Required'"},
            )

        try:
            encoded_credentials = auth.split(" ")[1]
            decoded_credentials = base64.b64decode(encoded_credentials).decode("utf-8")
            username, password = decoded_credentials.split(":", 1)
        except Exception as e:
            print(f"Error decoding credentials: {e}")
            return (
                jsonify({"message": "Invalid authentication credentials"}),
                401,
                {"WWW-Authenticate": "Basic realm='Login Required'"},
            )

        # 验证用户名和密码
        if username == auth_username and password == auth_password:
            print("Authentication successful!")
            return f(*args, **kwargs)
        else:
            print(f"Authentication failed for user: {username}")
            return (
                jsonify({"message": "Invalid credentials"}),
                401,
                {"WWW-Authenticate": "Basic realm='Login Required'"},
            )

    return decorated


@app.route("/favicon.ico")
def favicon():
    return f"Request Path: {request.path}"


index = 0
@app.route("/getChartInfo", methods=["GET"])
# @requires_auth  # 应用 HTTP 认证装饰器
def get_minbar():
    code = request.args.get("code")
    name = request.args.get("name")
    indexStr = request.args.get("index")
    if not code:
        global index
        if indexStr and indexStr.isdigit():
            index = int(indexStr) % len(stock_codes)
        else:
            index += 1
            if index >= len(stock_codes):
                index = 0
        code = stock_codes[index]["code"]
        name = stock_codes[index]["name"]
    try:
        result = fetch_data(code, name)
        return jsonify(result)
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=False)
