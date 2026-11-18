"""Generate installer art for Chromatic Sampler + Drag Sampler.
mark.png = brand/logo-mark.svg without its bg rect, rendered via `npx @resvg/resvg-js-cli --fit-width 1024`.
Run: python installer/art/make_art.py  (from plugin/)"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
PLUGIN = HERE.parents[1]
DRAG = PLUGIN.parents[1] / "DragSampler" / "installer" / "art"
INK, GREY, BLACK = (244, 244, 242), (120, 120, 118), (0, 0, 0)
MARK = Image.open(HERE / "mark.png").convert("RGBA")

def font(px, bold=False):
    for f in (["segoeuib.ttf", "arialbd.ttf"] if bold else ["segoeui.ttf", "arial.ttf"]):
        try: return ImageFont.truetype(f, px)
        except OSError: pass
    return ImageFont.load_default()

def mark(size, tint=None, alpha=1.0):
    m = MARK.resize((size, size), Image.LANCZOS)
    if tint:  # recolour whole mark (used for mac backgrounds)
        a = m.getchannel("A").point(lambda v: int(v * alpha))
        m = Image.new("RGBA", m.size, tint + (0,)); m.putalpha(a)
    return m

def banner(name, s):  # WizardImageFile 164x314 @ scale s
    w, h = 164 * s, 314 * s
    im = Image.new("RGB", (w, h), BLACK)
    m = mark(120 * s); im.paste(m, ((w - m.width) // 2, 70 * s), m)
    d = ImageDraw.Draw(im)
    y = 210 * s
    for line in name.split():
        d.text((w // 2, y), line.upper(), font=font(15 * s, True), fill=INK, anchor="mt"); y += 20 * s
    d.text((w // 2, h - 22 * s), "SELEVEN AUDIO", font=font(9 * s), fill=GREY, anchor="mt")
    return im

def small(s):  # WizardSmallImageFile 55x55 @ scale s
    im = Image.new("RGB", (55 * s, 55 * s), BLACK)
    m = mark(51 * s); im.paste(m, (2 * s, 2 * s), m)
    return im

def mac_bg(path, tint, alpha):  # bottom-left watermark, Installer.app sidebar is ~170px wide
    im = Image.new("RGBA", (260, 260), (0, 0, 0, 0))
    m = mark(220, tint, alpha); im.paste(m, (-30, 60), m)
    im.save(path)

for name, out, mac in (("Chromatic Sampler", HERE, PLUGIN / "installer/mac/chromatic"),
                       ("Drag Sampler", DRAG, PLUGIN / "installer/mac/drag")):
    out.mkdir(parents=True, exist_ok=True); mac.mkdir(parents=True, exist_ok=True)
    for s in (1, 2):
        sfx = "" if s == 1 else "@2x"
        banner(name, s).save(out / f"wizard{sfx}.bmp")
        small(s).save(out / f"wizard-small{sfx}.bmp")
    mac_bg(mac / "background.png", (0, 0, 0), 0.10)
    mac_bg(mac / "background-dark.png", INK, 0.10)
    assert Image.open(out / "wizard@2x.bmp").size == (328, 628)

# setup.ico = full logo-mark.svg (rounded dark tile + S + star), rendered per size so small sizes stay crisp
import io, resvg_py
SVG = (PLUGIN.parents[1] / "seleven-audio-site" / "brand" / "logo-mark.svg").read_text()
sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
imgs = [Image.open(io.BytesIO(resvg_py.svg_to_bytes(svg_string=SVG, width=z, height=z))).convert("RGBA") for z in sizes]
for out in (HERE, DRAG):
    imgs[-1].save(out / "setup.ico", sizes=[(z, z) for z in sizes], append_images=imgs[:-1])
    assert len(Image.open(out / "setup.ico").ico.sizes()) == len(sizes)
print("ok")
