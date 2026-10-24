"""동작 방식 그림: 서버 ↔ 화면 사이 UDP 흐름.
  python tools/make-flow.py docs/images/flow.png
"""
import sys
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
W, H = 1800, 980
BG, INK, SUB, LINE = (250, 250, 247), (34, 38, 46), (110, 116, 128), (214, 216, 220)
BLUE, GREEN, ORANGE, SKY = (37, 99, 235), (22, 163, 74), (234, 88, 12), (14, 116, 144)


def font(size, bold=False):
    for f in (("C:/Windows/Fonts/malgunbd.ttf" if bold else "C:/Windows/Fonts/malgun.ttf"),
              "/System/Library/Fonts/AppleSDGothicNeo.ttc",
              "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf" if bold else "/usr/share/fonts/truetype/nanum/NanumGothic.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


im = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(im)
F_T, F_H, F_B, F_S = font(46, True), font(32, True), font(27), font(23)


def box(x, y, w, h, title, lines, accent):
    d.rounded_rectangle([x, y, x + w, y + h], 22, fill=(255, 255, 255), outline=LINE, width=3)
    d.rounded_rectangle([x, y, x + 12, y + h], 6, fill=accent)
    d.text((x + 36, y + 22), title, font=F_H, fill=INK)
    for i, t in enumerate(lines):
        d.text((x + 36, y + 76 + i * 38), t, font=F_S, fill=SUB)


def arrow(x0, y, x1, color, label, sub, dashed=False):
    step = 1 if x1 > x0 else -1
    if dashed:
        for xs in range(x0, x1, 28 * step):
            xe = xs + 16 * step
            if (xe - x1) * step > 0:
                xe = x1
            d.line([xs, y, xe, y], fill=color, width=6)
    else:
        d.line([x0, y, x1 - 26 * step, y], fill=color, width=6)
    d.polygon([(x1, y), (x1 - 30 * step, y - 16), (x1 - 30 * step, y + 16)], fill=color)
    cx = (x0 + x1) // 2
    tw = d.textlength(label, font=F_B)
    d.rounded_rectangle([cx - tw / 2 - 18, y - 62, cx + tw / 2 + 18, y - 16], 12, fill=BG)
    d.text((cx - tw / 2, y - 58), label, font=F_B, fill=color)
    sw = d.textlength(sub, font=F_S)
    d.text((cx - sw / 2, y + 16), sub, font=F_S, fill=SUB)


title = "동작 방식"
d.text((70, 48), title, font=F_T, fill=INK)
d.text((70 + d.textlength(title, font=F_T) + 24, 64), "같은 LAN 안에서 UDP 로 주고받습니다", font=F_B, fill=SUB)

# 왼쪽: 서버들
SX, SW = 70, 470
box(SX, 170, SW, 190, "리눅스 서버", ["hostinfo_serial.py --broadcast", "CPU·RAM·디스크·GPU", "Docker 컨테이너 · Proxmox VM"], BLUE)
box(SX, 390, SW, 150, "Windows · 맥", ["hostinfo_serial.py --broadcast", "CPU·RAM·디스크"], BLUE)
d.text((SX + 30, 560), "상태를 보낼 컴퓨터마다 설치", font=F_S, fill=SUB)

# 오른쪽: 화면
RX, RW = 1260, 470
box(RX, 170, RW, 150, "SmallTV", ["ESP8266 · 1.54\" 240×240", "휴대폰으로 설정"], GREEN)
box(RX, 350, RW, 150, "ESP32-S3 + ST7789", ["점퍼선 8가닥", "USB 시리얼로 설정"], GREEN)
d.text((RX + 30, 520), "서버 정보 → 목록 → 날씨·시계", font=F_S, fill=SUB)

# 화살표 세 개
L, Rr = SX + SW + 30, RX - 30
arrow(L, 250, Rr, BLUE, "① 상태  UDP 4210 · 3초마다", "LAN 브로드캐스트(x.x.x.255)")
arrow(Rr, 450, L, ORANGE, "② \"여기 있음\"  UDP 4211 · 5초마다", "화면 → 설정한 서버 주소 + 브로드캐스트", dashed=True)
arrow(L, 650, Rr, GREEN, "③ 상태  UDP 4210 · 그 화면 주소로 직접", "②를 들은 서버가 그 화면에 직접도 보냄")

# 아래: 인터넷(날씨·시계)
IX, IY, IW, IH = 1260, 730, 470, 170
d.rounded_rectangle([IX, IY, IX + IW, IY + IH], 22, fill=(255, 255, 255), outline=LINE, width=3)
d.rounded_rectangle([IX, IY, IX + 12, IY + IH], 6, fill=SKY)
d.text((IX + 36, IY + 20), "인터넷", font=F_H, fill=INK)
d.text((IX + 36, IY + 74), "Open-Meteo 날씨(10분마다, 키 없음)", font=F_S, fill=SUB)
d.text((IX + 36, IY + 112), "NTP 시계(KST)", font=F_S, fill=SUB)
AX = RX + RW - 70   # 캡션을 지나지 않게 오른쪽으로
d.line([AX, 500, AX, IY - 8], fill=SKY, width=5)
d.polygon([(AX, IY - 2), (AX - 14, IY - 28), (AX + 14, IY - 28)], fill=SKY)

# 왼쪽 아래 설명
notes = [
    "• 일반 공유기: ① 만으로 화면이 서버를 받습니다.",
    "• 기업용 무선 컨트롤러·AP 격리처럼 유선 브로드캐스트가",
    "  Wi-Fi로 넘어가지 않는 곳: 화면 설정에 서버 주소를 적으면",
    "  ② → ③ 으로 직접 주고받습니다.",
    "• 서버 방화벽은 UDP 4211 받기를 열어 둡니다.",
]
for i, t in enumerate(notes):
    d.text((70, 740 + i * 40), t, font=F_S, fill=INK if t.startswith("•") else SUB)

im.save(OUT, optimize=True)
print(OUT, im.size)
