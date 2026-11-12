#!/usr/bin/env python3
"""Generate pre-computed .nav waypoint & APSP routing files for all 8 battle arenas.

Reads AppleDouble resource forks from Data/Terrain/Battle_*.ter.rsrc and writes:
  Data/Terrain/Battle_*.nav

Optionally generates PNG visual overlays via --preview <output_dir>.
Uses only the Python standard library.
"""

import argparse
import glob
import heapq
import math
import os
import struct
import zlib
from collections import deque

TERRAIN_POLYGON_SIZE = 800.0
MAP2UNIT_VALUE = 50.0
CELL_SIZE = 400.0

ITEM_START = 0
ITEM_SIGN = 3
ITEM_TREE = 4
ITEM_POW = 5
ITEM_FLAGPOLE = 9
ITEM_POW_STICKY = 12
ITEM_POW_SUSPENSION = 13
ITEM_PILLAR = 20
ITEM_TEAM_TORCH = 26
ITEM_TEAM_BASE = 27
ITEM_POW_INVIS = 29
ITEM_ROCK = 30
ITEM_AZTEC_HEAD = 34
ITEM_STONEHENGE = 45
ITEM_COLISEUM = 46
ITEM_TAR_PATCH = 60
ITEM_DRUID = 63

POW_ITEMS = {ITEM_POW, ITEM_POW_STICKY, ITEM_POW_SUSPENSION, ITEM_POW_INVIS}

BN_CELL_WALKABLE = 1 << 0
BN_CELL_HAZARD_TAR = 1 << 1
BN_CELL_STEEP = 1 << 2
BN_CELL_BLOCKED = 1 << 3

BN_NODE_SPAWN = 1 << 0
BN_NODE_BASE = 1 << 1
BN_NODE_TORCH = 1 << 2
BN_NODE_PICKUP = 1 << 3
BN_NODE_HUB = 1 << 4
BN_NODE_DEADEND = 1 << 5

MAGIC = b"CMNV"
VERSION = 1

# Tuned physics/geometry parameters matching Player_Car.c and Items.c
MAX_STEP_SLOPE = 0.85          # max |dy| / dist across walkable cell transitions (climbs Aztec/Celtic/Spiral ramps)
FENCE_MARGIN = 150.0           # minimum clearance from fence segments
TAR_RADIUS = 1100.0            # hazard penalty zone around scale-1.0 tar patches (scaled by 1 + p0*0.5)

OBSTACLE_RADII = {
    ITEM_SIGN: 120.0,
    ITEM_AZTEC_HEAD: 360.0,
    ITEM_ROCK: 340.0,
    ITEM_PILLAR: 320.0,
    ITEM_FLAGPOLE: 140.0,
    ITEM_DRUID: 180.0,
}
HENGE_ARCH_HALF_SPAN = 1300.0
HENGE_PYLON_CORE_RADIUS = 880.0

# Waypoint graph sampling & connectivity limits
ANCHOR_MERGE_DIST = 950.0
SAMPLE_MIN_SPACING = 2200.0
MAX_EDGE_DIST = 4400.0
MIN_DIRECT_LOS_MARGIN = 160.0


def parse_appledouble_rsrc(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] == b"\x00\x05\x16\x07":
        num_entries = struct.unpack_from(">H", data, 24)[0]
        for i in range(num_entries):
            eid, off, ln = struct.unpack_from(">III", data, 26 + i * 12)
            if eid == 2:
                data = data[off:off + ln]
                break
    data_off, map_off, _data_len, map_len = struct.unpack_from(">IIII", data, 0)
    rmap = data[map_off:map_off + map_len]
    tlist_off = struct.unpack_from(">H", rmap, 24)[0]
    num_types = struct.unpack_from(">H", rmap, tlist_off)[0] + 1
    res = {}
    pos = tlist_off + 2
    for _ in range(num_types):
        rtype = rmap[pos:pos + 4].decode("latin1")
        cnt_m1, ref_off = struct.unpack_from(">HH", rmap, pos + 4)
        pos += 8
        rpos = tlist_off + ref_off
        res[rtype] = {}
        for __ in range(cnt_m1 + 1):
            rid, _noff, attrs = struct.unpack_from(">HhI", rmap, rpos)
            doff = attrs & 0x00FFFFFF
            rpos += 12
            rlen = struct.unpack_from(">I", data, data_off + doff)[0]
            res[rtype][rid] = data[data_off + doff + 4:data_off + doff + 4 + rlen]
    return res


def point_seg_dist_sq(px, pz, x1, z1, x2, z2):
    dx = x2 - x1
    dz = z2 - z1
    len_sq = dx * dx + dz * dz
    if len_sq <= 1e-6:
        return (px - x1) ** 2 + (pz - z1) ** 2
    t = ((px - x1) * dx + (pz - z1) * dz) / len_sq
    if t < 0.0:
        t = 0.0
    elif t > 1.0:
        t = 1.0
    cx = x1 + t * dx
    cz = z1 + t * dz
    return (px - cx) ** 2 + (pz - cz) ** 2


def segs_intersect(ax, az, bx, bz, cx, cz, dx, dz):
    def orient(px, pz, qx, qz, rx, rz):
        return (qx - px) * (rz - pz) - (qz - pz) * (rx - px)

    # Fast bounding box reject
    if max(ax, bx) < min(cx, dx) or min(ax, bx) > max(cx, dx):
        return False
    if max(az, bz) < min(cz, dz) or min(az, bz) > max(cz, dz):
        return False

    o1 = orient(ax, az, bx, bz, cx, cz)
    o2 = orient(ax, az, bx, bz, dx, dz)
    o3 = orient(cx, cz, dx, dz, ax, az)
    o4 = orient(cx, cz, dx, dz, bx, bz)

    if ((o1 > 0 and o2 < 0) or (o1 < 0 and o2 > 0)) and ((o3 > 0 and o4 < 0) or (o3 < 0 and o4 > 0)):
        return True
    if o1 == 0 and min(ax, bx) <= cx <= max(ax, bx) and min(az, bz) <= cz <= max(az, bz):
        return True
    if o2 == 0 and min(ax, bx) <= dx <= max(ax, bx) and min(az, bz) <= dz <= max(az, bz):
        return True
    if o3 == 0 and min(cx, dx) <= ax <= max(cx, dx) and min(cz, dz) <= az <= max(cz, dz):
        return True
    if o4 == 0 and min(cx, dx) <= bx <= max(cx, dx) and min(cz, dz) <= bz <= max(cz, dz):
        return True
    return False


class ArenaMap:
    def __init__(self, rsrc_path):
        self.name = os.path.basename(rsrc_path).replace(".ter.rsrc", "")
        r = parse_appledouble_rsrc(rsrc_path)
        hdr = struct.unpack_from(">4b5i3f5i", r["Hedr"][1000])
        self.map_w = hdr[5]
        self.map_h = hdr[6]
        self.tile_size = hdr[9]
        self.yscale = TERRAIN_POLYGON_SIZE / self.tile_size

        self.grid_w = self.map_w * 2
        self.grid_h = self.map_h * 2
        self.world_w = self.map_w * TERRAIN_POLYGON_SIZE
        self.world_h = self.map_h * TERRAIN_POLYGON_SIZE

        num_verts = (self.map_w + 1) * (self.map_h + 1)
        raw_y = struct.unpack_from(f">{num_verts}f", r["YCrd"][1000])
        self.ygrid = [v * self.yscale for v in raw_y]

        # Parse tile layer & split mode
        self.split_forward = [False] * (self.map_w * self.map_h)
        if "Layr" in r and 1000 in r["Layr"] and "Atrb" in r and 1000 in r["Atrb"]:
            layr = struct.unpack_from(f">{self.map_w * self.map_h}H", r["Layr"][1000])
            atrb_blob = r["Atrb"][1000]
            num_att = len(atrb_blob) // 4
            att_flags = [struct.unpack_from(">Hbb", atrb_blob, i * 4)[0] for i in range(num_att)]
            # Terrain.c: BIT 0 or flag split; check CalculateSplitModeMatrix convention (approx symmetric anyway)
            for idx, lid in enumerate(layr):
                if lid < num_att:
                    self.split_forward[idx] = bool(att_flags[lid] & 1)

        # Parse fences in world units (* 50)
        self.fences = []
        if "Fenc" in r and 1000 in r["Fenc"] and "FnNb" in r:
            fenc_blob = r["Fenc"][1000]
            num_fences = len(fenc_blob) // 16
            for fi in range(num_fences):
                rid = 1000 + fi
                if rid not in r["FnNb"]:
                    continue
                nb_blob = r["FnNb"][rid]
                pts = [struct.unpack_from(">ii", nb_blob, j * 8) for j in range(len(nb_blob) // 8)]
                wpts = [(px * MAP2UNIT_VALUE, pz * MAP2UNIT_VALUE) for px, pz in pts]
                for j in range(len(wpts) - 1):
                    self.fences.append((wpts[j][0], wpts[j][1], wpts[j + 1][0], wpts[j + 1][1]))

        # Parse items
        self.starts = []
        self.bases = {}
        self.torches = []
        self.pickups = []
        self.obstacles = []  # (x, z, radius)
        self.tar_patches = []

        itms_blob = r["Itms"][1000]
        num_items = len(itms_blob) // 16
        for k in range(num_items):
            ix, iz, typ, p0, p1, p2, p3, _fl = struct.unpack_from(">IIH4bH", itms_blob, k * 16)
            wx = ix * MAP2UNIT_VALUE
            wz = iz * MAP2UNIT_VALUE
            wy = self.get_y(wx, wz)
            if typ == ITEM_START:
                self.starts.append((wx, wy, wz, p0, p1))
            elif typ == ITEM_TEAM_BASE:
                self.bases[int(p0)] = (wx, wy, wz)
            elif typ == ITEM_TEAM_TORCH:
                self.torches.append((wx, wy, wz, int(p0)))
            elif typ in POW_ITEMS:
                if int(p1) == 0:
                    subtype = int(p0) if typ == ITEM_POW else int(typ)
                    self.pickups.append((wx, wz, typ, subtype))
            elif typ == ITEM_TAR_PATCH:
                tar_scale = 1.0 + max(0, int(p0)) * 0.5
                self.tar_patches.append((wx, wz, TAR_RADIUS * tar_scale))
            elif typ == ITEM_TREE:
                if int(p3) & 1:
                    self.obstacles.append((wx, wz, 135.0))
            elif typ in OBSTACLE_RADII:
                self.obstacles.append((wx, wz, OBSTACLE_RADII[typ]))
            elif typ == ITEM_STONEHENGE:
                rot = 2.0 * math.pi * (float(p1) / 64.0)
                if p0 == 1:
                    # Two pylons separated by +-1300 along local X (rotated by -rot around Y)
                    c = math.cos(-rot)
                    s = math.sin(-rot)
                    dx = -HENGE_ARCH_HALF_SPAN * c
                    dz = -HENGE_ARCH_HALF_SPAN * s
                    self.obstacles.append((wx + dx, wz + dz, HENGE_PYLON_CORE_RADIUS))
                    self.obstacles.append((wx - dx, wz - dz, HENGE_PYLON_CORE_RADIUS))
                elif p0 == 2:
                    self.obstacles.append((wx, wz, HENGE_PYLON_CORE_RADIUS))
                elif p0 == 0:
                    self.obstacles.append((wx, wz, 220.0))

    def get_y_and_ny(self, x, z):
        col = min(self.map_w - 1, max(0, int(x / TERRAIN_POLYGON_SIZE)))
        row = min(self.map_h - 1, max(0, int(z / TERRAIN_POLYGON_SIZE)))
        xi = max(0.0, min(TERRAIN_POLYGON_SIZE, x - col * TERRAIN_POLYGON_SIZE))
        zi = max(0.0, min(TERRAIN_POLYGON_SIZE, z - row * TERRAIN_POLYGON_SIZE))
        stride = self.map_w + 1
        y0 = self.ygrid[row * stride + col]
        y1 = self.ygrid[row * stride + col + 1]
        y2 = self.ygrid[(row + 1) * stride + col + 1]
        y3 = self.ygrid[(row + 1) * stride + col]

        def tri_y_and_ny(ax, ay, az, bx, by, bz, cx, cy, cz):
            ux, uy, uz = bx - ax, by - ay, bz - az
            vx, vy, vz = cx - ax, cy - ay, cz - az
            nx = uy * vz - uz * vy
            ny = uz * vx - ux * vz
            nz = ux * vy - uy * vx
            mag = math.sqrt(nx * nx + ny * ny + nz * nz)
            if mag > 1e-6:
                ny = abs(ny) / mag
            else:
                ny = 1.0
            # Barycentric-free plane solve: n . (P - A) = 0
            denom = (bx - ax) * (cz - az) - (cx - ax) * (bz - az)
            if abs(denom) < 1e-6:
                return ay, ny
            u = ((x - ax) * (cz - az) - (cx - ax) * (z - az)) / denom
            v = ((bx - ax) * (z - az) - (x - ax) * (bz - az)) / denom
            py = ay + u * (by - ay) + v * (cy - ay)
            return py, ny

        px0, pz0 = col * TERRAIN_POLYGON_SIZE, row * TERRAIN_POLYGON_SIZE
        px1, pz2 = px0 + TERRAIN_POLYGON_SIZE, pz0 + TERRAIN_POLYGON_SIZE
        if not self.split_forward[row * self.map_w + col]:
            if xi < zi:
                return tri_y_and_ny(px0, y0, pz0, px1, y2, pz2, px0, y3, pz2)
            else:
                return tri_y_and_ny(px0, y0, pz0, px1, y1, pz0, px1, y2, pz2)
        else:
            if (TERRAIN_POLYGON_SIZE - xi) > zi:
                return tri_y_and_ny(px0, y0, pz0, px1, y1, pz0, px0, y3, pz2)
            else:
                return tri_y_and_ny(px1, y1, pz0, px1, y2, pz2, px0, y3, pz2)

    def get_y(self, x, z):
        return self.get_y_and_ny(x, z)[0]

    def cell_center(self, c, r):
        return (c + 0.5) * CELL_SIZE, (r + 0.5) * CELL_SIZE

    def world_to_cell(self, x, z):
        c = min(self.grid_w - 1, max(0, int(x / CELL_SIZE)))
        r = min(self.grid_h - 1, max(0, int(z / CELL_SIZE)))
        return c, r

    def hits_fence(self, x1, z1, x2, z2):
        for fx1, fz1, fx2, fz2 in self.fences:
            if segs_intersect(x1, z1, x2, z2, fx1, fz1, fx2, fz2):
                return True
        return False

    def min_fence_dist(self, x, z):
        best_sq = 1e18
        for fx1, fz1, fx2, fz2 in self.fences:
            d2 = point_seg_dist_sq(x, z, fx1, fz1, fx2, fz2)
            if d2 < best_sq:
                best_sq = d2
        return math.sqrt(best_sq)

    def inside_obstacle(self, x, z, pad=0.0):
        for ox, oz, rad in self.obstacles:
            r_tot = rad + pad
            if (x - ox) ** 2 + (z - oz) ** 2 < r_tot * r_tot:
                return True
        return False

    def in_tar(self, x, z):
        for tx, tz, trad in self.tar_patches:
            if (x - tx) ** 2 + (z - tz) ** 2 <= trad * trad:
                return True
        return False

    def build_nav(self):
        n_cells = self.grid_w * self.grid_h
        self.cell_y = [0.0] * n_cells
        self.cell_ny = [1.0] * n_cells
        self.cell_fence_dist = [0.0] * n_cells
        self.cell_flags = [0] * n_cells

        for r in range(self.grid_h):
            for c in range(self.grid_w):
                idx = r * self.grid_w + c
                wx, wz = self.cell_center(c, r)
                y, ny = self.get_y_and_ny(wx, wz)
                self.cell_y[idx] = y
                self.cell_ny[idx] = ny
                fd = self.min_fence_dist(wx, wz)
                self.cell_fence_dist[idx] = fd
                flags = 0
                if self.in_tar(wx, wz):
                    flags |= BN_CELL_HAZARD_TAR
                if fd < FENCE_MARGIN or self.inside_obstacle(wx, wz):
                    flags |= BN_CELL_BLOCKED
                if ny < 0.72:
                    flags |= BN_CELL_STEEP
                self.cell_flags[idx] = flags

        # Flood fill walkable region from normal & CTF starts inside fences
        walkable = [False] * n_cells
        q = deque()
        for sx, _sy, sz, _p0, _p1 in self.starts:
            sc, sr = self.world_to_cell(sx, sz)
            sidx = sr * self.grid_w + sc
            # Always seed start cells even if near obstacle edge
            self.cell_flags[sidx] &= ~(BN_CELL_STEEP | BN_CELL_BLOCKED)
            if not walkable[sidx]:
                walkable[sidx] = True
                q.append((sc, sr))

        while q:
            c, r = q.popleft()
            idx = r * self.grid_w + c
            x1, z1 = self.cell_center(c, r)
            y1 = self.cell_y[idx]
            for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nc, nr = c + dc, r + dr
                if not (0 <= nc < self.grid_w and 0 <= nr < self.grid_h):
                    continue
                nidx = nr * self.grid_w + nc
                if walkable[nidx]:
                    continue
                if self.cell_flags[nidx] & (BN_CELL_BLOCKED | BN_CELL_STEEP):
                    continue
                y2 = self.cell_y[nidx]
                if abs(y2 - y1) / CELL_SIZE > MAX_STEP_SLOPE:
                    continue
                x2, z2 = self.cell_center(nc, nr)
                if self.hits_fence(x1, z1, x2, z2):
                    continue
                walkable[nidx] = True
                q.append((nc, nr))

        for i in range(n_cells):
            if walkable[i]:
                self.cell_flags[i] |= BN_CELL_WALKABLE
            elif not (self.cell_flags[i] & (BN_CELL_STEEP | BN_CELL_BLOCKED)):
                self.cell_flags[i] |= BN_CELL_BLOCKED

        # Compute clearance from non-walkable cells (Euclidean BFS approximation)
        clearance = [1e6 if walkable[i] else 0.0 for i in range(n_cells)]
        cq = deque()
        for r in range(self.grid_h):
            for c in range(self.grid_w):
                idx = r * self.grid_w + c
                if not walkable[idx]:
                    cq.append((c, r))
                elif c == 0 or r == 0 or c == self.grid_w - 1 or r == self.grid_h - 1:
                    clearance[idx] = CELL_SIZE * 0.5
                    cq.append((c, r))
        while cq:
            c, r = cq.popleft()
            idx = r * self.grid_w + c
            base_d = clearance[idx]
            for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, 1), (1, -1), (-1, -1)):
                nc, nr = c + dc, r + dr
                if 0 <= nc < self.grid_w and 0 <= nr < self.grid_h:
                    nidx = nr * self.grid_w + nc
                    step = CELL_SIZE * (1.41421356 if (dc and dr) else 1.0)
                    nd = base_d + step
                    if nd < clearance[nidx]:
                        clearance[nidx] = nd
                        cq.append((nc, nr))
        for i in range(n_cells):
            clearance[i] = min(clearance[i], self.cell_fence_dist[i])
        self.clearance = clearance

        def nearest_walkable_cell(x, z):
            sc, sr = self.world_to_cell(x, z)
            if walkable[sr * self.grid_w + sc]:
                return sc, sr
            best = None
            best_d2 = 1e18
            for rad in range(1, 10):
                for dr in range(-rad, rad + 1):
                    for dc in range(-rad, rad + 1):
                        nc, nr = sc + dc, sr + dr
                        if 0 <= nc < self.grid_w and 0 <= nr < self.grid_h:
                            if walkable[nr * self.grid_w + nc]:
                                cx, cz = self.cell_center(nc, nr)
                                if not self.hits_fence(x, z, cx, cz):
                                    d2 = (cx - x) ** 2 + (cz - z) ** 2
                                    if d2 < best_d2:
                                        best_d2 = d2
                                        best = (nc, nr)
                if best is not None:
                    return best
            # Last resort ignore fence
            for r in range(self.grid_h):
                for c in range(self.grid_w):
                    if walkable[r * self.grid_w + c]:
                        cx, cz = self.cell_center(c, r)
                        d2 = (cx - x) ** 2 + (cz - z) ** 2
                        if d2 < best_d2:
                            best_d2 = d2
                            best = (c, r)
            return best

        # Drivable LOS helper between two world points
        def can_drive_direct(x1, z1, x2, z2):
            dist = math.hypot(x2 - x1, z2 - z1)
            if dist < 1.0:
                return True
            if self.hits_fence(x1, z1, x2, z2):
                return False
            ux = (x2 - x1) / dist
            uz = (z2 - z1) / dist
            rx, rz = -uz * 190.0, ux * 190.0
            steps = max(2, int(math.ceil(dist / 150.0)))
            step_d = dist / float(steps)
            prev_y = self.get_y(x1, z1)
            for s in range(1, steps + 1):
                t = s / float(steps)
                px = x1 + (x2 - x1) * t
                pz = z1 + (z2 - z1) * t
                if self.min_fence_dist(px, pz) < MIN_DIRECT_LOS_MARGIN:
                    return False
                if self.inside_obstacle(px, pz, pad=100.0):
                    return False
                py, ny = self.get_y_and_ny(px, pz)
                if ny < 0.72 or abs(py - prev_y) / step_d > MAX_STEP_SLOPE:
                    return False
                prev_y = py
                sc, sr = self.world_to_cell(px, pz)
                if not walkable[sr * self.grid_w + sc]:
                    return False
                # Enforce car-width lateral clearance on interior span
                if 0.15 <= t <= 0.85:
                    for side in (-1.0, 1.0):
                        lx = px + rx * side
                        lz = pz + rz * side
                        lc, lr = self.world_to_cell(lx, lz)
                        if self.cell_flags[lr * self.grid_w + lc] & (BN_CELL_STEEP | BN_CELL_BLOCKED):
                            return False
            return True

        def edge_cost(x1, z1, x2, z2):
            dist = math.hypot(x2 - x1, z2 - z1)
            steps = max(2, int(math.ceil(dist / 200.0)))
            total_w = 0.0
            for s in range(steps + 1):
                t = s / float(steps)
                px = x1 + (x2 - x1) * t
                pz = z1 + (z2 - z1) * t
                sc, sr = self.world_to_cell(px, pz)
                idx = sr * self.grid_w + sc
                w = 1.0
                if self.cell_flags[idx] & BN_CELL_HAZARD_TAR:
                    w = 5.0
                elif clearance[idx] < 500.0:
                    w = 1.35
                total_w += w
            return dist * (total_w / float(steps + 1))

        # Place anchor waypoints (Bases first so indices 0/1 or well-known, then starts, torches, pickups)
        nodes = []  # dicts: x, y, z, clearance, flags

        def add_or_merge_anchor(x, z, flag, force_keep=False, merge_dist=ANCHOR_MERGE_DIST):
            sc, sr = nearest_walkable_cell(x, z)
            wx, wz = self.cell_center(sc, sr)
            # If exact anchor is already safely clear and walkable, keep exact pos for bases
            asc, asr = self.world_to_cell(x, z)
            if walkable[asr * self.grid_w + asc] and (clearance[asr * self.grid_w + asc] >= 250.0 or force_keep):
                wx, wz = x, z
                sc, sr = asc, asr
            idx = sr * self.grid_w + sc
            for ni, n in enumerate(nodes):
                if not force_keep and math.hypot(n["x"] - wx, n["z"] - wz) <= merge_dist:
                    if can_drive_direct(n["x"], n["z"], wx, wz):
                        n["flags"] |= flag
                        return ni
            nodes.append({
                "x": wx,
                "y": self.get_y(wx, wz),
                "z": wz,
                "clearance": clearance[idx],
                "flags": flag,
            })
            return len(nodes) - 1

        base_nodes = [-1, -1]
        for team in (0, 1):
            if team in self.bases:
                bx, _by, bz = self.bases[team]
                base_nodes[team] = add_or_merge_anchor(bx, bz, BN_NODE_BASE, force_keep=True)

        for sx, _sy, sz, _p0, _p1 in self.starts:
            add_or_merge_anchor(sx, sz, BN_NODE_SPAWN, merge_dist=1000.0)
        for tx, _ty, tz, _p0 in self.torches:
            add_or_merge_anchor(tx, tz, BN_NODE_TORCH, merge_dist=1000.0)
        for px, pz, _typ, _sub in self.pickups:
            sc, sr = nearest_walkable_cell(px, pz)
            wx, wz = self.cell_center(sc, sr)
            if not self.hits_fence(px, pz, wx, wz) and math.hypot(px - wx, pz - wz) < 1200.0:
                add_or_merge_anchor(wx, wz, BN_NODE_PICKUP, merge_dist=1500.0)

        # Cover all remaining walkable terrain with clearance-biased Poisson sampling
        # Prefer corridor centers (high clearance) and skip narrow dead-end crevices
        def unwalkable_neighbors8(c, r):
            cnt = 0
            for dc in (-1, 0, 1):
                for dr in (-1, 0, 1):
                    if dc == 0 and dr == 0:
                        continue
                    nc, nr = c + dc, r + dr
                    if not (0 <= nc < self.grid_w and 0 <= nr < self.grid_h) or not walkable[nr * self.grid_w + nc]:
                        cnt += 1
            return cnt

        walk_cells = [
            (r * self.grid_w + c, c, r)
            for r in range(self.grid_h)
            for c in range(self.grid_w)
            if walkable[r * self.grid_w + c]
            and clearance[r * self.grid_w + c] >= 450.0
            and unwalkable_neighbors8(c, r) < 5
        ]
        walk_cells.sort(key=lambda item: (-clearance[item[0]], item[2], item[1]))

        for idx, c, r in walk_cells:
            wx, wz = self.cell_center(c, r)
            # Check if an existing node is already close AND directly drivable without barrier
            covered = False
            for n in nodes:
                d = math.hypot(n["x"] - wx, n["z"] - wz)
                if d < SAMPLE_MIN_SPACING and can_drive_direct(n["x"], n["z"], wx, wz):
                    covered = True
                    break
            if not covered:
                nodes.append({
                    "x": wx,
                    "y": self.cell_y[idx],
                    "z": wz,
                    "clearance": clearance[idx],
                    "flags": 0,
                })

        # Build visibility/drivability edges
        N = len(nodes)
        adj = [[] for _ in range(N)]

        def connect_pair(i, j):
            cost = edge_cost(nodes[i]["x"], nodes[i]["z"], nodes[j]["x"], nodes[j]["z"])
            adj[i].append((j, cost))
            adj[j].append((i, cost))

        edge_set = set()
        for i in range(N):
            for j in range(i + 1, N):
                d = math.hypot(nodes[i]["x"] - nodes[j]["x"], nodes[i]["z"] - nodes[j]["z"])
                if d <= MAX_EDGE_DIST and can_drive_direct(nodes[i]["x"], nodes[i]["z"], nodes[j]["x"], nodes[j]["z"]):
                    edge_set.add((i, j))
                    connect_pair(i, j)

        # Bridge any disconnected walkable components via grid BFS along walkable cells!
        def get_components():
            comp = [-1] * N
            cid = 0
            for start in range(N):
                if comp[start] != -1:
                    continue
                comp[start] = cid
                dq = deque([start])
                while dq:
                    u = dq.popleft()
                    for v, _ in adj[u]:
                        if comp[v] == -1:
                            comp[v] = cid
                            dq.append(v)
                cid += 1
            return comp, cid

        comp, num_comp = get_components()
        while num_comp > 1:
            # Find shortest grid path from component 0 to any other component, insert corridor chain
            src_cells = {}
            pq = []
            best_g = [1e18] * n_cells
            parent_g = [-1] * n_cells
            for i in range(N):
                sc, sr = self.world_to_cell(nodes[i]["x"], nodes[i]["z"])
                cidx = sr * self.grid_w + sc
                if comp[i] == 0:
                    best_g[cidx] = 0.0
                    heapq.heappush(pq, (0.0, cidx))
                else:
                    src_cells.setdefault(cidx, []).append(i)

            hit_cidx = -1
            while pq:
                d, u = heapq.heappop(pq)
                if d > best_g[u]:
                    continue
                if u in src_cells:
                    hit_cidx = u
                    break
                uc = u % self.grid_w
                ur = u // self.grid_w
                x1, z1 = self.cell_center(uc, ur)
                y1 = self.cell_y[u]
                for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, 1), (1, -1), (-1, -1)):
                    nc, nr = uc + dc, ur + dr
                    if not (0 <= nc < self.grid_w and 0 <= nr < self.grid_h):
                        continue
                    v = nr * self.grid_w + nc
                    if not walkable[v]:
                        continue
                    # For diagonals require both cardinal corners walkable
                    if dc != 0 and dr != 0:
                        if not (walkable[ur * self.grid_w + nc] and walkable[nr * self.grid_w + uc]):
                            continue
                    x2, z2 = self.cell_center(nc, nr)
                    step = CELL_SIZE * (1.41421356 if (dc and dr) else 1.0)
                    if abs(self.cell_y[v] - y1) / step > MAX_STEP_SLOPE:
                        continue
                    if self.hits_fence(x1, z1, x2, z2):
                        continue
                    # Strongly prefer high clearance corridors
                    pen = 1.0 + max(0.0, 500.0 - clearance[v]) * 0.004
                    nd = d + step * pen
                    if nd < best_g[v]:
                        best_g[v] = nd
                        parent_g[v] = u
                        heapq.heappush(pq, (nd, v))

            if hit_cidx == -1:
                raise RuntimeError(f"{self.name}: Failed to bridge disconnected components ({num_comp})!")

            # Reconstruct cell path and drop intermediate bridge nodes where LOS breaks
            chain = []
            cur = hit_cidx
            while cur != -1:
                chain.append(cur)
                cur = parent_g[cur]
            chain.reverse()

            # Connect chain endpoint nearest component 0
            start_cell = chain[0]
            sx, sz = self.cell_center(start_cell % self.grid_w, start_cell // self.grid_w)
            best_u = min((i for i in range(N) if comp[i] == 0), key=lambda i: (nodes[i]["x"] - sx) ** 2 + (nodes[i]["z"] - sz) ** 2)
            prev_ni = best_u
            last_added_cell_idx = 0
            for k in range(1, len(chain)):
                cx, cz = self.cell_center(chain[k] % self.grid_w, chain[k] // self.grid_w)
                if not can_drive_direct(nodes[prev_ni]["x"], nodes[prev_ni]["z"], cx, cz) or math.hypot(nodes[prev_ni]["x"] - cx, nodes[prev_ni]["z"] - cz) > 1600.0:
                    mid = chain[max(last_added_cell_idx + 1, k - 1)]
                    mx, mz = self.cell_center(mid % self.grid_w, mid // self.grid_w)
                    new_ni = len(nodes)
                    nodes.append({"x": mx, "y": self.cell_y[mid], "z": mz, "clearance": clearance[mid], "flags": 0})
                    adj.append([])
                    if can_drive_direct(nodes[prev_ni]["x"], nodes[prev_ni]["z"], mx, mz):
                        connect_pair(prev_ni, new_ni)
                    else:
                        # Direct adjacent step guaranteed drivable across walkable grid
                        connect_pair(prev_ni, new_ni)
                    prev_ni = new_ni
                    last_added_cell_idx = max(last_added_cell_idx + 1, k - 1)
            for end_ni in src_cells[hit_cidx]:
                connect_pair(prev_ni, end_ni)
            # Also connect any new bridge nodes to nearby visible nodes
            old_N = N
            N = len(nodes)
            for ni in range(old_N, N):
                for oj in range(N):
                    if ni != oj and math.hypot(nodes[ni]["x"] - nodes[oj]["x"], nodes[ni]["z"] - nodes[oj]["z"]) <= MAX_EDGE_DIST:
                        if can_drive_direct(nodes[ni]["x"], nodes[ni]["z"], nodes[oj]["x"], nodes[oj]["z"]):
                            if all(nb != oj for nb, _ in adj[ni]):
                                connect_pair(ni, oj)
            comp, num_comp = get_components()

        # Deduplicate adjacency keeping min cost before pruning non-anchor dead-end spurs
        for i in range(N):
            best_nb = {}
            for nb, c in adj[i]:
                if nb != i and (nb not in best_nb or c < best_nb[nb]):
                    best_nb[nb] = c
            adj[i] = sorted(best_nb.items())

        # Iteratively prune non-anchor leaf spurs (deg <= 1) without disconnecting anchors
        anchor_mask = BN_NODE_SPAWN | BN_NODE_BASE | BN_NODE_TORCH | BN_NODE_PICKUP
        alive = [True] * N
        prune_changed = True
        while prune_changed:
            prune_changed = False
            for i in range(N):
                if not alive[i] or (nodes[i]["flags"] & anchor_mask):
                    continue
                live_nbs = [(nb, c) for nb, c in adj[i] if alive[nb]]
                if len(live_nbs) <= 1:
                    alive[i] = False
                    prune_changed = True

        if not all(alive):
            old_to_new = [-1] * N
            new_nodes = []
            for i in range(N):
                if alive[i]:
                    old_to_new[i] = len(new_nodes)
                    new_nodes.append(nodes[i])
            new_adj = [[] for _ in range(len(new_nodes))]
            for i in range(N):
                ni = old_to_new[i]
                if ni < 0:
                    continue
                for nb, c in adj[i]:
                    nnb = old_to_new[nb]
                    if nnb >= 0 and nnb != ni:
                        new_adj[ni].append((nnb, c))
            base_nodes = [old_to_new[bn] if bn >= 0 else -1 for bn in base_nodes]
            nodes = new_nodes
            adj = new_adj
            N = len(nodes)

        # Degree / hub / deadend flags
        for i in range(N):
            adj[i].sort()
            deg = len(adj[i])
            nodes[i]["degree"] = deg
            if deg >= 4:
                nodes[i]["flags"] |= BN_NODE_HUB
            elif deg <= 1:
                nodes[i]["flags"] |= BN_NODE_DEADEND

        # Multi-source Dijkstra APSP from each node (fast, exact, gives nextHop & cost)
        next_hop = [-1] * (N * N)
        dist_eighth = [0xFFFF] * (N * N)
        for src in range(N):
            dist = [1e18] * N
            first_hop = [-1] * N
            dist[src] = 0.0
            first_hop[src] = src
            pq = [(0.0, src)]
            while pq:
                d, u = heapq.heappop(pq)
                if d > dist[u]:
                    continue
                for v, w in adj[u]:
                    nd = d + w
                    if nd < dist[v]:
                        dist[v] = nd
                        first_hop[v] = v if u == src else first_hop[u]
                        heapq.heappush(pq, (nd, v))
            for dst in range(N):
                next_hop[src * N + dst] = first_hop[dst]
                if dist[dst] < 1e17:
                    q8 = min(0xFFFE, int(round(dist[dst] / 8.0)))
                    dist_eighth[src * N + dst] = q8

        # Fill cellNode via multi-source Dijkstra from node cells through walkable terrain
        cell_node = [-1] * n_cells
        cell_best = [1e18] * n_cells
        cpq = []
        for ni, n in enumerate(nodes):
            sc, sr = self.world_to_cell(n["x"], n["z"])
            cidx = sr * self.grid_w + sc
            cell_best[cidx] = 0.0
            cell_node[cidx] = ni
            heapq.heappush(cpq, (0.0, cidx, ni))
        while cpq:
            d, u, ni = heapq.heappop(cpq)
            if d > cell_best[u]:
                continue
            uc = u % self.grid_w
            ur = u // self.grid_w
            x1, z1 = self.cell_center(uc, ur)
            y1 = self.cell_y[u]
            for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nc, nr = uc + dc, ur + dr
                if 0 <= nc < self.grid_w and 0 <= nr < self.grid_h:
                    v = nr * self.grid_w + nc
                    if not walkable[v]:
                        continue
                    if abs(self.cell_y[v] - y1) / CELL_SIZE > MAX_STEP_SLOPE:
                        continue
                    x2, z2 = self.cell_center(nc, nr)
                    if self.hits_fence(x1, z1, x2, z2):
                        continue
                    nd = d + CELL_SIZE
                    if nd < cell_best[v]:
                        cell_best[v] = nd
                        cell_node[v] = ni
                        heapq.heappush(cpq, (nd, v, ni))

        # Propagate nearest assigned cell into blocked/wall/exterior cells so every query succeeds
        fq = deque(i for i in range(n_cells) if cell_node[i] >= 0)
        while fq:
            u = fq.popleft()
            uc = u % self.grid_w
            ur = u // self.grid_w
            x1, z1 = self.cell_center(uc, ur)
            for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nc, nr = uc + dc, ur + dr
                if 0 <= nc < self.grid_w and 0 <= nr < self.grid_h:
                    v = nr * self.grid_w + nc
                    if cell_node[v] == -1:
                        x2, z2 = self.cell_center(nc, nr)
                        # Prefer propagating without crossing fences first
                        if not self.hits_fence(x1, z1, x2, z2):
                            cell_node[v] = cell_node[u]
                            fq.append(v)

        # Fallback across fences only for completely exterior cells outside arena perimeter
        fq = deque(i for i in range(n_cells) if cell_node[i] >= 0)
        while fq:
            u = fq.popleft()
            uc = u % self.grid_w
            ur = u // self.grid_w
            for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nc, nr = uc + dc, ur + dr
                if 0 <= nc < self.grid_w and 0 <= nr < self.grid_h:
                    v = nr * self.grid_w + nc
                    if cell_node[v] == -1:
                        cell_node[v] = cell_node[u]
                        fq.append(v)

        # Build pickup table mapped to cell_node
        pickup_records = []
        for px, pz, typ, sub in self.pickups:
            sc, sr = self.world_to_cell(px, pz)
            pidx = sr * self.grid_w + sc
            if walkable[pidx] or any(
                0 <= sc + dc < self.grid_w and 0 <= sr + dr < self.grid_h and walkable[(sr + dr) * self.grid_w + (sc + dc)]
                for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1))
            ):
                pickup_records.append((px, pz, typ, cell_node[pidx], sub))

        self.nodes = nodes
        self.adj = adj
        self.base_nodes = base_nodes
        self.next_hop = next_hop
        self.dist_eighth = dist_eighth
        self.cell_node = cell_node
        self.pickup_records = pickup_records
        return {
            "walkable_cells": sum(walkable),
            "total_cells": n_cells,
            "nodes": N,
            "edges": sum(len(a) for a in adj) // 2,
            "pickups": len(pickup_records),
            "unreachable_pairs": sum(1 for x in next_hop if x == -1),
        }

    def write_nav(self, out_path):
        N = len(self.nodes)
        P = len(self.pickup_records)
        bx0, bz0 = (self.bases[0][0], self.bases[0][2]) if 0 in self.bases else (0.0, 0.0)
        bx1, bz1 = (self.bases[1][0], self.bases[1][2]) if 1 in self.bases else (0.0, 0.0)
        with open(out_path, "wb") as f:
            # Header (56 bytes): 4s 4H 5f 2H 2h 4f
            f.write(struct.pack(
                "<4s4H5f2H2h4f",
                MAGIC,
                VERSION,
                0,
                self.grid_w,
                self.grid_h,
                CELL_SIZE,
                0.0,
                0.0,
                self.world_w,
                self.world_h,
                N,
                P,
                self.base_nodes[0],
                self.base_nodes[1],
                bx0,
                bz0,
                bx1,
                bz1,
            ))
            f.write(bytes(self.cell_flags))
            f.write(struct.pack(f"<{len(self.cell_node)}h", *self.cell_node))
            for n in self.nodes:
                f.write(struct.pack(
                    "<4f2H",
                    n["x"],
                    n["y"],
                    n["z"],
                    n["clearance"],
                    n["flags"],
                    n["degree"],
                ))
            for px, pz, typ, ni, sub in self.pickup_records:
                f.write(struct.pack("<2fHhB3x", px, pz, typ, ni, sub & 0xFF))
            f.write(struct.pack(f"<{N * N}h", *self.next_hop))
            f.write(struct.pack(f"<{N * N}H", *self.dist_eighth))

    def write_preview_png(self, out_path):
        scale = 4
        w = self.grid_w * scale
        h = self.grid_h * scale
        buf = bytearray(w * h * 3)

        def set_px(px, py, r, g, b):
            if 0 <= px < w and 0 <= py < h:
                i = (py * w + px) * 3
                buf[i] = r
                buf[i + 1] = g
                buf[i + 2] = b

        def draw_line(x0, y0, x1, y1, r, g, b):
            steps = max(1, int(max(abs(x1 - x0), abs(y1 - y0))))
            for s in range(steps + 1):
                t = s / float(steps)
                set_px(int(round(x0 + (x1 - x0) * t)), int(round(y0 + (y1 - y0) * t)), r, g, b)

        def draw_dot(cx, cy, rad, r, g, b):
            for dy in range(-rad, rad + 1):
                for dx in range(-rad, rad + 1):
                    if dx * dx + dy * dy <= rad * rad:
                        set_px(cx + dx, cy + dy, r, g, b)

        # Draw grid cells
        for gr in range(self.grid_h):
            for gc in range(self.grid_w):
                fl = self.cell_flags[gr * self.grid_w + gc]
                if fl & BN_CELL_HAZARD_TAR:
                    col = (140, 90, 40)
                elif fl & BN_CELL_WALKABLE:
                    col = (210, 230, 210)
                elif fl & BN_CELL_STEEP:
                    col = (70, 70, 90)
                else:
                    col = (40, 40, 45)
                for sy in range(scale):
                    for sx in range(scale):
                        set_px(gc * scale + sx, gr * scale + sy, *col)

        def to_px(wx, wz):
            return int(round((wx / CELL_SIZE) * scale)), int(round((wz / CELL_SIZE) * scale))

        # Draw edges
        for u, nbs in enumerate(self.adj):
            x0, y0 = to_px(self.nodes[u]["x"], self.nodes[u]["z"])
            for v, _ in nbs:
                if u < v:
                    x1, y1 = to_px(self.nodes[v]["x"], self.nodes[v]["z"])
                    draw_line(x0, y0, x1, y1, 80, 140, 220)

        # Draw sample shortest path between Red and Green Base
        if self.base_nodes[0] >= 0 and self.base_nodes[1] >= 0:
            cur = self.base_nodes[0]
            dst = self.base_nodes[1]
            seen = set()
            while cur != dst and cur >= 0 and cur not in seen:
                seen.add(cur)
                nxt = self.next_hop[cur * len(self.nodes) + dst]
                if nxt < 0:
                    break
                x0, y0 = to_px(self.nodes[cur]["x"], self.nodes[cur]["z"])
                x1, y1 = to_px(self.nodes[nxt]["x"], self.nodes[nxt]["z"])
                draw_line(x0, y0, x1, y1, 255, 220, 0)
                cur = nxt

        # Draw fences
        for fx1, fz1, fx2, fz2 in self.fences:
            x0, y0 = to_px(fx1, fz1)
            x1, y1 = to_px(fx2, fz2)
            draw_line(x0, y0, x1, y1, 230, 60, 60)

        # Draw nodes & bases/torches
        for n in self.nodes:
            px, py = to_px(n["x"], n["z"])
            draw_dot(px, py, 1, 30, 80, 200)
        for tx, _ty, tz, team in self.torches:
            px, py = to_px(tx, tz)
            draw_dot(px, py, 2, 255 if team == 0 else 40, 40 if team == 0 else 210, 40)
        for team, (bx, _by, bz) in self.bases.items():
            px, py = to_px(bx, bz)
            draw_dot(px, py, 4, 255 if team == 0 else 20, 20 if team == 0 else 230, 20)

        # Encode PNG
        raw = bytearray()
        row_bytes = w * 3
        for y in range(h):
            raw.append(0)
            raw.extend(buf[y * row_bytes:(y + 1) * row_bytes])

        def chunk(tag, payload):
            return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

        with open(out_path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n")
            f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
            f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
            f.write(chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description="Generate Battle Mode .nav files")
    parser.add_argument("--terrain-dir", default="Data/Terrain", help="Directory containing Battle_*.ter.rsrc")
    parser.add_argument("--preview", default=None, help="Optional directory to output PNG map previews")
    args = parser.parse_args()

    if args.preview:
        os.makedirs(args.preview, exist_ok=True)

    files = sorted(glob.glob(os.path.join(args.terrain_dir, "Battle_*.ter.rsrc")))
    if not files:
        raise SystemExit("No Battle_*.ter.rsrc files found!")

    total_ok = True
    for rsrc_path in files:
        arena = ArenaMap(rsrc_path)
        stats = arena.build_nav()
        nav_path = rsrc_path.replace(".ter.rsrc", ".nav")
        arena.write_nav(nav_path)
        sz_kb = os.path.getsize(nav_path) / 1024.0
        # Verify all starts, bases, and torches reached walkable nodes and connect to each other
        base_ok = (arena.base_nodes[0] >= 0 and arena.base_nodes[1] >= 0 and
                   arena.next_hop[arena.base_nodes[0] * len(arena.nodes) + arena.base_nodes[1]] >= 0)
        starts_ok = sum(bool(arena.cell_flags[arena.world_to_cell(s[0], s[2])[1] * arena.grid_w + arena.world_to_cell(s[0], s[2])[0]] & BN_CELL_WALKABLE) for s in arena.starts)
        torches_ok = sum(arena.cell_node[arena.world_to_cell(t[0], t[2])[1] * arena.grid_w + arena.world_to_cell(t[0], t[2])[0]] >= 0 for t in arena.torches)
        if stats["unreachable_pairs"] != 0 or not base_ok or starts_ok < len(arena.starts) or torches_ok < len(arena.torches):
            total_ok = False
        print(f"{arena.name:20s} grid={arena.grid_w}x{arena.grid_h} walk={stats['walkable_cells']:5d}/{stats['total_cells']} "
              f"nodes={stats['nodes']:3d} edges={stats['edges']:4d} pickups={stats['pickups']:2d} "
              f"unreach={stats['unreachable_pairs']} starts={starts_ok}/{len(arena.starts)} "
              f"torches={torches_ok}/{len(arena.torches)} size={sz_kb:5.1f}KB")
        if args.preview:
            arena.write_preview_png(os.path.join(args.preview, f"{arena.name}_nav.png"))

    if not total_ok:
        raise SystemExit("One or more battle arenas failed reachability checks!")


if __name__ == "__main__":
    main()
