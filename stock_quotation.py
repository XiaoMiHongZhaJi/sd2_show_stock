from flask import Flask, request, jsonify
import requests

app = Flask(__name__)

stoke_codes = [
    "603456",
    "002557",
    "002415",
    "688020",
]

def fetch_data(code: str):
    url = (
        f"https://finance.pae.baidu.com/selfselect/getstockquotation"
        f"?all=1&code={code}&isStock=true&newFormat=1&group=quotation_minute_ab"
    )
    headers = {
        'accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7',
        'accept-language': 'zh-CN,zh;q=0.9',
        'cache-control': 'max-age=0',
        'priority': 'u=0, i',
        'upgrade-insecure-requests': '1',
        'user-agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36'
    }

    resp = requests.get(url, headers=headers, timeout=10)
    resp.raise_for_status()

    data = resp.json()

    chart_point_length = 0
    # 提取 price 数组
    priceinfo = data["Result"]["priceinfo"]
    print(len(priceinfo))
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
    pankou["updateDate"] = price_datetime.split(" ")[0].replace("-", ".")
    pankou["updateTime"] = price_datetime.split(" ")[1].replace(":", ".")
    pankou["stockCode"] = data["Result"]["basicinfos"]["stock_market_code"]

    preClose = float(pankou["preClose"])
    currentPrice = float(pankou["currentPrice"])
    high = float(pankou["high"])
    low = float(pankou["low"])

    chart_max_per = (high - preClose) * 100 / preClose
    chart_min_per = (low - preClose) * 100 / preClose

    top_icon_2 = ""
    if currentPrice:
        if currentPrice < preClose: # 绿色
            top_icon_2 = 1
        elif currentPrice > preClose: # 红色
            top_icon_2 = 2

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
            "chart_point_length": chart_point_length,   #表格横轴点数
            "chart_array_length": len(minbar),          #数组长度（用不到）
        },
        "chart_array": minbar
    }

index = 0
@app.route("/getChartInfo", methods=["GET"])
def get_minbar():
    code = request.args.get("code")
    if not code:
        global index
        code = stoke_codes[index]
        index += 1
        if index >= len(stoke_codes):
            index = 0
    try:
        result = fetch_data(code)
        return jsonify(result)
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
