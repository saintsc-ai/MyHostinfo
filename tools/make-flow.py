"""동작 방식 그림: 서버 묶음 ↔ 화면 묶음 사이 UDP 흐름(검은 바탕).
  python tools/make-flow.py docs/images/flow-groups.png
"""
import sys
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
W, H = 1800, 1040
BG, INK, SUB, LINE, CARD, PANEL = (10, 11, 14), (236, 239, 244), (150, 158, 172), (58, 63, 74), (30, 33, 41), (18, 20, 25)
BLUE, GREEN, ORANGE, SKY = (96, 165, 250), (74, 222, 128), (251, 146, 60), (56, 189, 248)


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
F_T, F_P, F_H, F_B, F_S = font(46, True), font(30, True), font(28, True), font(27), font(22)


def panel(x, y, w, h, title, accent):
    """여러 상자를 감싸는 묶음. 화살표는 이 묶음의 가장자리에 붙는다."""
    d.rounded_rectangle([x, y, x + w, y + h], 28, fill=PANEL, outline=accent, width=3)
    d.text((x + 28, y + 18), title, font=F_P, fill=accent)


def card(x, y, w, h, title, lines):
    d.rounded_rectangle([x, y, x + w, y + h], 18, fill=CARD, outline=LINE, width=2)
    d.text((x + 24, y + 14), title, font=F_H, fill=INK)
    for i, t in enumerate(lines):
        d.text((x + 24, y + 58 + i * 32), t, font=F_S, fill=SUB)


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
    d.text((cx - tw / 2, y - 56), label, font=F_B, fill=color)
    sw = d.textlength(sub, font=F_S)
    d.text((cx - sw / 2, y + 16), sub, font=F_S, fill=SUB)


title = "동작 방식"
d.text((70, 44), title, font=F_T, fill=INK)
d.text((70 + d.textlength(title, font=F_T) + 24, 60), "같은 LAN 안에서 UDP 로 주고받습니다", font=F_B, fill=SUB)

# 왼쪽 묶음: 서버들(어느 것이든 같은 프로그램)
LX, LY, LW, LH = 60, 140, 520, 600
panel(LX, LY, LW, LH, "서버 — 상태를 보낼 컴퓨터마다", BLUE)
card(LX + 26, LY + 80, LW - 52, 180, "리눅스", ["CPU·RAM·디스크·GPU", "Docker 컨테이너 · Proxmox VM"])
card(LX + 26, LY + 284, LW - 52, 140, "Windows · 맥", ["CPU·RAM·디스크"])
d.text((LX + 30, LY + 448), "모두 같은 프로그램:", font=F_S, fill=SUB)
d.text((LX + 30, LY + 482), "hostinfo_serial.py --broadcast", font=F_B, fill=INK)
d.text((LX + 30, LY + 528), "방화벽: UDP 4211 받기 열기", font=F_S, fill=SUB)

# 오른쪽 묶음: 화면들(어느 것이든 같은 방식)
RX, RY, RW, RH = 1220, 140, 520, 600
panel(RX, RY, RW, RH, "화면", GREEN)
card(RX + 26, RY + 80, RW - 52, 140, "SmallTV", ["ESP8266 · 휴대폰으로 설정"])
card(RX + 26, RY + 240, RW - 52, 140, "ESP32-S3 + ST7789", ["USB 시리얼로 설정"])
card(RX + 26, RY + 400, RW - 52, 140, "NodeMCU + ST7789", ["USB 시리얼로 설정 · USB 화면도"])
d.text((RX + 30, RY + 556), "서버 정보 → 목록 → 날씨·시계", font=F_S, fill=SUB)

# 묶음 ↔ 묶음 화살표 세 개
A0, A1 = LX + LW + 24, RX - 24
arrow(A0, 300, A1, BLUE, "① 상태  UDP 4210 · 3초마다", "모든 서버 → LAN 브로드캐스트(x.x.x.255)")
arrow(A1, 460, A0, ORANGE, "② \"여기 있음\"  UDP 4211 · 5초마다", "모든 화면 → 설정한 서버 주소 + 브로드캐스트", dashed=True)
arrow(A0, 620, A1, GREEN, "③ 상태  UDP 4210 · 그 화면 주소로 직접", "②를 들은 서버 → 그 화면")

# 아래: 인터넷(화면 묶음에서)
IX, IY, IW, IH = RX, 820, RW, 170
d.rounded_rectangle([IX, IY, IX + IW, IY + IH], 22, fill=CARD, outline=SKY, width=3)
d.text((IX + 28, IY + 20), "인터넷", font=F_H, fill=INK)
d.text((IX + 28, IY + 70), "Open-Meteo 날씨(10분마다, 키 없음)", font=F_S, fill=SUB)
d.text((IX + 28, IY + 106), "NTP 시계(KST)", font=F_S, fill=SUB)
AX = RX + RW // 2
d.line([AX, RY + RH, AX, IY - 8], fill=SKY, width=5)
d.polygon([(AX, IY - 2), (AX - 14, IY - 28), (AX + 14, IY - 28)], fill=SKY)

# 왼쪽 아래 설명
notes = [
    ("• 일반 공유기: ① 만으로 화면이 서버를 받습니다.", INK),
    ("• 기업용 무선 컨트롤러·AP 격리처럼 유선 브로드캐스트가", INK),
    ("  Wi-Fi로 넘어가지 않는 곳: 화면 설정에 서버 주소를 적으면", SUB),
    ("  ② → ③ 으로 직접 주고받습니다.", SUB),
    ("• USB로 꽂은 NodeMCU는 UDP 없이 그 컴퓨터 상태를 받습니다.", INK),
]
for i, (t, c) in enumerate(notes):
    d.text((70, 800 + i * 40), t, font=F_S, fill=c)

im.save(OUT, optimize=True)
print(OUT, im.size)
