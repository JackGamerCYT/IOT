# 🌿 Đồ án 1 — Giám sát môi trường & điều khiển thiết bị (ESP32 + MQTT HiveMQ)

ESP32 đọc nhiệt độ, độ ẩm (DHT22), ánh sáng (BH1750), chất lượng không khí (MQ-135) và chuyển động (PIR), tự bật/tắt **đèn** và **quạt** qua module relay 2 kênh, gửi số liệu lên **broker MQTT HiveMQ**. Trang web (`index.html`) nối vào cùng broker qua WebSocket nên **không cần biết IP của ESP32** — ESP32 và máy xem web ở hai mạng khác nhau vẫn điều khiển được, miễn là cả hai có Internet.

```
 DHT22 ─GPIO14 ┐                                   ┌──────────────────────────────┐
 PIR   ─GPIO12 ┤   Wi-Fi     MQTT 1883             │ index.html (máy tính, điện    │
 MQ135 ─GPIO35 ┼─ ESP32 ───────────────> broker.hivemq.com <──── WSS 8884 ──── thoại, Vercel, GitHub Pages)│
 BH1750─21/22  ┤   doan1_node7391/telemetry, status, event  →   realtime, đồ thị, nhật ký   │
 Relay ─25/26  ┘   doan1_node7391/command  ←  web gửi lệnh  ;  ack → web hiện "Xác nhận"   │
                                                   └──────────────────────────────┘
 Luật tự động chạy NGAY TRÊN ESP32 → mất Internet vẫn tự bật/tắt đèn, quạt.
```

## 1. Cấu trúc thư mục

```
DO-AN1-CODE/
├── README.md                    File này
├── index.html                   Dashboard web (mở trực tiếp, hoặc đưa lên Vercel / GitHub Pages)
├── firmware/do_an1/
│   ├── do_an1.ino               Firmware ESP32 (Arduino IDE) – CHỈ 1 FILE
│   └── secrets.example.h        (Tuỳ chọn) mẫu khai báo Wi-Fi → copy thành secrets.h
├── tools/
│   └── device_simulator.py      Giả lập ESP32 qua MQTT để thử web khi chưa có mạch
├── docs/
│   ├── WIRING.md                Đấu nối, nguồn, lưu ý an toàn
│   ├── PINOUT.xlsx              Bảng chân + danh mục linh kiện
│   └── KHAC_PHUC_SU_CO.md       Lỗi thường gặp và cách sửa
├── PUSH.md                      Đưa lên GitHub + Vercel
└── .gitignore
```

## 2. Nạp firmware (Arduino IDE)

1. **Board ESP32**: *File → Preferences → Additional boards manager URLs* thêm
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` → *Boards Manager* cài **esp32 by Espressif**.
2. **Thư viện** (*Sketch → Include Library → Manage Libraries*):
   - `PubSubClient` — **by Nick O'Leary**
   - `DHT sensor library` — **by Adafruit** (bấm *Install All* để cài kèm `Adafruit Unified Sensor`)
   - `BH1750` — **by Christopher Laws**
3. Mở `firmware/do_an1/do_an1.ino`, sửa **2 dòng** ở mục 1:
   ```cpp
   #define WIFI_SSID "TenWiFi"        // Wi-Fi 2.4 GHz CÓ INTERNET (Wi-Fi nhà, hotspot điện thoại…)
   #define WIFI_PASS "MatKhauWiFi"
   ```
   Nên đổi luôn `TOPIC_BASE` thành chuỗi riêng của nhóm (vd `doan1_nhom5_2026`) vì HiveMQ là broker công cộng — ai dùng trùng topic sẽ thấy và điều khiển được thiết bị của bạn.
4. *Tools → Board* chọn **ESP32 Dev Module**, chọn cổng COM, **Upload**.
5. Mở *Serial Monitor* **115200**. Thấy `[WIFI] OK IP …` rồi `[MQTT] OK broker.hivemq.com` là xong.

## 3. Mở web

- Mở thẳng file `index.html` bằng Chrome/Edge (máy cần Internet). Web tự kết nối HiveMQ, badge **Broker: kết nối**, **Thiết bị: ONLINE**, **Dữ liệu: vừa cập nhật**.
- Hoặc đưa `index.html` lên **Vercel / GitHub Pages** (xem `PUSH.md`) để mở bằng điện thoại ở bất cứ đâu.
- Đã đổi `TOPIC_BASE` trong firmware → trên web bấm **⚙**, nhập đúng topic đó, **Lưu & kết nối lại** (web nhớ cho lần sau). Có thể gắn sẵn vào link: `index.html?topic=doan1_nhom5_2026`.

## 4. Phần cứng

| Linh kiện | Chân | ESP32 | Ghi chú |
|---|---|---|---|
| DHT22 | DATA | **GPIO 14** | Nên cấp 3,3 V |
| PIR HC-SR501 | OUT | **GPIO 12** | Chân strapping — xem `docs/WIRING.md` |
| MQ-135 | AO | **GPIO 35** | AO tới 5 V → phân áp 10 kΩ/20 kΩ, `MQ_DIVIDER = 0.667` |
| BH1750 | SDA / SCL | **GPIO 21 / 22** | I2C 0x23 |
| Relay 2 kênh | IN1 / IN2 | **GPIO 25 / 26** | Kích mức THẤP; IN1 = đèn, IN2 = quạt |

## 5. Luật tự động (chạy trên ESP32)

| Relay | Bật khi | Tắt khi | Chống đóng cắt liên tục |
|---|---|---|---|
| **1 – Đèn** | PIR có người **và** ánh sáng < `luxOn` (50 lux) | Không có chuyển động trong `hold` giây (30 s) | Đèn đã bật thì **bỏ qua cảm biến ánh sáng** |
| **2 – Quạt** | Nhiệt độ ≥ `tOn` (32 °C) **hoặc** MQ-135 ≥ `gasOn` (2000) | Nhiệt độ ≤ `tOn − 1` **và** MQ-135 ≤ `gasOn − 200` | Dải trễ + chạy tối thiểu 10 s; bỏ qua MQ-135 60 s đầu |

Mỗi relay có AUTO riêng. Bấm BẬT/TẮT trên web → relay đó sang MANUAL. Ngưỡng đổi trên web, lưu Flash.

## 6. Đặc tả MQTT

Gốc topic `TOPIC_BASE` (mặc định `doan1_node7391`), broker `broker.hivemq.com` (ESP32: TCP 1883 · web: `wss://broker.hivemq.com:8884/mqtt`).

| Topic | Hướng | QoS / retain | Nội dung |
|---|---|---|---|
| `…/telemetry` | ESP32 → web | 0, 2 s/lần | số liệu + trạng thái relay + ngưỡng + 12 sự kiện gần nhất |
| `…/status` | ESP32 → web | LWT 1, retained | `online` / `offline` (broker tự gửi `offline` khi ESP32 mất kết nối) |
| `…/event` | ESP32 → web | 0 | `{"type","detail","time"}` |
| `…/command` | web → ESP32 | 1 | `{"id","device","action",…}` |
| `…/ack` | ESP32 → web | 0 | `{"id","device","ok","msg","r1","r2","a1","a2"}` |

| `device` | `action` / trường thêm | Tác dụng |
|---|---|---|
| `relay1`, `relay2` | `ON` / `OFF` | Bật/tắt tay, relay đó sang MANUAL |
| `auto1`, `auto2` | `ON` / `OFF` | Bật/tắt chế độ AUTO |
| `config` | `luxOn`, `hold`, `tOn`, `gasOn` | Đổi ngưỡng, lưu Flash |
| `ping` | — | Trả `pong` (thử kết nối) |

Ví dụ telemetry:
```json
{"dev":"doan1-A1B2C3","fw":"2.0.0","seq":120,"ts":1791530000000,"t":29.4,"h":68.2,"lux":35.0,"gas":1640,
 "gasV":1.238,"warm":0,"motion":1,"motionAgo":0,"r1":1,"a1":1,"why1":"Có người, trời tối (35 lux)",
 "r2":0,"a2":1,"why2":"Nhiệt độ và không khí bình thường","cfg":{"luxOn":50,"hold":30,"tOn":32.0,"gasOn":2000},
 "evn":7,"ev":["14:02:11  Đèn BẬT – Có người, trời tối (35 lux)"],"ip":"192.168.1.50","ssid":"WiFi-Nha",
 "rssi":-58,"clock":"14:02:12","uptime":3600}
```
Giá trị lỗi cảm biến là `null`.

## 7. Thử web khi chưa có mạch

```bash
pip install paho-mqtt
python tools/device_simulator.py
```
Rồi mở `index.html` — web nhận số liệu giả qua HiveMQ, bấm nút sẽ thấy "Xác nhận" như mạch thật.

## 8. Gặp lỗi

Xem [`docs/KHAC_PHUC_SU_CO.md`](docs/KHAC_PHUC_SU_CO.md).
