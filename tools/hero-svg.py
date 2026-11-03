#!/usr/bin/env python3
"""The site's hero image (docs/public/hero.svg): the main menu's ribbon of letters, a line of text
running along a curve that winds through space, the near letters large and bright, the far ones small
and faint. Our own drawing, after the look of the game's menu; nothing from the game is used.

Each letter is a <text> moved along the projected curve by SMIL (animateMotion), with its size and
opacity following the depth (animateTransform, animate), every letter the same animation started a
little later, so the line flows round the closed curve. SMIL runs inside an <img>, which a CSS
animation of offset-path does not everywhere.

    python3 tools/hero-svg.py [out.svg]

Also writes <out>-still.svg, the letters where they start, for prefers-reduced-motion
(docs/.vitepress/theme/cmr2.css swaps it in).
"""
import math
import sys

W, H = 640, 480
TEXT = "opencmr2 · colin mcrae rally 2.0 · reimplemented · "
DUR = 60.0          # seconds for a letter to go once round
SAMPLES = 360
SPACING = 12.0      # px between letters through space (at the screen's scale)


def curve(t):
    """A closed curve through space: one wide loop with a smaller one riding on it, rising and
    falling, as the menu's ribbon curls across the screen."""
    a = 2 * math.pi * t
    x = 1.00 * math.sin(a) + 0.30 * math.sin(3 * a + 0.6)
    y = 0.55 * math.sin(2 * a + 0.9) + 0.12 * math.cos(5 * a)
    z = 0.85 * math.cos(a) + 0.35 * math.cos(3 * a + 0.6)
    return x, y, z


def project(p):
    x, y, z = p
    f = 2.6
    s = f / (f + z)                  # nearer (z < 0) is larger
    return W / 2 + x * 225 * s, H / 2 - y * 225 * s + 10, s


space = [curve(i / SAMPLES) for i in range(SAMPLES + 1)]
pts = [project(p) for p in space]
lens, lens3 = [0.0], [0.0]
for (x0, y0, _), (x1, y1, _) in zip(pts, pts[1:]):
    lens.append(lens[-1] + math.hypot(x1 - x0, y1 - y0))
for a, b in zip(space, space[1:]):
    lens3.append(lens3[-1] + 225 * math.dist(a, b))
total, total3 = lens[-1], lens3[-1]
smin = min(p[2] for p in pts)
smax = max(p[2] for p in pts)

# key times: every few samples, by the distance through space (an even pace there, so the near letters
# spread apart on the screen as they grow); the motion's key points by the distance on the screen
step = 10
idx = list(range(0, SAMPLES + 1, step))
if idx[-1] != SAMPLES:
    idx.append(SAMPLES)
key_times = ";".join(f"{lens3[i] / total3:.4f}" for i in idx)
key_points = ";".join(f"{lens[i] / total:.4f}" for i in idx)
scales = ";".join(f"{pts[i][2]:.3f}" for i in idx)
ops = ";".join(f"{0.28 + 0.72 * (pts[i][2] - smin) / (smax - smin):.2f}" for i in idx)
d = "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y, _ in pts) + " Z"

n = int(total3 / SPACING)
chars = (TEXT * (n // len(TEXT) + 1))[:n]

out = []
out.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
           'role="img" aria-label="opencmr2: colin mcrae rally 2.0 reimplemented, the letters running '
           'round a curve as in the game\'s main menu">')
out.append('<defs><path id="r" d="' + d + '"/></defs>')
out.append('<g font-family="Helvetica Neue, Helvetica, Arial, sans-serif" font-size="17" fill="#fff" '
           'text-anchor="middle">')
for i, c in enumerate(chars):
    if c == " ":
        continue
    begin = f"{-DUR * i / n:.3f}s"
    ch = {"&": "&amp;", "<": "&lt;"}.get(c, c)
    out.append(
        f'<g><animateMotion dur="{DUR}s" begin="{begin}" repeatCount="indefinite" calcMode="linear" '
        f'keyTimes="{key_times}" keyPoints="{key_points}">'
        f'<mpath href="#r"/></animateMotion>'
        f'<text dy="7" opacity="0.5">{ch}'
        f'<animate attributeName="opacity" dur="{DUR}s" begin="{begin}" repeatCount="indefinite" '
        f'keyTimes="{key_times}" values="{ops}"/></text>'
        f'<animateTransform attributeName="transform" type="scale" additive="sum" dur="{DUR}s" '
        f'begin="{begin}" repeatCount="indefinite" keyTimes="{key_times}" values="{scales}"/></g>')
out.append('</g></svg>')

path = sys.argv[1] if len(sys.argv) > 1 else "docs/public/hero.svg"
with open(path, "w") as f:
    f.write("\n".join(out) + "\n")
print(f"{path}: {n} letters, path {total:.0f} px, {sum(len(s) for s in out) // 1024} KB")


def at(u):
    """The screen place, scale and opacity a distance u (0-1 through space) round the curve."""
    k = next(j for j in range(1, len(lens3)) if lens3[j] >= u * total3)
    f = (u * total3 - lens3[k - 1]) / max(lens3[k] - lens3[k - 1], 1e-9)
    x, y, sc = (pts[k - 1][q] + (pts[k][q] - pts[k - 1][q]) * f for q in range(3))
    return x, y, sc, 0.28 + 0.72 * (sc - smin) / (smax - smin)


still = [out[0], '<g font-family="Helvetica Neue, Helvetica, Arial, sans-serif" font-size="17" fill="#fff" '
         'text-anchor="middle">']
for i, c in enumerate(chars):
    if c == " ":
        continue
    x, y, sc, op = at((i / n) % 1.0)   # where letter i is at time 0 (its begin is -i/n of a round)
    ch = {"&": "&amp;", "<": "&lt;"}.get(c, c)
    still.append(f'<text transform="translate({x:.1f} {y:.1f}) scale({sc:.3f})" dy="7" opacity="{op:.2f}">{ch}</text>')
still.append('</g></svg>')
spath = path[:-4] + "-still.svg"
with open(spath, "w") as f:
    f.write("\n".join(still) + "\n")
print(f"{spath}: the still")
