"""글 맨 위 그림: SmallTV 세 대가 서로 다른 화면을 보이는 모습(그린 그림, 사진 아님).
  PREVIEW_EACH=<폴더> python tools/preview.py <glcdfont.c> docs/images/smalltv-screens.png tools/sample-screens.txt
  python tools/make-hero.py <그 폴더> docs/images/smalltv-hero.png
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

SRC, OUT = sys.argv[1], sys.argv[2]
PICK = ["screen1.png", "screen4.png", "screen5.png"]   # 서버 정보 · Docker 목록(문제 1) · 날씨
W, H = 2160, 1000
S = 2   # 화면 240px → 480px

bg = Image.new("RGB", (W, H))
d = ImageDraw.Draw(bg)
for y in range(H):   # 위는 어두운 남색, 아래는 책상 쪽으로 조금 밝게
    t = y / H
    d.line([0, y, W, y], fill=(int(18 + 22 * t), int(22 + 24 * t), int(34 + 26 * t)))


def font(size, bold=False):
    for f in (("C:/Windows/Fonts/malgunbd.ttf" if bold else "C:/Windows/Fonts/malgun.ttf"),
              "/System/Library/Fonts/AppleSDGothicNeo.ttc", "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def device(scr):
    """흰 정육면체 케이스 + 검은 유리 + 화면."""
    sw = 240 * S
    pad, glass = 46, 26
    bw, bh = sw + 2 * (pad + glass), sw + 2 * (pad + glass) + 40
    im = Image.new("RGBA", (bw, bh), (0, 0, 0, 0))
    g = ImageDraw.Draw(im)
    g.rounded_rectangle([0, 0, bw - 1, bh - 1], 46, fill=(236, 236, 232))
    g.rounded_rectangle([6, 6, bw - 7, bh - 7], 42, fill=(248, 248, 245))
    g.rounded_rectangle([pad, pad, bw - pad, pad + sw + 2 * glass], 18, fill=(12, 12, 14))
    im.paste(scr.resize((sw, sw), Image.NEAREST), (pad + glass, pad + glass))
    shine = Image.new("RGBA", (sw + 2 * glass, sw + 2 * glass), (0, 0, 0, 0))   # 유리 반사
    ImageDraw.Draw(shine).polygon([(0, 0), (int(sw * 0.55), 0), (0, int(sw * 0.45))], fill=(255, 255, 255, 18))
    im.alpha_composite(shine, (pad, pad))
    g.ellipse([bw // 2 - 5, bh - 34, bw // 2 + 5, bh - 24], fill=(200, 200, 196))   # 아래 표시등 자리
    return im


devs = [device(Image.open(os.path.join(SRC, p)).convert("RGB")) for p in PICK]
dw, dh = devs[0].size
gap = 60
x0 = (W - (3 * dw + 2 * gap)) // 2
y0 = 190
canvas = bg.convert("RGBA")
for i, dv in enumerate(devs):
    x = x0 + i * (dw + gap)
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))   # 그림자
    ImageDraw.Draw(sh).rounded_rectangle([x + 20, y0 + 40, x + dw + 10, y0 + dh + 30], 50, fill=(0, 0, 0, 150))
    canvas.alpha_composite(sh.filter(ImageFilter.GaussianBlur(28)))
    canvas.alpha_composite(dv, (x, y0))
    # 책상에 비친 모습
    ref = dv.transpose(Image.FLIP_TOP_BOTTOM).crop((0, 0, dw, 120))
    mask = Image.linear_gradient("L").resize((dw, 120)).point(lambda v: max(0, 70 - v * 70 // 255))
    ref.putalpha(Image.composite(ref.getchannel("A"), Image.new("L", ref.size, 0), mask).point(lambda v: v // 4))
    canvas.alpha_composite(ref, (x, y0 + dh + 4))

d = ImageDraw.Draw(canvas)
title = "SmallTV 서버 모니터"
sub = "작은 날씨 시계를 Wi-Fi 서버 상태판으로 — Proxmox · Docker · GPU · 날씨"
ft, fs = font(64, True), font(30)
d.text(((W - d.textlength(title, font=ft)) // 2, 52), title, font=ft, fill=(255, 255, 255))
d.text(((W - d.textlength(sub, font=fs)) // 2, 132), sub, font=fs, fill=(170, 186, 210))
canvas.convert("RGB").save(OUT, optimize=True)
print(OUT, canvas.size)
