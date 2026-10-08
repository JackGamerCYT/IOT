# 🌿 Đồ án 1 — Giám sát môi trường & điều khiển thiết bị (ESP32)

ESP32 đọc nhiệt độ, độ ẩm (DHT22), ánh sáng (BH1750), chất lượng không khí (MQ-135) và chuyển động (PIR), tự bật/tắt **đèn** và **quạt** qua module relay 2 kênh, đồng thời **tự phát một trang web** để theo dõi và điều khiển từ điện thoại hoặc máy tính trong cùng mạng WiFi. Không cần server, không cần Internet.

```
 [12 V]──[LM2596 → 5 V]──> ESP32 (VIN), relay, cảm biến
                             │
  DHT22 ──GPIO14             │  WiFi (STA, tự chuyển AP nếu không vào được)
  PIR   ──GPIO12             ▼
  MQ-135──GPIO35 (ADC)    WebServer :80 ──> http://<IP-ESP32>  hoặc  http://doan1.local
  BH1750──GPIO21/22 (I2C)     │  GET  /api/state            (JSON, trang web gọi 1 s/lần)
                              │  POST /api/relay /api/auto /api/config
  Relay IN1──GPIO25 ── Đèn    │
  Relay IN2──GPIO26 ── Quạt   └─ Luật tự động chạy ngay trên ESP32 (mất WiFi vẫn chạy)
```

## 1. Cấu trúc thư mục

```
DO-AN1-CODE/
├── README.md                     File này
├── index.html                    Giao diện web (bản gốc để sửa; mở trực tiếp trên máy tính cũng được)
├── firmware/do_an1/
│   ├── do_an1.ino                Firmware ESP32 (Arduino IDE)
│   ├── index_html.h              Trang web nhúng vào firmware — PHẢI nằm cạnh do_an1.ino
│   └── secrets.example.h         (Tuỳ chọn) mẫu khai báo WiFi → copy thành secrets.h
├── tools/
│   ├── make_index_h.py           Sinh lại index_html.h sau khi sửa index.html
│   └── device_simulator.py       Giả lập ESP32 để thử web khi chưa có mạch
├── docs/
│   ├── WIRING.md                 Đấu nối, nguồn, lưu ý an toàn
│   ├── PINOUT.xlsx               Bảng chân + danh mục linh kiện
│   └── KHAC_PHUC_SU_CO.md        Lỗi thường gặp và cách sửa
├── PUSH.md                       Đưa dự án lên GitHub
└── .gitignore
```

## 2. Nạp firmware (Arduino IDE)

1. **Cài board ESP32**: *File → Preferences → Additional boards manager URLs* thêm
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, rồi *Tools → Board → Boards Manager* cài **esp32 by Espressif**.
2. **Cài 3 thư viện** (*Sketch → Include Library → Manage Libraries*):
   - `DHT sensor library` — **by Adafruit** (bấm *Install All* để cài kèm `Adafruit Unified Sensor`)
   - `BH1750` — **by Christopher Laws**
3. **Mở `firmware/do_an1/do_an1.ino`** (mở cả thư mục `do_an1`, phải thấy 2 tab `do_an1.ino` và `index_html.h`).
4. **Điền WiFi**: sửa `WIFI_SSID`, `WIFI_PASSWORD` ở đầu `do_an1.ino`, hoặc copy `secrets.example.h` thành `secrets.h` rồi điền vào đó.
5. *Tools → Board* chọn **ESP32 Dev Module**, chọn đúng cổng COM, bấm **Upload**.
6. Mở *Serial Monitor* **115200 baud**, nhấn EN. Dòng `Đã vào WiFi. Mở trình duyệt: http://192.168.x.x` cho biết địa chỉ web.

Không vào được WiFi sau 15 s → ESP32 tự phát WiFi **`DoAn1-ESP32`** (mật khẩu `12345678`), kết nối vào rồi mở **http://192.168.4.1**.

## 3. Phần cứng

| Linh kiện | Chân linh kiện | ESP32 | Ghi chú |
|---|---|---|---|
| DHT22 | DATA | **GPIO 14** | Nên cấp **3,3 V** |
| PIR HC-SR501 | OUT | **GPIO 12** | GPIO12 là chân strapping — xem `docs/WIRING.md` |
| MQ-135 | AO | **GPIO 35** (ADC1, chỉ vào) | AO tới 5 V → cần phân áp 10 kΩ/20 kΩ |
| BH1750 (GY-302) | SDA / SCL | **GPIO 21 / 22** | I2C, địa chỉ 0x23 |
| Relay 2 kênh | IN1 / IN2 | **GPIO 25 / 26** | Kích mức THẤP; IN1 = đèn, IN2 = quạt |
| LM2596 | OUT | 5 V → VIN ESP32 | Chỉnh đúng 5,0 V **trước khi** cắm ESP32 |

Chi tiết đấu nối và an toàn: [`docs/WIRING.md`](docs/WIRING.md).

## 4. Luật tự động

| Relay | Bật khi | Tắt khi | Chống đóng cắt liên tục |
|---|---|---|---|
| **1 – Đèn** | PIR có người **và** ánh sáng < `luxOn` (mặc định 50 lux) | Không có chuyển động trong `hold` giây (mặc định 30 s) | Khi đèn đã bật thì **bỏ qua cảm biến ánh sáng** (ánh sáng của chính đèn làm lux tăng) |
| **2 – Quạt** | Nhiệt độ ≥ `tOn` (32 °C) **hoặc** MQ-135 ≥ `gasOn` (2000) | Nhiệt độ ≤ `tOn − 1 °C` **và** MQ-135 ≤ `gasOn − 200` | Dải trễ + bật tối thiểu 10 s; bỏ qua MQ-135 trong 60 s đầu (cảm biến đang nóng) |

- Mỗi relay có công tắc **AUTO** riêng. Bấm BẬT/TẮT trên web → relay đó chuyển sang **MANUAL**; tick lại AUTO để giao cho mạch.
- Ngưỡng chỉnh trên web, lưu vào Flash (Preferences) nên mất điện không mất cấu hình.
- Lỗi đọc BH1750 → coi như trời tối (đèn vẫn chạy theo PIR). Lỗi đọc DHT22 → bỏ luật nhiệt độ.

## 5. API (ESP32 và bộ giả lập giống nhau)

| Phương thức | Đường dẫn | Tham số | Kết quả |
|---|---|---|---|
| GET | `/` | — | Trang web |
| GET | `/api/state` | — | JSON trạng thái (dưới đây) |
| POST | `/api/relay` | `ch=1\|2`, `on=0\|1` | Bật/tắt tay, chuyển relay đó sang MANUAL |
| POST | `/api/auto` | `ch=1\|2`, `on=0\|1` | Bật/tắt chế độ AUTO |
| POST | `/api/config` | `luxOn`, `hold`, `tOn`, `gasOn` | Đổi ngưỡng, lưu Flash |

```json
{"t":29.4,"h":68.2,"lux":35.0,"gas":1640,"gasV":1.238,"warm":0,"motion":1,"motionAgo":0,
 "r1":1,"a1":1,"why1":"Có người, trời tối (35 lux)","r2":0,"a2":1,"why2":"Nhiệt độ và không khí bình thường",
 "cfg":{"luxOn":50,"hold":30,"tOn":32.0,"gasOn":2000},"evn":7,"ev":["14:02:11  Đèn BẬT – Có người, trời tối (35 lux)"],
 "ip":"192.168.1.50","mode":"TenWiFi","rssi":-58,"clock":"14:02:12","uptime":3600}
```

Giá trị lỗi cảm biến trả về `null`. Mọi phản hồi có header `Access-Control-Allow-Origin: *`, nên `index.html` mở trực tiếp trên máy tính (nhập IP ESP32 ở góc trên) vẫn gọi được API.

## 6. Thử web khi chưa có mạch

```bash
python tools/device_simulator.py
```
Mở http://localhost:8080 — số liệu giả, luật tự động chạy giống firmware. Chỉ cần Python 3, không cài thêm gì.

## 7. Sửa giao diện

1. Sửa `index.html` ở gốc (mở bằng trình duyệt hoặc chạy bộ giả lập để xem ngay).
2. Chạy `python tools/make_index_h.py` → ghi đè `firmware/do_an1/index_html.h`.
3. Nạp lại firmware.

## 8. Gặp lỗi

Xem [`docs/KHAC_PHUC_SU_CO.md`](docs/KHAC_PHUC_SU_CO.md): thiếu `index_html.h`, không khởi động được, DHT22 báo `--`, BH1750 không tìm thấy, relay đảo ngược, MQ-135 luôn 4095, không mở được web…
