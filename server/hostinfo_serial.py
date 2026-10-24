#!/usr/bin/env python3
"""이 컴퓨터의 상태를 USB 로 꽂은 화면 보드에 보낸다. 리눅스·macOS·Windows 같은 형식, 파이썬 표준 라이브러리만.

받는 보드: NodeMCU 표시 전용(hosts/hostinfo/display8266, 115200) 또는 ESP32-S3 433MHz 보드(hosts/esp32-rf, 921600).
3초마다 한 줄:
  HOST <호스트 이름>|<IP>|CPU <n>% <온도>C|RAM <쓴>/<전체>G|DISK <n>% <루트>|VM <실행>/<전체> run|up <시간>|@<날짜 요일 시:분>
  (온도는 리눅스에서만 — Windows·맥은 일반 권한으로 못 읽는다. VM 은 Proxmox 일 때만)
보드는 받은 줄을 화면에 그리고, 20초 넘게 안 오면 '응답 없음'(또는 보드 상태 화면)으로 바뀐다.
GPU 가 있으면(nvidia-smi) 'GPU 35% 58C 92W' 줄을 더한다.

목록(리눅스만, 2026-10-20): Proxmox 면 VM(pvesh), Docker 면 컨테이너(docker ps·stats)를 15초마다 따로 모아
  LIST <호스트>|<쪽>/<전체>|<제목>|<오른쪽>|<요약>|<줄>...
로 9초마다 보낸다. 줄 = <상태 g|y|r|o><이름>~<값>~<메모리 %>, '#이름' = 소제목(compose 묶음).
문제(unhealthy·재시작·비정상 종료)는 빨강으로 맨 위, 요약 'D 4/4'(문제가 있으면 '!1 D 5/6')은 서버 정보 제목줄 오른쪽에.

정해 둔 보드(USB 일련번호)가 꽂혀 있을 때만 보낸다 — 같은 칩을 쓰는 다른 보드(433MHz rf433d 등)를 건드리지 않게.

  python hostinfo_serial.py                              # NodeMCU(CP210x)를 저절로 찾아 115200 으로(Ctrl+C 로 끝)
  hostinfo-serial --serial 5D01163736 --baud 921600      # 일련번호로 찾기(쉼표로 여러 개, 이름 일부여도 됨)
  hostinfo-serial --port /dev/ttyUSB0 --baud 115200      # 리눅스 포트를 직접
  python hostinfo-serial --port COM7 --baud 115200       # Windows
  hostinfo-serial --dry --once                            # 한 줄만 만들어 출력(보내지 않음)
  hostinfo-serial --list                                  # 보이는 시리얼 포트와 일련번호
  hostinfo-serial --broadcast                             # 같은 줄을 LAN 브로드캐스트(UDP 4210)로도 — Wi-Fi 화면 보드가 받음

--broadcast 는 USB 보드가 없어도 보낸다. 주소를 안 주면 이 컴퓨터 LAN 주소의 /24 브로드캐스트(예 192.168.1.255).
기업용 무선 컨트롤러·AP 격리처럼 유선 브로드캐스트를 Wi-Fi 로 넘기지 않는 곳이 있어,
Wi-Fi 보드가 5초마다 'DISPLAY <id>'를 UDP 4211 로 알리면 그 주소로도 직접 보낸다(30초 안 들리면 뺌).
"""
import argparse
import glob
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time

INTERVAL = 3
UDP_PORT = 4210
ANNOUNCE_PORT = 4211
DISPLAY_TTL = 30
WIN = sys.platform == "win32"


# --- 상태 읽기 ---------------------------------------------------------------
if WIN:
    import ctypes
    from ctypes import wintypes

    class _FT(ctypes.Structure):
        _fields_ = [("lo", wintypes.DWORD), ("hi", wintypes.DWORD)]

    class _MEM(ctypes.Structure):
        _fields_ = [("dwLength", wintypes.DWORD), ("dwMemoryLoad", wintypes.DWORD),
                    ("total", ctypes.c_ulonglong), ("avail", ctypes.c_ulonglong),
                    ("tp", ctypes.c_ulonglong), ("ap", ctypes.c_ulonglong),
                    ("tv", ctypes.c_ulonglong), ("av", ctypes.c_ulonglong), ("ae", ctypes.c_ulonglong)]

    def cpu_times():
        idle, kern, user = _FT(), _FT(), _FT()
        ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kern), ctypes.byref(user))
        v = lambda f: (f.hi << 32) | f.lo
        return v(kern) + v(user), v(idle)   # kernel 시간에 idle 이 들어 있다

    def mem():
        m = _MEM()
        m.dwLength = ctypes.sizeof(_MEM)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
        return (m.total - m.avail) / 2**30, m.total / 2**30

    def temp():
        return None   # Windows 는 관리자 권한·제조사 도구 없이 CPU 온도를 주지 않는다

    def uptime_sec():
        ctypes.windll.kernel32.GetTickCount64.restype = ctypes.c_ulonglong
        return ctypes.windll.kernel32.GetTickCount64() // 1000

    ROOT = os.environ.get("SystemDrive", "C:") + "\\"
elif sys.platform == "darwin":   # macOS: sysctl·vm_stat·ps 로 읽는다(모두 기본 설치)
    def _sysctl(name):
        return subprocess.run(["sysctl", "-n", name], capture_output=True, text=True, timeout=5).stdout.strip()

    _NCPU = int(_sysctl("hw.ncpu") or 1)

    def cpu_times():
        """맥은 누적 CPU 시간을 쉽게 못 읽어 'ps 의 %CPU 합 ÷ 코어 수'를 쓴다. build() 와 모양을 맞추려고
        (100, 100 - 사용률) 을 돌려준다 — 이전 값과의 차이가 아니라 지금 값이 그대로 나온다."""
        out = subprocess.run(["ps", "-A", "-o", "%cpu="], capture_output=True, text=True, timeout=5).stdout
        used = sum(float(x) for x in out.split() if x.replace(".", "", 1).isdigit()) / _NCPU
        return None, max(0.0, min(100.0, used))

    def mem():
        total = int(_sysctl("hw.memsize") or 0)
        vm = subprocess.run(["vm_stat"], capture_output=True, text=True, timeout=5).stdout
        page = int(re.search(r"page size of (\d+)", vm).group(1)) if "page size of" in vm else 16384
        pages = {k.strip(): int(v.strip(" .")) for k, v in (l.split(":", 1) for l in vm.splitlines()[1:] if ":" in l)
                 if v.strip(" .").isdigit()}
        avail = (pages.get("Pages free", 0) + pages.get("Pages inactive", 0) + pages.get("Pages speculative", 0)) * page
        return (total - avail) / 2**30, total / 2**30

    def temp():
        return None   # 맥은 권한·별도 도구 없이 CPU 온도를 주지 않는다

    def uptime_sec():
        m = re.search(r"sec = (\d+)", _sysctl("kern.boottime"))
        return int(time.time() - int(m.group(1))) if m else 0

    ROOT = "/"
else:
    def cpu_times():
        with open("/proc/stat") as f:
            v = [int(x) for x in f.readline().split()[1:]]
        return sum(v), v[3] + (v[4] if len(v) > 4 else 0)

    def mem():
        m = {}
        with open("/proc/meminfo") as f:
            for line in f:
                k, v = line.split(":", 1)
                m[k] = int(v.split()[0])
        return (m["MemTotal"] - m.get("MemAvailable", m.get("MemFree", 0))) / 1048576, m["MemTotal"] / 1048576

    def temp():
        """CPU 패키지 온도(°C). coretemp·k10temp 의 첫 값(Package·Tctl), 없으면 thermal_zone, 그래도 없으면 None."""
        for d in glob.glob("/sys/class/hwmon/hwmon*"):
            try:
                name = open(os.path.join(d, "name")).read().strip()
            except OSError:
                continue
            if name in ("coretemp", "k10temp", "zenpower", "cpu_thermal"):
                for t in sorted(glob.glob(os.path.join(d, "temp*_input"))):
                    try:
                        return int(open(t).read()) / 1000
                    except (OSError, ValueError):
                        continue
        best = None
        for z in glob.glob("/sys/class/thermal/thermal_zone*/temp"):
            try:
                v = int(open(z).read()) / 1000
            except (OSError, ValueError):
                continue
            if 0 < v < 120:
                best = v if best is None else max(best, v)
        return best

    def uptime_sec():
        with open("/proc/uptime") as f:
            return int(float(f.read().split()[0]))

    ROOT = "/"


def cpu_counts():
    """(물리 코어 수, 스레드 수). 모르면 코어 = 스레드. CPU 줄 뒤에 '20c 40t' 로 붙는다(처음 한 번만 센다)."""
    threads = os.cpu_count() or 1
    cores = None
    try:
        if WIN:   # GetLogicalProcessorInformation 의 RelationProcessorCore(0) 항목 수
            k = ctypes.windll.kernel32
            n = wintypes.DWORD(0)
            k.GetLogicalProcessorInformation(None, ctypes.byref(n))
            buf = (ctypes.c_ubyte * n.value)()
            if k.GetLogicalProcessorInformation(buf, ctypes.byref(n)):
                p = ctypes.sizeof(ctypes.c_void_p)            # ProcessorMask 다음이 Relationship
                size = 2 * p + 16                              # x64 32바이트
                cores = sum(1 for o in range(0, n.value - size + 1, size)
                            if int.from_bytes(bytes(buf[o + p:o + p + 4]), "little") == 0)
        elif sys.platform == "darwin":
            cores = int(_sysctl("hw.physicalcpu") or 0)
            threads = int(_sysctl("hw.logicalcpu") or threads)
        else:
            pairs, phys, core = set(), None, None
            with open("/proc/cpuinfo") as f:
                for line in f:
                    k_, _, v = line.partition(":")
                    k_ = k_.strip()
                    if k_ == "physical id":
                        phys = v.strip()
                    elif k_ == "core id":
                        core = v.strip()
                    elif not line.strip():
                        if core is not None:
                            pairs.add((phys, core))
                        phys = core = None
            if core is not None:
                pairs.add((phys, core))
            cores = len(pairs)
    except (OSError, ValueError, AttributeError):
        cores = None
    return (cores if cores else threads), threads


CPU_COUNTS = None


def ip_addr():
    """기본 경로로 나가는 IPv4(LAN 주소). 패킷은 보내지 않는다."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))
        return s.getsockname()[0]
    except OSError:
        return "-"
    finally:
        s.close()


def vms():
    """Proxmox 면 (실행 중, 전체) VM 수. 아니면 None."""
    if WIN or not shutil.which("qm"):
        return None
    try:
        out = subprocess.run(["qm", "list"], capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    rows = [l.split() for l in out.splitlines()[1:] if l.strip()]
    return sum(1 for r in rows if len(r) > 2 and r[2] == "running"), len(rows)


def gpu():
    """NVIDIA GPU 가 보이면 'GPU 35% 58C 92W'. 없으면 None."""
    if WIN or not shutil.which("nvidia-smi"):
        return None
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu,temperature.gpu,power.draw",
                              "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10).stdout
        u, t, w = [x.strip() for x in out.splitlines()[0].split(",")]
        return f"GPU {int(float(u))}% {int(float(t))}C" + (f" {float(w):.0f}W" if w.replace(".", "", 1).isdigit() else "")
    except (OSError, subprocess.TimeoutExpired, IndexError, ValueError):
        return None


# --- 목록(Proxmox VM · Docker 컨테이너) -------------------------------------------------
SCREEN_H, LIST_TOP, ROW_H, HEAD_H, MAX_PAGES = 240, 40, 26, 14, 4


def host_name():
    return socket.gethostname().split(".")[0][:19]


def _run(cmd, timeout=20):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.stdout if r.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def proxmox_list():
    """(제목, 오른쪽, 요약, 줄들) 또는 None. 줄 = (상태, 이름, 값, 메모리%|None)."""
    if WIN or not shutil.which("pvesh"):
        return None
    out = _run(["pvesh", "get", "/cluster/resources", "--type", "vm", "--output-format", "json"])
    if out is None:
        return None
    import json
    me = socket.gethostname().split(".")[0]
    vms = [v for v in json.loads(out) if v.get("node") == me and not v.get("template")]
    if not vms:
        return None
    rows = []
    for v in sorted(vms, key=lambda v: (v.get("status") != "running", v.get("vmid", 0))):
        name = (v.get("name") or str(v.get("vmid")))
        if v.get("status") == "running":
            mx = v.get("maxmem") or 0
            rows.append(("g", name, f"{round(100 * (v.get('cpu') or 0))}%",
                         round(100 * (v.get("mem") or 0) / mx) if mx else None))
        elif v.get("status") == "paused":
            rows.append(("y", name, "PAU", None))
        else:
            rows.append(("o", name, "off", None))
    run = sum(1 for r in rows if r[0] == "g")
    return host_name(), f"{run}/{len(rows)}", "", [rows]


def _dstate(c):
    """docker ps 한 줄 → (상태, 값). 값은 문제일 때만(아니면 CPU% 로 채움)."""
    st, status = c.get("State", ""), c.get("Status", "")
    if st == "running":
        if "(unhealthy)" in status:
            return "r", "UNH"
        if "health: starting" in status:
            return "y", None
        return "g", None
    if st == "restarting":
        return "r", "RST"
    if st == "paused":
        return "y", "PAU"
    if st == "exited":
        m = re.search(r"Exited \((\d+)\)", status)
        code = int(m.group(1)) if m else 0
        return ("o", "exit") if code in (0, 143) else ("r", f"E{code}"[:4])
    if st == "dead":
        return "r", "dead"
    return "o", st[:4] or "?"


def docker_list():
    if WIN or sys.platform == "darwin" or not shutil.which("docker"):
        return None
    out = _run(["docker", "ps", "-a", "--format", "{{json .}}"])
    if out is None:
        return None
    import json
    cs = [json.loads(l) for l in out.splitlines() if l.strip()]
    if not cs:
        return None
    stats = {}
    so = _run(["docker", "stats", "--no-stream", "--format", "{{json .}}"], timeout=30) or ""
    for l in so.splitlines():
        try:
            d = json.loads(l)
            stats[d["Name"]] = (float(d["CPUPerc"].rstrip("%") or 0), float(d["MemPerc"].rstrip("%") or 0))
        except (ValueError, KeyError):
            pass
    groups = {}
    for c in cs:
        name = c.get("Names", "").split(",")[0]
        labels = dict(kv.split("=", 1) for kv in c.get("Labels", "").split(",") if "=" in kv)
        proj = labels.get("com.docker.compose.project", "")
        short = name
        if proj:
            for sep in ("-", "_"):
                if short.startswith(proj + sep):
                    short = short[len(proj) + 1:]
            short = re.sub(r"[-_]1$", "", short)
        st, val = _dstate(c)
        cpu, mem = stats.get(name, (None, None))
        if val is None:
            val = f"{round(cpu)}%" if cpu is not None else "-"
        groups.setdefault(proj, []).append((st, short, val, round(mem) if mem is not None and st != "o" else None))
    for k in [k for k, g in groups.items() if k and len(g) == 1 and g[0][1] == k]:   # 혼자인 묶음(이름이 같음) → 소제목 없이
        groups.setdefault("", []).append(groups.pop(k)[0])
    sev = {"r": 0, "y": 1, "g": 2, "o": 3}
    for g in groups.values():
        g.sort(key=lambda r: (sev[r[0]], r[1]))
    order = sorted(groups, key=lambda k: (min(sev[r[0]] for r in groups[k]), k == "", k))
    named = any(k for k in groups)
    sections = [((k or "etc") if named else None, groups[k]) for k in order]
    allrows = [r for g in groups.values() for r in g]
    run = sum(1 for r in allrows if r[0] in ("g", "y"))
    bad = sum(1 for r in allrows if r[0] == "r")
    right = (f"!{bad} " if bad else "") + f"{run}/{len(allrows)}"
    return host_name(), right, ("!" + str(bad) + " " if bad else "") + f"D {run}/{len(allrows)}", sections


def _clean(x):
    return str(x).replace("|", "/").replace("~", "-")


def paginate(sections):
    """[(소제목|None, 줄들)] → 쪽마다 문자열 줄 목록. 화면 높이 240 기준(펌웨어 drawList 와 같은 계산)."""
    pages, cur, y = [], [], LIST_TOP

    def fits(h):
        return y + 22 <= SCREEN_H and (h == ROW_H or y + HEAD_H + 22 <= SCREEN_H)

    for head, rows in sections:
        first = True
        for st, name, val, mem in rows:
            need_head = head is not None and (first or not cur)
            if need_head and not fits(HEAD_H) or not need_head and not fits(ROW_H):
                pages.append(cur)
                cur, y = [], LIST_TOP
                need_head = head is not None
            if need_head:
                cur.append("#" + _clean(head))
                y += HEAD_H
            cur.append(f"{st}{_clean(name)}~{_clean(val)}~{'' if mem is None else mem}")
            y += ROW_H
            first = False
    if cur:
        pages.append(cur)
    return pages[:MAX_PAGES]


def list_lines(item):
    title, right, summary, sections = item
    if sections and not isinstance(sections[0], tuple):   # proxmox: 소제목 없는 한 묶음
        sections = [(None, sections[0])]
    pages = paginate(sections)
    return [f"LIST {host_name()}|{i + 1}/{len(pages)}|{_clean(title)}|{_clean(right)}|{_clean(summary)}|" + "|".join(pg)
            for i, pg in enumerate(pages)]


class Lists:
    """15초마다 뒤에서 목록을 모은다(docker stats 가 2초쯤 걸려 상태 줄을 늦추지 않게)."""

    def __init__(self):
        self.lines = []
        self.lock = threading.Lock()
        threading.Thread(target=self.loop, daemon=True).start()

    def loop(self):
        while True:
            lines = []
            for f in (docker_list, proxmox_list):
                try:
                    item = f()
                except Exception as e:   # 목록이 실패해도 상태 줄은 계속
                    print(f"{f.__name__} failed: {e}", flush=True)
                    item = None
                if item:
                    lines += list_lines(item)
            with self.lock:
                self.lines = lines
            time.sleep(15)

    def get(self):
        with self.lock:
            return list(self.lines)


def fmt_uptime(s):
    d, s = divmod(s, 86400)
    h, s = divmod(s, 3600)
    return f"{d}d {h}h" if d else f"{h}h {s // 60}m"


def build(prev):
    tot, idle = cpu_times()
    if tot is None:   # 맥: idle 자리에 지금 사용률이 바로 들어 있다
        cpu = round(idle)
    else:
        cpu = max(0, min(100, 100 - round(100 * (idle - prev[1]) / max(1, tot - prev[0]))))
    used, total = mem()
    t = temp()
    du = shutil.disk_usage(ROOT)
    global CPU_COUNTS
    if CPU_COUNTS is None:
        CPU_COUNTS = cpu_counts()
    fields = [
        host_name(),
        ip_addr(),
        f"CPU {cpu}%" + (f" {t:.0f}C" if t is not None else "") + f" {CPU_COUNTS[0]}c {CPU_COUNTS[1]}t",
        f"RAM {used:.1f}/{total:.0f}G",
        f"DISK {round(100 * du.used / du.total)}% {ROOT.rstrip(chr(92)) or '/'}",
    ]
    v = vms()
    if v:
        fields.append(f"VM {v[0]}/{v[1]} run")
    g = gpu()
    if g:
        fields.append(g)
    fields.append(f"up {fmt_uptime(uptime_sec())}")
    fields.append(time.strftime("@%Y-%m-%d %a %H:%M"))   # 화면 아래 큰 시계(보드에는 시계가 없다)
    return "HOST " + "|".join(f.replace("|", "/") for f in fields), (tot, idle)


# --- 시리얼 ------------------------------------------------------------------
def list_ports():
    """[(포트, 설명·일련번호 문자열)]."""
    if WIN:
        # 레지스트리에서 바로 읽는다(3초마다 PowerShell 을 띄우면 창이 깜빡였다, 2026-10-20).
        # 값 이름이 드라이버 장치 이름이라 CP210x = \Device\Silabser*, CH343 = \Device\CH343*.
        import winreg
        res = []
        try:
            k = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM")
        except OSError:
            return res
        i = 0
        while True:
            try:
                name, port, _ = winreg.EnumValue(k, i)
            except OSError:
                break
            i += 1
            desc = name + (" CP210x" if "silabser" in name.lower() else "")
            res.append((port, desc))
        winreg.CloseKey(k)
        return res
    if sys.platform == "darwin":   # 맥: /dev/cu.*(CP210x 는 cu.SLAB_USBtoUART 또는 cu.usbserial-<일련번호>)
        return [(p, os.path.basename(p)) for p in sorted(glob.glob("/dev/cu.*"))
                if "Bluetooth" not in p and "debug-console" not in p]
    return [(os.path.realpath(p), os.path.basename(p)) for p in sorted(glob.glob("/dev/serial/by-id/*"))]


def find_port(serials):
    for s in serials:
        for port, desc in list_ports():
            if s.lower() in desc.lower():
                return port
    return None


class Port:
    """쓰기 전용으로 연다(받은 줄은 리눅스에서만 비운다). DTR·RTS 를 내려 보드가 리셋되지 않게."""

    def __init__(self, path, baud):
        self.path = path
        if WIN:
            # 포트를 연 뒤 Win32 SetCommState 로 속도를 맞춘다(CP210x 는 열 때 이전 설정을 버린다 — 2026-10-20
            # mode 로 미리 맞춰도 9600 으로 열려 보드가 못 알아들었다). DTR·RTS 는 끈다(보드 리셋 방지).
            import msvcrt
            self.f = open(rf"\\.\{path}", "wb", buffering=0)
            self.fd = None
            h = msvcrt.get_osfhandle(self.f.fileno())

            class DCB(ctypes.Structure):
                _fields_ = [("DCBlength", wintypes.DWORD), ("BaudRate", wintypes.DWORD), ("flags", wintypes.DWORD),
                            ("wReserved", wintypes.WORD), ("XonLim", wintypes.WORD), ("XoffLim", wintypes.WORD),
                            ("ByteSize", ctypes.c_ubyte), ("Parity", ctypes.c_ubyte), ("StopBits", ctypes.c_ubyte),
                            ("XonChar", ctypes.c_char), ("XoffChar", ctypes.c_char), ("ErrorChar", ctypes.c_char),
                            ("EofChar", ctypes.c_char), ("EvtChar", ctypes.c_char), ("wReserved1", wintypes.WORD)]

            dcb = DCB()
            dcb.DCBlength = ctypes.sizeof(DCB)
            k32 = ctypes.windll.kernel32
            if not k32.GetCommState(h, ctypes.byref(dcb)):
                raise OSError("GetCommState failed")
            dcb.BaudRate, dcb.ByteSize, dcb.Parity, dcb.StopBits = baud, 8, 0, 0
            dcb.flags = 0x0001   # fBinary 만(흐름 제어·DTR·RTS 모두 끔)
            if not k32.SetCommState(h, ctypes.byref(dcb)):
                raise OSError("SetCommState failed")
            k32.EscapeCommFunction(h, 6)   # CLRDTR
            k32.EscapeCommFunction(h, 4)   # CLRRTS
        else:
            import termios
            self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            a = termios.tcgetattr(self.fd)
            a[0] = a[1] = a[3] = 0
            a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            a[4] = a[5] = {115200: termios.B115200}.get(baud, getattr(termios, "B921600", 0o010007))
            termios.tcsetattr(self.fd, termios.TCSANOW, a)
            try:
                import fcntl
                import struct
                fcntl.ioctl(self.fd, termios.TIOCMBIC, struct.pack("I", termios.TIOCM_DTR | termios.TIOCM_RTS))
                # 독점으로 연다: 굽는 프로그램(esptool·pio)이 같은 포트를 열면 '사용 중'으로 바로 실패하게
                # (리눅스는 여러 프로그램이 한 포트를 열 수 있어 굽는 중에 상태 줄이 끼어들면 굽기가 깨진다)
                fcntl.ioctl(self.fd, termios.TIOCEXCL)
            except (OSError, AttributeError):
                pass

    def write(self, line):
        data = (line + "\n").encode()
        if self.fd is None:
            self.f.write(data)
            return
        try:
            os.read(self.fd, 65536)   # 보드가 내는 줄(BLE 등)은 버린다
        except BlockingIOError:
            pass
        os.write(self.fd, data)

    def close(self):
        try:
            if self.fd is None:
                self.f.close()
            else:
                os.close(self.fd)
        except OSError:
            pass


def bcast_addr(arg):
    if arg != "auto":
        return arg
    ip = ip_addr()
    return ip.rsplit(".", 1)[0] + ".255" if ip.count(".") == 3 else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial", default="", help="보드 USB 일련번호(쉼표로 여러 개, 포트 이름·설명의 일부)")
    ap.add_argument("--port", help="포트를 직접(리눅스 /dev/ttyUSB0, Windows COM7)")
    ap.add_argument("--baud", type=int, default=115200, choices=(115200, 921600),
                    help="NodeMCU 표시 전용 115200(기본), ESP32-S3 433MHz 보드 921600")
    ap.add_argument("--once", action="store_true", help="한 번만")
    ap.add_argument("--dry", action="store_true", help="보내지 않고 출력만")
    ap.add_argument("--list", action="store_true", help="보이는 시리얼 포트")
    ap.add_argument("--broadcast", nargs="?", const="auto", metavar="ADDR",
                    help="LAN 브로드캐스트(UDP %d)로도 보냄. 주소를 안 주면 LAN 주소의 /24 .255" % UDP_PORT)
    ap.add_argument("--no-usb", action="store_true",
                    help="USB 보드를 찾지 않음(--broadcast 만) — 같은 CP210x 칩을 쓰는 Zigbee 동글이 있는 컴퓨터")
    ap.add_argument("--geo", metavar="LAT,LON,NAME",
                    help="이 집 위치 — 날씨 위치를 따로 넣지 않은 화면 보드가 쓴다(예 37.566,126.978,Seoul)")
    ap.add_argument("--no-lists", action="store_true", help="Proxmox VM·Docker 목록을 보내지 않음")
    ap.add_argument("--log", help="출력을 이 파일에(1MB 넘으면 비우고 다시) — 창 없이 돌릴 때")
    a = ap.parse_args()
    if a.log:
        try:
            if os.path.getsize(a.log) > 1_000_000:
                open(a.log, "w").close()
        except OSError:
            pass
        sys.stdout = sys.stderr = open(a.log, "a", encoding="utf-8", buffering=1)
    if a.list:
        for port, desc in list_ports():
            print(port, desc)
        return
    serials = [s for s in a.serial.split(",") if s]
    udp = None
    if a.broadcast and not a.dry:
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        print(f"broadcast {bcast_addr(a.broadcast)}:{UDP_PORT}", flush=True)
        ann = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        ann.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            ann.bind(("", ANNOUNCE_PORT))
            ann.setblocking(False)
        except OSError as e:
            print(f"listen {ANNOUNCE_PORT} failed: {e}", flush=True)
            ann = None
    displays = {}   # Wi-Fi 보드 IP -> 마지막으로 들은 때
    if not (a.port or serials or a.dry or a.no_usb):
        # 아무것도 안 주면 NodeMCU 표시 전용 보드(CP210x)를 찾는다. 433MHz ESP32 보드(CH343)는 건드리지 않는다
        serials = ["CP210", "CP2102", "SLAB_USBtoUART", "usbserial"]   # 리눅스·Windows 이름, 맥 이름
        print("포트를 주지 않아 CP210x(NodeMCU 화면 보드)를 찾습니다. 다른 포트는 --port 로", flush=True)

    prev = cpu_times()
    lists = None if a.no_lists else Lists()
    if lists and (a.dry or a.once):
        time.sleep(4)   # 첫 목록을 기다림(docker stats 약 2초)
    time.sleep(1)
    port, cur, last_err = None, None, None
    tick = 0
    while True:
        line, prev = build(prev)
        extra = lists.get() if lists and (tick % 3 == 0 or a.once) else []   # 목록은 9초마다
        if a.geo and (tick % 3 == 0 or a.once):
            extra.append("GEO " + a.geo.replace("|", "/"))
        tick += 1
        if a.dry:
            print(line, flush=True)
            for l in extra:
                print(l, flush=True)
        elif not a.no_usb:
            path = a.port or find_port(serials)
            if path != cur:   # 꽂고 뺄 때 다시 연다
                if port and not path:
                    print(f"{cur} 빠짐 — 꽂히면 다시 보낸다", flush=True)
                if port:
                    port.close()
                port, cur = None, path
                if path:
                    try:
                        port = Port(path, a.baud)
                        print(f"opened {path} ({a.baud})", flush=True)
                        last_err = None
                    except OSError as e:
                        if str(e) != last_err:   # 같은 실패를 3초마다 되풀이해 남기지 않는다
                            print(f"open {path} failed: {e}", flush=True)
                            last_err = str(e)
                        cur = None
            if port:
                try:
                    port.write(line)
                    for l in extra:   # 목록·위치(433MHz ESP32 보드도 2026-10-20 펌웨어부터 받음)
                        port.write(l)
                except OSError as e:
                    print(f"write failed: {e}", flush=True)
                    port.close()
                    port, cur = None, None
        if udp:
            now = time.time()
            while ann:
                try:
                    d, (ip, _) = ann.recvfrom(200)
                except OSError:   # 받을 것 없음(BlockingIOError 포함)
                    break
                if d.startswith(b"DISPLAY "):
                    if ip not in displays:
                        print(f"display {ip} {d[8:].decode('ascii', 'replace').strip()}", flush=True)
                    displays[ip] = now
            for ip in [k for k, t in displays.items() if now - t > DISPLAY_TTL]:
                print(f"display {ip} 안 들림 — 뺌", flush=True)
                del displays[ip]
            dst = bcast_addr(a.broadcast)   # 주소가 바뀌어도(DHCP) 따라간다
            try:
                for one in [line] + extra:
                    data = (one + "\n").encode("utf-8")
                    if dst:
                        udp.sendto(data, (dst, UDP_PORT))
                    for ip in displays:
                        udp.sendto(data, (ip, UDP_PORT))
            except OSError as e:
                if str(e) != last_err:
                    print(f"broadcast failed: {e}", flush=True)
                    last_err = str(e)
        if a.once:
            return
        time.sleep(INTERVAL)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("끝", flush=True)
