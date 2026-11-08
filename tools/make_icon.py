"""生成阅读器图标：圆角方块 + 摊开的书 + 书签丝带。

用法: python tools/make_icon.py
输出: res/reader.ico (多尺寸)、build/icon_preview.png、build/icon_small.png
主图按 2048 超采样后降采样，书页用贝塞尔路径（外角做圆角），保证小尺寸也干净。
"""
from PIL import Image, ImageDraw, ImageFilter

M = 2048
OUT_ICO = "res/reader.ico"
OUT_PREVIEW = "build/icon_preview.png"
OUT_SMALL = "build/icon_small.png"
SIZES = [256, 128, 96, 64, 48, 40, 32, 24, 20, 16]

TILE_TOP = (140, 94, 41)
TILE_BOT = (72, 45, 19)
PAGE_L = (238, 228, 208)
PAGE_R = (249, 244, 232)
INK = (196, 176, 150)
RIBBON = (196, 84, 56)
RIBBON_DK = (152, 58, 38)

# 书页几何（单位坐标，左页；右页镜像）
SPINE_X = 0.494
SPINE_TOP = 0.228
SPINE_BOT = 0.786
OUTER_X = 0.175
OUTER_TOP = 0.268
OUTER_BOT = 0.748
CORNER = 0.034
SAG_TOP = 0.008
SAG_BOT = 0.010
BULGE = 0.016

RIB_X0, RIB_X1 = 0.4645, 0.5355
RIB_TOP, RIB_BOT, RIB_NOTCH = 0.186, 0.858, 0.058

LINE_ROWS = [(0.374, 0.245), (0.452, 0.245), (0.530, 0.245), (0.608, 0.172)]
LINE_X0 = 0.208
LINE_W = 0.028


def cbez(p0, p1, p2, p3, n=40):
    pts = []
    for i in range(n + 1):
        t = i / n
        m = 1.0 - t
        a, b, c, d = m * m * m, 3 * m * m * t, 3 * m * t * t, t * t * t
        pts.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
                    a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))
    return pts


def seg_along(p, q, f, dy=0.0):
    return (p[0] + (q[0] - p[0]) * f, p[1] + (q[1] - p[1]) * f + dy)


def page_poly(mirror):
    """左页轮廓：上边 -> 外上圆角 -> 外沿 -> 外下圆角 -> 下边 -> 书脊(直边)。"""
    sx, ox = SPINE_X, OUTER_X
    st, sb = SPINE_TOP, SPINE_BOT
    ot, ob = OUTER_TOP, OUTER_BOT
    r = CORNER

    # 上边与书脊的夹角方向（书脊 -> 外上角）
    dx, dy = ox - sx, ot - st
    ln = (dx * dx + dy * dy) ** 0.5
    ux, uy = dx / ln, dy / ln                       # 指向外上角
    t_end = (ox - ux * r, ot - uy * r)              # 上边结束点（圆角起点）
    o_start = (ox, ot + r)                          # 外沿起点（圆角终点）

    pts = cbez((sx, st), seg_along((sx, st), t_end, 1 / 3, SAG_TOP),
               seg_along((sx, st), t_end, 2 / 3, SAG_TOP), t_end)
    pts += cbez(t_end, (ox, ot), (ox, ot), o_start)          # 外上圆角
    pts += cbez(o_start, (ox - BULGE, o_start[1] + (ob - ot - 2 * r) / 3),
                (ox - BULGE, ob - r - (ob - ot - 2 * r) / 3), (ox, ob - r))
    pts += cbez((ox, ob - r), (ox, ob), (ox, ob), (ox - ux * r, ob - uy * r))   # 外下圆角
    pts += cbez((ox - ux * r, ob - uy * r), seg_along((ox - ux * r, ob - uy * r), (sx, sb), 1 / 3, SAG_BOT),
                seg_along((ox - ux * r, ob - uy * r), (sx, sb), 2 / 3, SAG_BOT), (sx, sb))
    pts.append((sx, st))
    if mirror:
        pts = [(1.0 - x, y) for (x, y) in pts]
    return pts


def sc(pts, size):
    return [(x * size, y * size) for (x, y) in pts]


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def gradient(size, top, bot):
    g = Image.new("RGB", (1, size))
    px = g.load()
    for y in range(size):
        t = y / (size - 1)
        px[0, y] = tuple(int(round(top[i] + (bot[i] - top[i]) * t)) for i in range(3))
    return g.resize((size, size), Image.BILINEAR)


def build_master(ink=True, rib_pad=0.0):
    S = M
    tile = gradient(S, TILE_TOP, TILE_BOT).convert("RGBA")
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    base.paste(tile, (0, 0), rounded_mask(S, int(0.2237 * S)))

    hl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(hl).rounded_rectangle([0.0045 * S, 0.0045 * S, 0.9955 * S, 0.9955 * S],
                                        radius=0.2195 * S, outline=(255, 255, 255, 24),
                                        width=max(1, int(0.0032 * S)))
    base = Image.alpha_composite(base, hl)

    left = sc(page_poly(False), S)
    right = sc(page_poly(True), S)

    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    for poly in (left, right):
        sd.polygon([(x + 0.005 * S, y + 0.014 * S) for (x, y) in poly], fill=(26, 15, 4, 120))
    base = Image.alpha_composite(base, shadow.filter(ImageFilter.GaussianBlur(0.018 * S)))

    pages = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pd = ImageDraw.Draw(pages)
    pd.polygon(left, fill=PAGE_L + (255,))
    pd.polygon(right, fill=PAGE_R + (255,))
    if ink:
        lw = LINE_W * S
        for (y, w) in LINE_ROWS:
            y0, y1 = y * S - lw / 2, y * S + lw / 2
            pd.rounded_rectangle([LINE_X0 * S, y0, (LINE_X0 + w) * S, y1], radius=lw / 2, fill=INK + (255,))
            pd.rounded_rectangle([(1 - LINE_X0 - w) * S, y0, (1 - LINE_X0) * S, y1], radius=lw / 2, fill=INK + (255,))
    base = Image.alpha_composite(base, pages)

    x0, x1 = (RIB_X0 - rib_pad) * S, (RIB_X1 + rib_pad) * S
    rib = [(x0, RIB_TOP * S), (x1, RIB_TOP * S), (x1, RIB_BOT * S),
           (0.5 * S, (RIB_BOT - RIB_NOTCH) * S), (x0, RIB_BOT * S)]
    ribbon = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    rd = ImageDraw.Draw(ribbon)
    rd.polygon([(x + 0.004 * S, y + 0.009 * S) for (x, y) in rib], fill=(26, 14, 4, 115))
    rd.polygon(rib, fill=RIBBON + (255,))
    rd.polygon([(x0, RIB_TOP * S), (x0 + (x1 - x0) * 0.28, RIB_TOP * S),
                (x0 + (x1 - x0) * 0.28, (RIB_BOT - 0.012) * S), (x0, RIB_BOT * S)],
               fill=RIBBON_DK + (255,))
    base = Image.alpha_composite(base, ribbon)
    return base


def render(master, size):
    return master.resize((size, size), Image.LANCZOS).convert("RGBA")


def main():
    master = build_master()
    # 16/20/24 上去掉正文线、丝带加宽一点，否则缩下去只是一团糊
    tiny = build_master(ink=False, rib_pad=0.011)
    master.resize((512, 512), Image.LANCZOS).save(OUT_PREVIEW)

    imgs = [render(tiny if s <= 24 else master, s) for s in SIZES]
    imgs[0].save(OUT_ICO, format="ICO", sizes=[(s, s) for s in SIZES], append_images=imgs[1:])

    strip = [16, 20, 24, 32, 48, 64]
    k, pad = 6, 12
    w = sum(s * k + pad for s in strip) + pad
    h = max(strip) * k + pad * 2
    sheet = Image.new("RGB", (w, h), (245, 245, 245))
    x = pad
    for s in strip:
        ic = render(tiny if s <= 24 else master, s)
        big = ic.resize((s * k, s * k), Image.NEAREST)
        sheet.paste(big, (x, pad + (max(strip) - s) * k), big)
        x += s * k + pad
    sheet.save(OUT_SMALL)
    print("wrote", OUT_ICO, "sizes", SIZES)
    print("wrote", OUT_PREVIEW, "and", OUT_SMALL)


if __name__ == "__main__":
    main()
