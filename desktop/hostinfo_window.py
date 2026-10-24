#!/usr/bin/env python3
"""hostinfo 화면을 PC 창으로 — SmallTV·ESP32-S3 화면(1.54" 240x240)과 같은 동작·같은 그림(파이썬 표준 라이브러리만).

  python hostinfo_window.py            # 창 띄우기(Windows 는 pythonw 로 콘솔 없이)
  python hostinfo_window.py --scale 2  # 2배(480x480). 기본 1 = 화면 보드와 같은 240x240

받는 것: 같은 LAN 서버들의 server/hostinfo_serial.py --broadcast 가 UDP 4210 으로 보내는 HOST·LIST·GEO 줄.
알리는 것: 5초마다 'DISPLAY <id>'를 UDP 4211 로(설정한 서버 주소 + 브로드캐스트 + 이 PC). 들은 서버는 이 창으로 직접도 보낸다.
화면 순서: 서버마다 서버 정보 8초 → 목록 쪽마다 6초 → 다음 서버, 한 바퀴 돌면 날씨·시계 5초.
          받는 서버가 없거나 20초 넘게 끊기면 날씨·시계만.
그리기: 펌웨어(firmware/esp8266/src/main.cpp)와 같은 좌표·색·글꼴(Adafruit GFX 6x8)로 240x240 픽셀을 그리고,
        크기는 정수 배율로만 키운다(픽셀 그대로).
마우스: 왼쪽 클릭 = 다음 화면, 오른쪽 클릭 = 메뉴(항상 위·크기·테두리·설정·종료), 테두리 없을 때 끌어서 옮기기.
설정 파일: Windows %APPDATA%\\hostinfo-window.json, 그 밖 ~/.config/hostinfo-window.json

글꼴 데이터(GLCD)는 Adafruit GFX Library 의 glcdfont.c(Copyright (c) 2012 Adafruit Industries, BSD License)다.
"""
import json
import math
import os
import socket
import sys
import threading
import time
import urllib.request

if sys.version_info < (3, 7):
    sys.exit("hostinfo_window.py 는 Python 3.7 이상이 필요합니다")

import tkinter as tk
from tkinter import simpledialog

UDP_PORT, ANN_PORT = 4210, 4211
HOST_MS, LIST_MS, WX_MS = 8.0, 6.0, 5.0
STALE, LIST_TTL = 20.0, 60.0
NET_MAX, MAX_PAGES = 8, 4
W = H = 240
LINE_H, TOP_Y, PAD_X, CORNER_X, TEXT_SIZE = 26, 6, 4, 4, 2   # 1.54" 화면 값(펌웨어와 같다)


def c565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return "#%02x%02x%02x" % (r * 255 // 31, g * 255 // 63, b * 255 // 31)


BLACK, WHITE = c565(0x0000), c565(0xFFFF)
GREEN, YELLOW, RED, GRAY = c565(0x07E8), c565(0xFFE0), c565(0xF8A2), c565(0x8410)
DIM, SKY, HEAD, ORANGE, LIGHT = c565(0x2104), c565(0x5D9F), c565(0x1A6F), c565(0xFC60), c565(0xC618)
WX_NAME = ["Clear", "Partly", "Cloudy", "Fog", "Rain", "Snow", "Storm"]

GLCD = bytes.fromhex(
    "00000000003e5b4f5b3e3e6b4f6b3e1c3e7c3e1c183c7e3c181c577d571c1c5e7f5e1c00183c1800ffe7c3e7ff001824"
    "1800ffe7dbe7ff30483a060e2629792926407f050507407f05253f5a3ce73c5a7f3e1c1c08081c1c3e7f14227f22145f"
    "5f005f5f06097f017f006689956a606060606094a2ffa29408047e040810207e201008082a1c08081c2a08081e101010"
    "100c1e0c1e0c30383e3830060e3e0e06000000000000005f00000007000700147f147f14242a7f2a1223130864623649"
    "5620500008070300001c2241000041221c002a1c7f1c2a08083e08080080703000080808080800006060002010080402"
    "3e5149453e00427f400072494949462141494d331814127f1027454545393c4a49493141211109073649494936464949"
    "291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e"
    "414141227f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040"
    "407f021c027f7f0408107f3e4141413e7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f20"
    "40201f3f4038403f631408146303047804036159494d43007f4141410204081020004141417f04020102044040404040"
    "000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c787f0804047800447d"
    "40002040403d007f1028440000417f40007c047804787c080404783844444438fc1824241818242418fc7c0804040848"
    "5454542404043f44243c4040207c1c2040201c3c4030403c44281028444c9090907c4464544c44000836410000007700"
    "00004136080002010204023c2623263c1ea1a161123a4040207a38545455592155557941225454784221555478402054"
    "5579400c1e5272123955555559395454545939555454580000457c410002457d420001457c407d1211127df0282528f0"
    "7c545545002054547c547c0a097f4932494949323a4444443a324a4848303a4141217a3a42402078009da0a07d3d4242"
    "423d3d4040403d3c24ff2424487e4943662b2ffc2f2bff0929f620c0887e090320545479410000447d413048484a3238"
    "4040227a007a0a0a727d0d19317d2629292f28262929292630484d4020380808080808080808382f10c8acba2f102834"
    "fa00007b000008142a142222142a14085500550055aa55aa55aaff55ff55ff000000ff00101010ff00141414ff001010"
    "ff00ff1010f010f0141414fc001414f700ff0000ff00ff1414f404fc141417101f10101f101f1414141f00101010f000"
    "0000001f101010101f10101010f010000000ff101010101010101010ff10000000ff140000ff00ff00001f10170000fc"
    "04f414141710171414f404f40000ff00f714141414141414f700f7141414171410101f101f141414f4141010f010f000"
    "001f101f0000001f14000000fc140000f010f01010ff10ff141414ff141010101f00000000f010fffffffffff0f0f0f0"
    "f0ffffff0000000000ffff0f0f0f0f0f3844443844fc4a4a4a347e02020606027e027e0263554941633844443c04407e"
    "201e2006027e020299a5e7a5991c2a492a1c4c7201724c304a4d4d303048784830bc625a463d3e494949007e0101017e"
    "2a2a2a2a2a44445f444440514a444040444a51400000ff0103e080ff000008086b6b083612362436060f090f06000018"
    "180000001010003040ff0101001f01011e00191d1712003c3c3c3c0000000000"
)

CFG_PATH = os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~/.config"), "hostinfo-window.json")
DEFAULT_CFG = {"servers": "", "geo": "", "show": "all", "scale": 1, "topmost": False, "frameless": False, "x": None, "y": None}


def load_cfg():
    cfg = dict(DEFAULT_CFG)
    try:
        with open(CFG_PATH, encoding="utf-8") as f:
            cfg.update(json.load(f))
    except (OSError, ValueError):
        pass
    return cfg


def save_cfg(cfg):
    try:
        os.makedirs(os.path.dirname(CFG_PATH), exist_ok=True)
        with open(CFG_PATH, "w", encoding="utf-8") as f:
            json.dump({k: v for k, v in cfg.items() if not k.startswith("_")}, f, ensure_ascii=False, indent=1)
    except OSError:
        pass


def level(pct):
    return GREEN if pct < 60 else YELLOW if pct < 85 else RED


def num(s, default=0.0):
    """앞쪽 숫자만(펌웨어 toInt·toFloat 처럼): '42C' → 42, ' 19.8' → 19.8."""
    s = s.strip()
    i = 0
    while i < len(s) and (s[i].isdigit() or s[i] in ".-"):
        i += 1
    try:
        return float(s[:i])
    except ValueError:
        return default


def pct_of(s):
    """'CPU 7% 42C' → 7, 'DISK 23% /' → 23, 'RAM 19.8/126G' → 16, 없으면 -1."""
    if s.startswith("RAM "):
        sl = s.find("/")
        u, t = num(s[4:sl]), num(s[sl + 1:])
        return int(100 * u / t + 0.5) if t > 0 else -1
    sp, pc = s.find(" "), s.find("%")
    return int(num(s[sp + 1:pc])) if 0 < sp < pc else -1


def wx_kind(c):
    if c <= 0:
        return 0
    if c <= 2:
        return 1
    if c == 3:
        return 2
    if c in (45, 48):
        return 3
    if 71 <= c <= 77 or c in (85, 86):
        return 5
    if c >= 95:
        return 6
    if c >= 51:
        return 4
    return 2


class FB:
    """240x240 픽셀 화면(Adafruit GFX 와 같은 그리기 함수)."""

    def __init__(self):
        self.p = [BLACK] * (W * H)

    def clear(self):
        self.p = [BLACK] * (W * H)

    def rect(self, x, y, w, h, c):
        x, y, w, h = int(x), int(y), int(w), int(h)
        x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
        if x1 <= x0:
            return
        row = [c] * (x1 - x0)
        for yy in range(y0, y1):
            self.p[yy * W + x0:yy * W + x1] = row

    def px(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.p[int(y) * W + int(x)] = c

    def hline(self, x, y, w, c):
        self.rect(x, y, w, 1, c)

    def line(self, x0, y0, x1, y1, c):
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        e = dx + dy
        while True:
            self.px(x0, y0, c)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * e
            if e2 >= dy:
                e += dy
                x0 += sx
            if e2 <= dx:
                e += dx
                y0 += sy

    def circle(self, cx, cy, r, c):   # fillCircle
        cx, cy, r = int(cx), int(cy), int(r)
        for dy in range(-r, r + 1):
            dx = int(math.sqrt(r * r - dy * dy) + 0.5)
            self.rect(cx - dx, cy + dy, 2 * dx + 1, 1, c)

    def circle_line(self, cx, cy, r, c):   # drawCircle
        for a in range(0, 360, 4):
            self.px(round(cx + r * math.cos(math.radians(a))), round(cy + r * math.sin(math.radians(a))), c)

    def rrect(self, x, y, w, h, r, c):   # fillRoundRect
        self.rect(x + r, y, w - 2 * r, h, c)
        self.rect(x, y + r, w, h - 2 * r, c)
        for cx, cy in ((x + r, y + r), (x + w - r - 1, y + r), (x + r, y + h - r - 1), (x + w - r - 1, y + h - r - 1)):
            self.circle(cx, cy, r, c)

    def tri(self, pts, c):   # fillTriangle
        ys = [p[1] for p in pts]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            xs = []
            for (x0, y0), (x1, y1) in ((pts[0], pts[1]), (pts[1], pts[2]), (pts[2], pts[0])):
                if (y0 <= y <= y1 or y1 <= y <= y0) and y0 != y1:
                    xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
            if xs:
                self.rect(min(xs), y, max(xs) - min(xs) + 1, 1, c)

    def text(self, x, y, s, c, size):
        x, y = int(x), int(y)
        for ch in s:
            o = ord(ch)
            if o > 255:
                o = ord("?")
            if o >= 176:          # Adafruit 기본(cp437 아님)은 176 부터 한 칸 밀려 있다
                o += 1
            for i in range(5):
                col = GLCD[min(o, 255) * 5 + i]
                for j in range(8):
                    if col >> j & 1:
                        if size == 1:
                            self.px(x + i, y + j, c)
                        else:
                            self.rect(x + i * size, y + j * size, size, size, c)
            x += 6 * size

    def photo_data(self):
        p = self.p
        return " ".join("{" + " ".join(p[y * W:(y + 1) * W]) + "}" for y in range(H))


class Host:
    def __init__(self, name):
        self.name, self.line, self.at = name, "", 0.0
        self.pages, self.page, self.list_at = 0, [""] * MAX_PAGES, 0.0


class State:
    """받은 서버들·위치·날씨. 받는 작업(스레드)이 넣고 화면(Tk)이 읽는다."""

    def __init__(self):
        self.lock = threading.Lock()
        self.hosts = []
        self.host_geo = ""
        self.wx = None
        self.wx_city = ""
        self.error = ""
        self.seq = 0

    def slot(self, name, create):
        for h in self.hosts:
            if h.name == name:
                return h
        if not create:
            return None
        if len(self.hosts) >= NET_MAX:
            self.hosts.remove(min(self.hosts, key=lambda h: h.at))
        h = Host(name)
        self.hosts.append(h)
        return h

    def feed(self, line):
        line = line.strip()
        with self.lock:
            if line.startswith("HOST "):
                h = self.slot(line[5:].split("|", 1)[0], True)
                h.line, h.at = line, time.time()
            elif line.startswith("LIST "):
                parts = line[5:].split("|", 2)
                if len(parts) < 3:
                    return
                h = self.slot(parts[0], False)
                try:
                    k, n = (int(x) for x in parts[1].split("/", 1))
                except ValueError:
                    return
                if h and 1 <= k <= MAX_PAGES and n >= 1:
                    h.pages, h.page[k - 1], h.list_at = min(n, MAX_PAGES), parts[2], time.time()
            elif line.startswith("GEO "):
                g = line[4:].strip()
                if "," in g and len(g) <= 40:
                    self.host_geo = g
            else:
                return
            self.seq += 1


def http_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "hostinfo-window"}), timeout=10) as r:
        return json.loads(r.read().decode("utf-8"))


def weather_loop(state, cfg):
    """10분마다 Open-Meteo(키 없음). 위치 = 설정 → 서버 GEO 줄 → IP 로 대략. 실패하면 1분 뒤."""
    while True:
        wait = 60
        geo = (cfg.get("geo") or "").strip()
        try:
            if geo.lower() == "off":
                state.wx = None
                wait = 30
            else:
                geo = geo or state.host_geo
                if geo:
                    p = [x.strip() for x in geo.split(",")]
                    lat, lon, city = p[0], p[1], p[2] if len(p) > 2 else ""
                else:
                    r = http_json("http://ip-api.com/json/?fields=status,lat,lon,city")
                    if r.get("status") != "success":
                        raise OSError("ip-api failed")
                    lat, lon, city = "%.3f" % r["lat"], "%.3f" % r["lon"], r.get("city", "")
                r = http_json("https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
                              "&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
                              "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1" % (lat, lon))
                c, d = r["current"], r["daily"]
                state.wx = dict(t=c["temperature_2m"], rh=int(c["relative_humidity_2m"]), code=int(c["weather_code"]),
                                day=c["is_day"] == 1, tmax=d["temperature_2m_max"][0], tmin=d["temperature_2m_min"][0],
                                at=time.time())
                state.wx_city = city
                wait = 600
        except Exception as e:   # 인터넷이 없거나 응답이 이상해도 계속
            print("weather failed:", e, flush=True)
        for _ in range(int(wait)):
            time.sleep(1)
            if cfg.get("_geo_changed"):
                cfg["_geo_changed"] = False
                break


def net_loop(state, cfg, dev_id):
    """UDP 4210 받기 + 5초마다 'DISPLAY <id>' 알리기(같은 소켓에서 보내 서버가 4210 으로 답한다)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    try:
        s.bind(("", UDP_PORT))
    except OSError as e:
        state.error = "UDP %d busy" % UDP_PORT
        print("bind failed:", e, flush=True)
        return
    s.settimeout(0.5)
    last_ann = 0.0
    while True:
        now = time.time()
        if now - last_ann >= 5:
            last_ann = now
            dests = [x.strip() for x in (cfg.get("servers") or "").split(",") if x.strip()]
            for ip in dests + ["255.255.255.255", "127.0.0.1"]:
                try:
                    s.sendto(("DISPLAY %s\n" % dev_id).encode(), (ip, ANN_PORT))
                except OSError:
                    pass
        try:
            data, _ = s.recvfrom(4096)
        except socket.timeout:
            continue
        except OSError:
            time.sleep(1)
            continue
        for line in data.decode("utf-8", "replace").splitlines():
            state.feed(line)


class Window:
    def __init__(self, cfg, state):
        self.cfg, self.state = cfg, state
        self.fb = FB()
        self.root = tk.Tk()
        self.root.title("hostinfo")
        self.root.configure(bg=BLACK)
        self.cv = tk.Canvas(self.root, bg=BLACK, highlightthickness=0)
        self.cv.pack()
        self.img = tk.PhotoImage(width=W, height=H)
        self.zoomed = None
        self.item = None
        self.view, self.slot_name, self.page, self.since = "none", None, 0, 0.0
        self.last_key = None
        self.drag = None
        self.apply_window()
        self.cv.bind("<Button-1>", self.on_click)
        self.cv.bind("<B1-Motion>", self.on_drag)
        self.cv.bind("<ButtonRelease-1>", self.on_release)
        self.cv.bind("<Button-3>", self.on_menu)
        self.root.protocol("WM_DELETE_WINDOW", self.quit)
        self.tick()

    # --- 창 ---------------------------------------------------------------
    def apply_window(self):
        self.S = max(1, min(4, int(self.cfg.get("scale") or 1)))
        self.cv.configure(width=W * self.S, height=H * self.S)
        geo = "%dx%d" % (W * self.S, H * self.S)
        if self.cfg.get("x") is not None:
            geo += "+%d+%d" % (self.cfg["x"], self.cfg["y"])
        self.root.geometry(geo)
        self.root.resizable(False, False)
        self.root.attributes("-topmost", bool(self.cfg.get("topmost")))
        self.root.overrideredirect(bool(self.cfg.get("frameless")))
        self.last_key = None

    def remember_pos(self):
        self.cfg["x"], self.cfg["y"] = self.root.winfo_x(), self.root.winfo_y()

    def quit(self):
        self.remember_pos()
        save_cfg(self.cfg)
        self.root.destroy()

    def on_click(self, e):
        self.drag = (e.x_root, e.y_root, self.root.winfo_x(), self.root.winfo_y(), False)

    def on_drag(self, e):
        if not self.drag or not self.cfg.get("frameless"):
            return
        x0, y0, wx, wy, _ = self.drag
        if abs(e.x_root - x0) + abs(e.y_root - y0) > 3:
            self.drag = (x0, y0, wx, wy, True)
            self.root.geometry("+%d+%d" % (wx + e.x_root - x0, wy + e.y_root - y0))

    def on_release(self, e):
        moved = self.drag and self.drag[4]
        self.drag = None
        if moved:
            self.remember_pos()
            save_cfg(self.cfg)
        else:
            self.schedule(force=True)   # 다음 화면
            self.render()

    def on_menu(self, e):
        m = tk.Menu(self.root, tearoff=0)
        m.add_checkbutton(label="항상 위", variable=tk.BooleanVar(value=bool(self.cfg.get("topmost"))),
                          command=lambda: self.toggle("topmost"))
        m.add_checkbutton(label="테두리 없이", variable=tk.BooleanVar(value=bool(self.cfg.get("frameless"))),
                          command=lambda: self.toggle("frameless"))
        sz = tk.Menu(m, tearoff=0)
        for k in (1, 2, 3, 4):
            sz.add_command(label="%d배 (%dx%d)%s" % (k, W * k, H * k, "  ✓" if k == self.S else ""),
                           command=lambda k=k: self.set_scale(k))
        m.add_cascade(label="크기", menu=sz)
        m.add_separator()
        m.add_command(label="서버 주소…", command=lambda: self.ask("servers", "'여기 있음'을 보낼 서버 IP(쉼표로, 보통은 비워 둠)"))
        m.add_command(label="날씨 위치…", command=lambda: self.ask("geo", "위도,경도,이름 (비우면 서버가 알려 준 위치·IP 로, off = 끔)"))
        m.add_command(label="보일 서버…", command=lambda: self.ask("show", "all 또는 서버 호스트 이름"))
        m.add_separator()
        m.add_command(label="종료", command=self.quit)
        m.tk_popup(e.x_root, e.y_root)

    def toggle(self, key):
        self.remember_pos()
        self.cfg[key] = not self.cfg.get(key)
        self.apply_window()
        save_cfg(self.cfg)

    def set_scale(self, k):
        self.remember_pos()
        self.cfg["scale"] = k
        self.apply_window()
        save_cfg(self.cfg)

    def ask(self, key, prompt):
        v = simpledialog.askstring("hostinfo", prompt, initialvalue=self.cfg.get(key) or "", parent=self.root)
        if v is None:
            return
        self.cfg[key] = v.strip() or ("all" if key == "show" else "")
        if key == "geo":
            self.state.wx = None
            self.cfg["_geo_changed"] = True
        save_cfg(self.cfg)

    # --- 화면 순서 ----------------------------------------------------------
    def alive(self, h):
        return h.at and time.time() - h.at < STALE

    def candidates(self):
        show = (self.cfg.get("show") or "all").strip()
        with self.state.lock:
            return [h for h in self.state.hosts if self.alive(h) and (show == "all" or h.name == show)]

    def pages(self, h):
        return h.pages if h.pages and time.time() - h.list_at < LIST_TTL else 0

    def wx_ok(self):
        w = self.state.wx
        return bool(w) and time.time() - w["at"] < 3 * 3600

    def set_view(self, kind, name=None, page=0):
        self.view, self.slot_name, self.page, self.since = kind, name, page, time.time()

    def schedule(self, force=False):
        now = time.time()
        cands = self.candidates()
        if not cands:
            if self.view != "wx":
                self.set_view("wx")
            return
        names = [h.name for h in cands]
        cur = next((h for h in cands if h.name == self.slot_name), None)
        valid = (self.view == "host" and cur) or (self.view == "list" and cur and self.page < self.pages(cur)) or \
                (self.view == "wx" and self.wx_ok())
        dur = HOST_MS if self.view == "host" else LIST_MS if self.view == "list" else WX_MS
        if valid and not force and now - self.since < dur:
            return
        if valid and self.view == "host" and self.pages(cur):
            return self.set_view("list", cur.name, 0)
        if valid and self.view == "list" and self.page + 1 < self.pages(cur):
            return self.set_view("list", cur.name, self.page + 1)
        if self.view in ("host", "list") and self.slot_name in names:
            i = names.index(self.slot_name)
            if i + 1 < len(names):
                return self.set_view("host", names[i + 1])
            if self.wx_ok():
                return self.set_view("wx")
        self.set_view("host", names[0])

    def tick(self):
        try:
            self.schedule()
            self.render()
        except Exception as e:   # 그리다 실패해도 창은 살아 있게
            print("draw failed:", e, flush=True)
        self.root.after(500, self.tick)

    def render(self):
        t = time.localtime()
        key = (self.view, self.slot_name, self.page, self.state.seq, t.tm_min, bool(self.state.wx), self.S)
        if key == self.last_key:
            return
        self.last_key = key
        self.fb.clear()
        self.draw()
        self.img.put(self.fb.photo_data(), to=(0, 0))
        shown = self.img if self.S == 1 else self.img.zoom(self.S, self.S)
        self.zoomed = shown   # 참조를 잡아 둬야 지워지지 않는다
        if self.item is None:
            self.item = self.cv.create_image(0, 0, image=shown, anchor="nw")
        else:
            self.cv.itemconfigure(self.item, image=shown)

    # --- 그리기(펌웨어와 같은 좌표) ----------------------------------------
    def draw(self):
        fb = self.fb
        if self.state.error:
            fb.text((W - 12 * len(self.state.error)) // 2, 110, self.state.error, RED, 2)
            return
        if self.view == "wx":
            return self.draw_weather()
        with self.state.lock:
            h = next((x for x in self.state.hosts if x.name == self.slot_name), None)
        if not h:
            return self.draw_weather()
        if self.view == "list" and self.page < h.pages:
            return self.draw_list(h, self.page)
        self.draw_host(h)

    def fancy(self, i, s):
        fb = self.fb
        y, x, w, cw = TOP_Y + i * LINE_H, PAD_X, W, 6 * TEXT_SIZE
        if not s:
            return
        if i == 0:   # 제목줄('\t' 뒤 = 오른쪽 요약)
            fb.rrect(CORNER_X - 6, y - 3, w - 2 * (CORNER_X - 6), TEXT_SIZE * 8 + 6, 6, HEAD)
            t, tab, r = s.partition("\t")
            fb.text(CORNER_X, y, t, WHITE, 2)
            if tab:
                fb.text(w - CORNER_X - cw * len(r), y, r, RED if r.startswith("!") else GREEN, 2)
            return
        cpu, ram, disk = s.startswith(("CPU ", "GPU ")), s.startswith("RAM "), s.startswith("DISK ")
        if cpu or ram or disk:
            pct = pct_of(s)
            sp = s.find(" ")
            label, rest, cnt = s[:sp], s[sp:], ""
            fb.text(x, y, label, SKY, 2)
            if s.startswith("CPU ") and rest.endswith("t"):   # 끝의 "20c 40t" 는 회색
                b = rest.rfind(" ")
                a = rest.rfind(" ", 0, b)
                if a > 0 and rest[b - 1] == "c":
                    cnt, rest = rest[a:], rest[:a]
            if cnt:
                fb.text(x + (len(label) + len(rest)) * cw, y, cnt, GRAY, 2)
            if cpu and rest.find("C") > 0:   # 온도(와 GPU 전력)는 따로 색
                t = rest.find(" ", rest.find("%"))
                load, tc = (rest[:t], rest[t:]) if t >= 0 else (rest, "")
                deg = num(tc)
                fb.text(x + len(label) * cw, y, load, level(pct), 2)
                fb.text(x + (len(label) + len(load)) * cw, y, tc, GREEN if deg < 60 else YELLOW if deg < 75 else RED, 2)
            else:
                fb.text(x + len(label) * cw, y, rest, WHITE, 2)
            if pct >= 0:   # 글자 아래 막대
                by, bw = y + TEXT_SIZE * 8 + 2, w - 2 * x
                fb.rect(x, by, bw, 5, DIM)
                fb.rect(x, by, bw * min(pct, 100) // 100, 5, level(pct))
            return
        if s.startswith("VM ") and "/" in s:
            sl = s.find("/")
            run, allv = int(num(s[3:sl])), int(num(s[sl + 1:]))
            fb.text(x, y, "VM", SKY, 2)
            fb.text(x + 2 * cw, y, s[2:], GREEN if run == allv else YELLOW if run else RED, 2)
            return
        if s.startswith("up "):
            return fb.text(x, y, s, GRAY, 2)
        fb.text(x, y, s, ORANGE if i == 1 else WHITE, 2)

    def draw_clock(self, top):
        fb = self.fb
        free = H - top - 6
        if free < 20:
            return
        fb.hline(CORNER_X, top + 2, W - 2 * CORNER_X, DIM)
        t = time.localtime()
        hm, date = time.strftime("%H:%M", t), time.strftime("%Y-%m-%d %a", t)
        big = 5 if free >= 70 else 4 if free >= 50 else 3
        small = 2 if free >= 50 else 1
        bh, sh, gap = 8 * big, 8 * small, 6
        y0 = top + 4 + (free - bh - gap - sh) // 2
        fb.text((W - 6 * big * len(hm)) // 2, y0, hm, YELLOW, big)
        fb.text((W - 6 * small * len(date)) // 2, y0 + bh + gap, date, SKY, small)

    def draw_host(self, h):
        fields = [f for f in h.line[5:].split("|") if not f.startswith("@")][:9]
        if h.pages and h.page[0] and time.time() - h.list_at < LIST_TTL and fields:
            p = h.page[0].split("|")
            if len(p) > 2 and p[2]:
                fields[0] += "\t" + p[2]
        for i, f in enumerate(fields):
            self.fancy(i, f)
        self.draw_clock(TOP_Y + len(fields) * LINE_H)

    def draw_list(self, h, pg):
        fb = self.fb
        p = h.page[pg].split("|")
        title = p[0] if p else ""
        if h.pages > 1:
            title += " %d/%d" % (pg + 1, h.pages)
        self.fancy(0, title + ("\t" + p[1] if len(p) > 1 and p[1] else ""))
        y = TOP_Y + 30
        for row in p[3:]:
            if y + 22 > H:
                break
            if not row:
                continue
            if row[0] == "#":   # 소제목(compose 묶음)
                g = row[1:]
                fb.text(8, y + 4, g, GRAY, 1)
                fb.hline(8 + 6 * len(g) + 4, y + 7, W - 6 - (8 + 6 * len(g) + 4), DIM)
                y += 14
                continue
            st, rest = row[0], row[1:].split("~")
            name, val = rest[0], rest[1] if len(rest) > 1 else ""
            mem = int(num(rest[2], -1)) if len(rest) > 2 and rest[2] else -1
            col = {"g": GREEN, "y": YELLOW, "r": RED}.get(st, GRAY)
            fb.circle(10, y + 7, 4, col)
            room = (W - 6 - 12 * len(val) - 6 - 20) // 12
            fb.text(20, y, name[:room], GRAY if st == "o" else WHITE, 2)
            vcol = RED if st == "r" else level(int(num(val))) if val.endswith("%") else GRAY
            fb.text(W - 6 - 12 * len(val), y, val, vcol, 2)
            if mem >= 0:
                bw = W - 26
                fb.rect(20, y + 18, bw, 3, DIM)
                fb.rect(20, y + 18, bw * min(mem, 100) // 100, 3, SKY)
            y += 26

    def draw_weather(self):
        fb = self.fb
        t = time.localtime()
        hm, date = time.strftime("%H:%M", t), time.strftime("%Y-%m-%d %a", t)
        if not self.wx_ok():   # 날씨가 없으면 시계·날짜만 크게
            fb.text((W - 42 * len(hm)) // 2, 72, hm, YELLOW, 7)
            fb.text((W - 12 * len(date)) // 2, 146, date, WHITE, 2)
            return
        w = self.state.wx
        city = (self.state.wx_city or "Weather")[:19]
        fb.text((W - 12 * len(city)) // 2, 8, city, SKY, 2)
        fb.text((W - 36 * len(hm)) // 2, 34, hm, YELLOW, 6)
        fb.text((W - 12 * len(date)) // 2, 90, date, WHITE, 2)
        fb.hline(8, 116, W - 16, DIM)
        k = wx_kind(w["code"])
        self.icon(8, 126, k, w["day"])
        nx = 8 + (64 - 12 * len(WX_NAME[k])) // 2
        fb.text(max(2, nx), 198, WX_NAME[k], WHITE, 2)
        tv = "%.1f" % w["t"]
        if len(tv) > 4:
            tv = "%d" % round(w["t"])
        tc = SKY if w["t"] < 0 else WHITE if w["t"] < 25 else ORANGE if w["t"] < 30 else RED
        rx = 92
        fb.text(rx, 128, tv, tc, 4)
        ex = rx + 24 * len(tv)
        fb.circle_line(ex + 5, 132, 4, tc)
        fb.circle_line(ex + 5, 132, 3, tc)
        fb.text(ex + 12, 128, "C", tc, 4)
        fb.text(rx, 172, "RH", SKY, 2)
        fb.text(rx + 36, 172, "%d%%" % w["rh"], WHITE, 2)
        fb.text(rx, 198, "%d~%dC" % (round(w["tmin"]), round(w["tmax"])), GRAY, 2)

    def sun(self, cx, cy, r):
        for a in range(8):
            an = a * math.pi / 4
            c, s = math.cos(an), math.sin(an)
            for d in (-1, 0, 1):
                self.fb.line(cx + c * (r + 4) + d * s, cy + s * (r + 4) - d * c,
                             cx + c * (r + 9) + d * s, cy + s * (r + 9) - d * c, YELLOW)
        self.fb.circle(cx, cy, r, YELLOW)

    def moon(self, cx, cy, r):
        self.fb.circle(cx, cy, r, YELLOW)
        self.fb.circle(cx + r // 2, cy - r // 3, r - 1, BLACK)

    def cloud(self, x, y, c):   # 56x32
        fb = self.fb
        fb.circle(x + 16, y + 18, 12, c)
        fb.circle(x + 32, y + 13, 13, c)
        fb.circle(x + 45, y + 21, 10, c)
        fb.rrect(x + 4, y + 18, 52, 14, 7, c)

    def icon(self, x, y, k, day):   # 64x64
        fb = self.fb
        if k == 0:
            self.sun(x + 32, y + 32, 14) if day else self.moon(x + 32, y + 32, 17)
        elif k == 1:
            self.sun(x + 42, y + 20, 10) if day else self.moon(x + 44, y + 18, 12)
            self.cloud(x + 2, y + 26, WHITE)
        elif k == 2:
            self.cloud(x + 12, y + 8, GRAY)
            self.cloud(x + 2, y + 22, LIGHT)
        elif k == 3:
            for i in range(5):
                fb.rrect(x + 4 + (i % 2) * 8, y + 10 + i * 10, 48, 5, 2, LIGHT)
        elif k == 4:
            self.cloud(x + 4, y + 6, LIGHT)
            for i in range(4):
                for d in range(2):
                    fb.line(x + 16 + i * 11 + d, y + 44, x + 11 + i * 11 + d, y + 58, SKY)
        elif k == 5:
            self.cloud(x + 4, y + 6, LIGHT)
            for i in range(4):
                fb.circle(x + 14 + i * 11, y + 47 + (i % 2) * 7, 3, WHITE)
        else:
            self.cloud(x + 4, y + 6, GRAY)
            fb.tri(((x + 34, y + 36), (x + 22, y + 52), (x + 32, y + 52)), YELLOW)
            fb.tri(((x + 30, y + 50), (x + 40, y + 50), (x + 26, y + 64)), YELLOW)


def main():
    import argparse
    ap = argparse.ArgumentParser(description="hostinfo 화면을 PC 창으로(240x240)")
    ap.add_argument("--scale", type=int, help="배율 1~4(기본 1 = 240x240, 화면 보드와 같은 크기)")
    ap.add_argument("--servers", help="'여기 있음'을 보낼 서버 IP(쉼표로)")
    ap.add_argument("--geo", help="날씨 위치 위도,경도,이름 (off = 끔)")
    a = ap.parse_args()
    cfg = load_cfg()
    for k in ("scale", "servers", "geo"):
        if getattr(a, k) is not None:
            cfg[k] = getattr(a, k)
    dev_id = "window-" + socket.gethostname().split(".")[0].lower()
    state = State()
    threading.Thread(target=net_loop, args=(state, cfg, dev_id), daemon=True).start()
    threading.Thread(target=weather_loop, args=(state, cfg), daemon=True).start()
    Window(cfg, state).root.mainloop()


if __name__ == "__main__":
    main()
