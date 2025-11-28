
// cloudflare pages 部署
export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const params = url.searchParams;

    // 获取请求参数 ?code=xxxx&name=xxxx 或者 ?index=xxx
    const indexStr = params.get("index");
    let code = params.get("code");
    let name = params.get("name") || "";

    if (!code && !indexStr) {
      return new Response(JSON.stringify({ error: "Missing 'code' or 'index' parameter" }), {
        status: 400,
        headers: { "content-type": "application/json" },
      });
    }

    const stock_codes = [
        {"code": "603456", "name": "九洲药业"},
        {"code": "002557", "name": "洽洽食品"},
        {"code": "002415", "name": "海康威视"},
    ]

    if (!code) {
      if (Number.isNaN(indexStr)) {
        return new Response(JSON.stringify({ error: "'index' not a number" }), {
          status: 400,
          headers: { "content-type": "application/json" },
        });
      }
      const index = parseInt(indexStr) % stock_codes.length;
      const stock = stock_codes[index];
      code = stock.code;
      name = stock.name;
    }

    // 记录开始时间
    const startTime = Date.now();

    // 构造百度 API URL
    const targetUrl = new URL("https://finance.pae.baidu.com/selfselect/getstockquotation");
    targetUrl.searchParams.append("all", "1");
    targetUrl.searchParams.append("code", code);
    targetUrl.searchParams.append("isStock", "true");
    targetUrl.searchParams.append("newFormat", "1");
    targetUrl.searchParams.append("group", "quotation_minute_ab");

    const cookieString = "your cookie string";

    const headers = {
      'accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7',
      'accept-language': 'zh-CN,zh;q=0.9',
      'cache-control': 'max-age=0',
      'priority': 'u=0, i',
      'upgrade-insecure-requests': '1',
      'origin': 'https://gushitong.baidu.com',
      'referer': 'https://gushitong.baidu.com',
      'user-agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36',
      'cookie': cookieString
    };

    try {
      const resp = await fetch(targetUrl.toString(), {
        method: "GET",
        headers: headers
      });

      if (!resp.ok) {
        throw new Error(`Upstream API error: ${resp.status}`);
      }

      const data = await resp.json();

      // === 数据处理逻辑 (对应 Python) ===
      
      let chart_point_length = 0;
      let minbar = [];
      
      const priceinfo = data.Result.priceinfo || [];
      
      if (priceinfo.length === 0) {
        minbar = [];
        chart_point_length = 0;
      } else if (priceinfo.length <= 121) {
        // 上午: Python [::2] 等同于 index % 2 === 0
        minbar = priceinfo.filter((_, i) => i % 2 === 0).map(item => item.price);
        chart_point_length = 60;
      } else {
        // 下午: Python [::4] 等同于 index % 4 === 0
        minbar = priceinfo.filter((_, i) => i % 4 === 0).map(item => item.price);
        chart_point_length = 60;
      }

      const pankou = data.Result.pankouinfos?.origin_pankou || {};
      const price_datetime = priceinfo[priceinfo.length - 1]?.datetime || "";
      const stock_market_code = data.Result.basicinfos?.stock_market_code || "";

      // 日期格式处理
      const [datePart, timePart] = price_datetime.split(" ");
      pankou.updateDate = datePart?.replace(/-/g, ".") || "";
      pankou.updateTime = timePart?.replace(/:/g, ".") || "";
      pankou.stockCode = name ? (stock_market_code.substring(0, 7) + name) : stock_market_code;

      const preClose = parseFloat(pankou.preClose);
      const currentPrice = parseFloat(pankou.currentPrice);
      let high = parseFloat(pankou.high);
      let low = parseFloat(pankou.low);

      let chart_max_per = 0.00;
      let chart_min_per = 0.00;

      if (!high || high === 0) {
        high = preClose * 1.1;
        chart_max_per = 9.99;
      } else {
        chart_max_per = (high - preClose) * 100 / preClose;
      }

      if (!low || low === 0) {
        low = preClose * 0.9;
        chart_min_per = -9.99;
      } else {
        chart_min_per = (low - preClose) * 100 / preClose;
      }

      let top_icon_2 = "";
      if (currentPrice) {
        if (currentPrice < preClose) top_icon_2 = "1"; // 绿色
        else if (currentPrice > preClose) top_icon_2 = "2"; // 红色
      }

      const speedTimeVal = (Date.now() - startTime) / 1000;

      const result = {
        "chart_info": {
          "top_label_1": currentPrice.toFixed(2),
          "top_icon_2": top_icon_2,
          "top_label_2": pankou.priceLimit + " %",
          "chart_max_val": high.toFixed(2),
          "chart_min_val": low.toFixed(2),
          "chart_max_per": chart_max_per.toFixed(2) + " %",
          "chart_min_per": chart_min_per.toFixed(2) + " %",
          "chart_abscissa": preClose.toFixed(2),
          "bottom_label_1": pankou.stockCode,
          "bottom_label_2": pankou.updateDate,
          "bottom_label_3": pankou.updateTime,
          "chart_point_length": Math.max(chart_point_length, minbar.length),
          "chart_array_length": minbar.length,
        },
        "chart_array": minbar,
        "speed_time": speedTimeVal.toFixed(2)
      };

      return new Response(JSON.stringify(result), {
        headers: {
          "content-type": "application/json;charset=UTF-8",
          "Access-Control-Allow-Origin": "*" // 允许跨域调用
        }
      });

    } catch (err) {
      return new Response(JSON.stringify({ error: err.message }), {
        status: 500,
        headers: { "content-type": "application/json" }
      });
    }
  }
};