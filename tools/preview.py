"""hostinfo 화면 미리보기: 펌웨어(firmware/esp8266/src/main.cpp)의 그리기를 Adafruit GFX 기본 글꼴로 흉내 내 240x240 PNG 로."""
import math
import re
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_C = sys.argv[1]
OUT = sys.argv[2]
src = open(FONT_C, encoding="latin-1").read()
body = src[src.index("{") + 1:src.rindex("}")]
FONT = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", body)]


def c565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


BLACK, WHITE = 0x0000, 0xFFFF
C_GREEN, C_YELLOW, C_RED, C_GRAY = 0x07E8, 0xFFE0, 0xF8A2, 0x8410
C_DIM, C_SKY, C_HEAD, C_ORANGE, LIGHT = 0x2104, 0x5D9F, 0x1A6F, 0xFC60, 0xC618


class TFT:
    def __init__(s, w=240, h=240):
        s.w, s.h = w, h
        s.im = Image.new("RGB", (w, h), c565(BLACK))
        s.d = ImageDraw.Draw(s.im)
        s.size = 2

    def rect(s, x, y, w, h, c):
        if w > 0 and h > 0:
            s.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c565(c))

    def rrect(s, x, y, w, h, r, c):
        s.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], r, fill=c565(c))

    def circle(s, x, y, r, c, fill=True):
        box = [x - r, y - r, x + r, y + r]
        (s.d.ellipse(box, fill=c565(c)) if fill else s.d.ellipse(box, outline=c565(c)))

    def line(s, x0, y0, x1, y1, c):
        s.d.line([x0, y0, x1, y1], fill=c565(c))

    def tri(s, pts, c):
        s.d.polygon(pts, fill=c565(c))

    def text(s, x, y, t, c, size=None):
        size = size or s.size
        for ch in t:
            o = ord(ch)
            for i in range(5):
                col = FONT[o * 5 + i]
                for j in range(8):
                    if col >> j & 1:
                        s.rect(x + i * size, y + j * size, size, size, c)
            x += 6 * size


def level(p):
    return C_GREEN if p < 60 else C_YELLOW if p < 85 else C_RED


def pct_of(s):
    if s.startswith("RAM "):
        u, t = s[4:].rstrip("G").split("/")
        return round(100 * float(u) / float(t))
    m = re.match(r"\S+ (\d+)%", s)
    return int(m.group(1)) if m else -1


TOP, LH, PAD, CORNER, TS = 10, 26, 4, 4, 2


def fancy(t, i, s):
    y, x, w, cw = TOP + i * LH, PAD, t.w, 12
    if i == 0:
        t.rrect(CORNER - 6, y - 3, w - 2 * (CORNER - 6), 22, 6, C_HEAD)
        name, _, right = s.partition("\t")
        t.text(CORNER, y, name, WHITE)
        if right:   # (계획) 제목줄 오른쪽 요약
            t.text(w - CORNER - 12 * len(right), y, right, C_GREEN if not right.startswith("!") else C_RED)
        return
    head = s.split(" ")[0]
    if head in ("CPU", "RAM", "DISK", "GPU"):
        p = pct_of(s)
        label, rest = head, s[len(head):]
        t.text(x, y, label, C_SKY)
        m = re.search(r" \d+c \d+t$", rest) if head == "CPU" else None   # 끝의 코어·스레드는 회색
        if m:
            rest = rest[:m.start()]
            t.text(x + (len(label) + len(rest)) * cw, y, m.group(), C_GRAY)
        if head in ("CPU", "GPU") and rest.split()[-1].endswith("C") or head == "GPU":
            parts = rest.split(" ")
            load = " ".join(parts[:2])
            tail = rest[len(load):]
            t.text(x + len(label) * cw, y, load, level(p))
            t.text(x + (len(label) + len(load)) * cw, y, tail, C_GREEN)
        else:
            t.text(x + len(label) * cw, y, rest, WHITE)
        if p >= 0:
            by, bw = y + 18, w - 2 * x
            t.rect(x, by, bw, 5, C_DIM)
            t.rect(x, by, bw * min(p, 100) // 100, 5, level(p))
        return
    if head in ("VM", "DOCKER"):
        a, b = re.search(r"(\d+)/(\d+)", s).groups()
        t.text(x, y, head, C_SKY)
        t.text(x + len(head) * cw, y, s[len(head):], C_GREEN if a == b else C_YELLOW if int(a) else C_RED)
        return
    if head == "up":
        t.text(x, y, s, C_GRAY)
        return
    t.text(x, y, s, C_ORANGE if i == 1 else WHITE)


def clock_area(t, top, clk):
    free = t.h - top - 6
    if free < 20:
        return
    t.line(CORNER, top + 2, t.w - CORNER, top + 2, C_DIM)
    date, hm = clk.rsplit(" ", 1)
    big = 5 if free >= 70 else 4 if free >= 50 else 3
    small = 2 if free >= 50 else 1
    bh, sh, gap = 8 * big, 8 * small, 6
    y0 = top + 4 + (free - bh - gap - sh) // 2
    t.text((t.w - 6 * big * len(hm)) // 2, y0, hm, C_YELLOW, big)
    t.text((t.w - 6 * small * len(date)) // 2, y0 + bh + gap, date, C_SKY, small)


def host_page(fields, clk, right=""):
    t = TFT()
    if right:
        fields = [fields[0] + "\t" + right] + fields[1:]
    for i, f in enumerate(fields):
        fancy(t, i, f)
    clock_area(t, TOP + len(fields) * LH, clk)
    return t.im


def sun(t, cx, cy, r):
    for a in range(8):
        an = a * math.pi / 4
        c, s = math.cos(an), math.sin(an)
        for d in (-1, 0, 1):
            t.line(cx + c * (r + 4) + d * s, cy + s * (r + 4) - d * c, cx + c * (r + 9) + d * s, cy + s * (r + 9) - d * c, C_YELLOW)
    t.circle(cx, cy, r, C_YELLOW)


def cloud(t, x, y, c):
    t.circle(x + 16, y + 18, 12, c)
    t.circle(x + 32, y + 13, 13, c)
    t.circle(x + 45, y + 21, 10, c)
    t.rrect(x + 4, y + 18, 52, 14, 7, c)


def icon(t, x, y, k):
    if k == 0:
        sun(t, x + 32, y + 32, 14)
    elif k == 1:
        sun(t, x + 42, y + 20, 10)
        cloud(t, x + 2, y + 26, WHITE)
    elif k == 4:
        cloud(t, x + 4, y + 6, LIGHT)
        for i in range(4):
            for d in range(2):
                t.line(x + 16 + i * 11 + d, y + 44, x + 11 + i * 11 + d, y + 58, C_SKY)


def weather_page(city, clk, k, name, temp, rh, lo, hi):
    t = TFT()
    w = t.w
    t.text((w - 12 * len(city)) // 2, 8, city, C_SKY)
    date, hm = clk.rsplit(" ", 1)
    t.text((w - 36 * len(hm)) // 2, 34, hm, C_YELLOW, 6)
    t.text((w - 12 * len(date)) // 2, 90, date, WHITE)
    t.line(8, 116, w - 8, 116, C_DIM)
    icon(t, 8, 126, k)
    nx = max(2, 8 + (64 - 12 * len(name)) // 2)
    t.text(nx, 198, name, WHITE)
    tc = WHITE
    rx = 92
    t.text(rx, 128, temp, tc, 4)
    ex = rx + 24 * len(temp)
    t.circle(ex + 5, 132, 4, tc, False)
    t.circle(ex + 5, 132, 3, tc, False)
    t.text(ex + 12, 128, "C", tc, 4)
    t.text(rx, 172, "RH", C_SKY)
    t.text(rx + 36, 172, rh + "%", WHITE)
    t.text(rx, 198, f"{lo}~{hi}C", C_GRAY)
    return t.im


def clock_only(clk):
    t = TFT()
    date, hm = clk.rsplit(" ", 1)
    t.text((t.w - 42 * len(hm)) // 2, 72, hm, C_YELLOW, 7)
    t.text((t.w - 12 * len(date)) // 2, 146, date, WHITE)
    return t.im


# (계획) 목록 화면: 제목줄 + 줄마다 상태 점 · 이름 · CPU% · 아래 가는 막대(메모리)
STATE = {"ok": C_GREEN, "start": C_YELLOW, "bad": C_RED, "off": C_GRAY}


def list_page(title, right, rows, foot):
    t = TFT()
    fancy(t, 0, title + "\t" + right)
    y = TOP + 30
    for row in rows:
        if isinstance(row, str):   # compose 묶음 소제목
            t.text(8, y + 4, row, C_GRAY, 1)
            t.line(8 + 6 * len(row) + 4, y + 7, t.w - 6, y + 7, C_DIM)
            y += 14
            continue
        name, st, val, mem = row
        col = STATE[st]
        t.circle(10, y + 7, 4, col)
        room = (t.w - 6 - 12 * len(val) - 6 - 20) // 12
        t.text(20, y, name[:room], WHITE if st != "off" else C_GRAY)
        t.text(t.w - 6 - 12 * len(val), y, val, C_RED if st == "bad" else level(int(val[:-1])) if val.endswith("%") else C_GRAY)
        if mem is not None:
            bw = t.w - 26
            t.rect(20, y + 18, bw, 3, C_DIM)
            t.rect(20, y + 18, int(bw * mem), 3, C_SKY)
        y += 26
    t.text((t.w - 6 * len(foot)) // 2, t.h - 12, foot, C_GRAY, 1)
    return t.im


CLK = "2026-10-24 Sat 14:32"
screens = []
if len(sys.argv) <= 3:
    sys.exit("usage: preview.py <glcdfont.c> <out.png> <screens.txt>   (예: tools/sample-screens.txt)")

if len(sys.argv) > 3:   # 실제 값: hostinfo_serial.py --dry 출력(HOST·LIST 줄)으로 그린다
    lines = [l.strip() for l in open(sys.argv[3], encoding="utf-8") if l.strip()]
    st_map = {"g": "ok", "y": "start", "r": "bad", "o": "off"}
    lists = {}
    for l in lines:
        if l.startswith("LIST "):
            parts = l[5:].split("|")
            lists.setdefault(parts[0], []).append(parts)
    screens = []
    for l in lines:
        if l.startswith("HOST "):
            f = l[5:].split("|")
            clk = next((x[1:] for x in f if x.startswith("@")), CLK)
            f = [x for x in f if not x.startswith("@")]
            name = f[0]
            pages = lists.get(name, [])
            summ = pages[0][4] if pages else ""
            screens.append((f"{name} · 서버 정보", host_page(f, clk, summ)))
            for parts in pages:
                k = parts[1]
                title = parts[2] + (" " + k if not k.endswith("/1") else "")
                rows = []
                for r in parts[5:]:
                    if r.startswith("#"):
                        rows.append(r[1:])
                    else:
                        nm, val, mem = r[1:].split("~")
                        rows.append((nm, st_map[r[0]], val, int(mem) / 100 if mem else None))
                screens.append((f"{name} · 목록 {k}", list_page(title, parts[3], rows, "")))
    for l in lines:   # 날씨·시계 화면도 보이려면: WX <위치>|<날짜 시각>|<아이콘 0~4>|<설명>|<기온>|<습도>|<최저>|<최고>, CLOCK <날짜 시각>
        if l.startswith("WX "):
            w = l[3:].split("|")
            screens.append(("날씨·시계", weather_page(w[0], w[1], int(w[2]), w[3], w[4], w[5], int(w[6]), int(w[7]))))
        elif l.startswith("CLOCK "):
            screens.append(("서버 없을 때 · 날짜·시계", clock_only(l[6:])))

SCALE, COLS, GAP = 2, 3, 28
kfont = None
for f in ("/System/Library/Fonts/AppleSDGothicNeo.ttc", "C:/Windows/Fonts/malgun.ttf",
          "/usr/share/fonts/truetype/nanum/NanumGothic.ttf"):   # 맥·Windows·리눅스(나눔)
    try:
        kfont = ImageFont.truetype(f, 22)
        break
    except OSError:
        pass
kfont = kfont or ImageFont.load_default()
cw, ch = 240 * SCALE + 24, 240 * SCALE + 24 + 40
rows = (len(screens) + COLS - 1) // COLS
sheet = Image.new("RGB", (COLS * cw + (COLS + 1) * GAP, rows * ch + (rows + 1) * GAP), (238, 236, 230))
d = ImageDraw.Draw(sheet)
for n, (cap, im) in enumerate(screens):
    r, c = divmod(n, COLS)
    x0, y0 = GAP + c * (cw + GAP), GAP + r * (ch + GAP)
    d.rounded_rectangle([x0, y0, x0 + cw, y0 + cw], 18, fill=(40, 40, 44))
    sheet.paste(im.resize((240 * SCALE, 240 * SCALE), Image.NEAREST), (x0 + 12, y0 + 12))
    d.text((x0 + 4, y0 + cw + 8), cap, fill=(40, 40, 40), font=kfont)
sheet.save(OUT)
import os
if os.environ.get("PREVIEW_EACH"):   # 화면마다 240x240 PNG 도(글 그림용): PREVIEW_EACH=<폴더>
    os.makedirs(os.environ["PREVIEW_EACH"], exist_ok=True)
    for n, (cap, im) in enumerate(screens):
        im.save(os.path.join(os.environ["PREVIEW_EACH"], f"screen{n + 1}.png"))
print(OUT, sheet.size)
