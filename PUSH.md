# Đưa dự án lên GitHub

Thay `<TEN-TAI-KHOAN>/<TEN-REPO>` bằng repo của nhóm.

## Cách 1 — dòng lệnh (PowerShell)

```powershell
cd $HOME\Downloads\DO-AN1-CODE        # thư mục đã giải nén
git init
git add -A
git status                            # KHÔNG được thấy secrets.h
git commit -m "Do an 1: ESP32 giam sat moi truong + web"
git branch -M main
git remote add origin https://github.com/<TEN-TAI-KHOAN>/<TEN-REPO>.git
git push -u origin main
```

Lần sau chỉ cần:
```powershell
git add -A
git commit -m "mo ta thay doi"
git push
```

## Cách 2 — không dùng dòng lệnh

1. Vào repo trên github.com → **Add file → Upload files**.
2. Kéo **cả thư mục** `firmware`, `tools`, `docs` và các file `index.html`, `README.md`, `.gitignore` vào.
3. Kiểm tra không có `secrets.h` (chứa mật khẩu WiFi), rồi bấm **Commit changes**.
