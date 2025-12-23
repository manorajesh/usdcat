import curses
import math
import time

RAMP = " .`^\",:;Il!i~+_-?][}{1)(|\\/*tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"

# --------- small vector/matrix helpers (no numpy) ----------


def dot(a, b): return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
def sub(a, b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def add(a, b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def mul(a, s): return (a[0]*s, a[1]*s, a[2]*s)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def norm(a):
    l = math.sqrt(dot(a, a)) + 1e-9
    return (a[0]/l, a[1]/l, a[2]/l)


def look_at(eye, target, up=(0, 1, 0)):
    f = norm(sub(target, eye))
    r = norm(cross(f, up))
    u = cross(r, f)
    # view-space coords: x=dot(p-eye, r), y=dot(p-eye, u), z=dot(p-eye, -f)
    return r, u, mul(f, -1.0), eye


def project(p_view, fov_y, aspect):
    # p_view: (x,y,z) where z is negative in front of camera (by our look_at)
    x, y, z = p_view
    if z >= -1e-6:
        return None
    f = 1.0 / math.tan(fov_y * 0.5)
    ndc_x = (x * f / aspect) / (-z)
    ndc_y = (y * f) / (-z)
    return ndc_x, ndc_y, -z  # return positive depth


def to_screen(ndc_x, ndc_y, w, h):
    sx = int((ndc_x * 0.5 + 0.5) * (w - 1))
    sy = int(((-ndc_y) * 0.5 + 0.5) * (h - 1))
    return sx, sy


def barycentric(px, py, ax, ay, bx, by, cx, cy):
    v0x, v0y = bx-ax, by-ay
    v1x, v1y = cx-ax, cy-ay
    v2x, v2y = px-ax, py-ay
    den = v0x*v1y - v1x*v0y
    if abs(den) < 1e-9:
        return None
    inv = 1.0 / den
    v = (v2x*v1y - v1x*v2y) * inv
    w = (v0x*v2y - v2x*v0y) * inv
    u = 1.0 - v - w
    return u, v, w


# --------- cube geometry (8 verts, 12 tris) ----------
V = [
    (-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1),
    (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1),
]
TRIS = [
    (0, 1, 2), (0, 2, 3),  # back
    (4, 6, 5), (4, 7, 6),  # front
    (0, 4, 5), (0, 5, 1),  # bottom
    (3, 2, 6), (3, 6, 7),  # top
    (0, 3, 7), (0, 7, 4),  # left
    (1, 5, 6), (1, 6, 2),  # right
]


def orbit_camera(yaw, pitch, radius, target=(0, 0, 0)):
    # yaw around +Y, pitch around +X (clamped)
    pitch = max(-1.2, min(1.2, pitch))
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    # spherical-ish
    x = radius * sy * cp
    y = radius * sp
    z = radius * cy * cp
    eye = add(target, (x, y, z))
    return eye, target


def render_frame(w, h, yaw, pitch, radius):
    # buffers
    zbuf = [1e9] * (w*h)
    cbuf = [' '] * (w*h)

    eye, target = orbit_camera(yaw, pitch, radius)
    r, u, fneg, eye_ref = look_at(eye, target)
    aspect = w / max(1, h)
    fov = math.radians(60)

    light_dir = norm((0.4, 0.8, 0.2))  # world light

    def world_to_view(p):
        pe = sub(p, eye_ref)
        return (dot(pe, r), dot(pe, u), dot(pe, fneg))

    for i0, i1, i2 in TRIS:
        p0, p1, p2 = V[i0], V[i1], V[i2]

        # face normal in world for Lambert
        n = norm(cross(sub(p1, p0), sub(p2, p0)))
        lambert = max(0.0, dot(n, light_dir))

        # simple backface cull in view space
        v0, v1, v2 = world_to_view(p0), world_to_view(p1), world_to_view(p2)
        # compute signed area in screen-ish space later; cheap view-space cull:
        # if normal points away from camera, skip
        # camera looks down -Z in view space, so facing camera means normal has negative z in view
        n_view = cross(sub(v1, v0), sub(v2, v0))
        if n_view[2] >= 0:
            continue

        q0 = project(v0, fov, aspect)
        q1 = project(v1, fov, aspect)
        q2 = project(v2, fov, aspect)
        if not q0 or not q1 or not q2:
            continue

        x0, y0, d0 = q0
        x1, y1, d1 = q1
        x2, y2, d2 = q2

        sx0, sy0 = to_screen(x0, y0, w, h)
        sx1, sy1 = to_screen(x1, y1, w, h)
        sx2, sy2 = to_screen(x2, y2, w, h)

        minx = max(0, min(sx0, sx1, sx2))
        maxx = min(w-1, max(sx0, sx1, sx2))
        miny = max(0, min(sy0, sy1, sy2))
        maxy = min(h-1, max(sy0, sy1, sy2))

        # choose char by brightness
        idx = int(lambert * (len(RAMP)-1) + 0.5)
        ch = RAMP[max(0, min(len(RAMP)-1, idx))]

        for py in range(miny, maxy+1):
            for px in range(minx, maxx+1):
                bc = barycentric(px+0.5, py+0.5, sx0, sy0, sx1, sy1, sx2, sy2)
                if not bc:
                    continue
                a, b, c = bc
                if a < 0 or b < 0 or c < 0:
                    continue
                depth = a*d0 + b*d1 + c*d2
                k = py*w + px
                if depth < zbuf[k]:
                    zbuf[k] = depth
                    cbuf[k] = ch

    # turn into lines
    lines = []
    for y in range(h):
        lines.append(''.join(cbuf[y*w:(y+1)*w]))
    return lines


def main(stdscr):
    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.keypad(True)

    yaw, pitch, radius = 0.0, 0.2, 4.0

    last = time.time()
    while True:
        h, w = stdscr.getmaxyx()
        # keep some margin
        h = max(10, h-1)
        w = max(20, w)

        # input
        k = stdscr.getch()
        if k == ord('q'):
            break
        elif k == curses.KEY_LEFT:
            yaw -= 0.10
        elif k == curses.KEY_RIGHT:
            yaw += 0.10
        elif k == curses.KEY_UP:
            pitch += 0.08
        elif k == curses.KEY_DOWN:
            pitch -= 0.08
        elif k == ord('w'):
            radius = max(2.0, radius - 0.3)
        elif k == ord('s'):
            radius = min(20.0, radius + 0.3)

        # render
        lines = render_frame(w, h, yaw, pitch, radius)

        stdscr.erase()
        for i, line in enumerate(lines):
            stdscr.addstr(i, 0, line[:w-1])
        stdscr.addstr(h, 0, "Arrows: orbit | w/s: zoom | q: quit")
        stdscr.refresh()

        # small sleep to reduce CPU
        now = time.time()
        dt = now - last
        last = now
        time.sleep(0.01)


if __name__ == "__main__":
    curses.wrapper(main)
