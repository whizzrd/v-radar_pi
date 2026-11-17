#!/usr/bin/env python3
"""Extract radar-relevant S-57 ENC features around a position into a small text file.

usage: enc_extract.py LAT LON RADIUS_NM OUT [ENC_DIR]
Only reads cells that are fully local (never iCloud 'dataless' ones: reading those would
trigger a download). Output lines:
  L lon1 lat1 lon2 lat2   land-area polygon edge (shoreline; also drives inside/outside parity)
  E lon1 lat1 lon2 lat2   line reflector (coastline, shoreline construction, bridges)
  P lon lat OBJ           point target (buoy, beacon, ...)
"""
import json, math, os, subprocess, sys, glob

OGR = "/Applications/QGIS.app/Contents/MacOS/bin"
SF_DATALESS = 0x40000000
EPS = 1e-5
POINT_LAYERS = ["BOYLAT", "BOYCAR", "BOYISD", "BOYSAW", "BOYSPP", "BCNLAT", "BCNCAR", "BCNISD", "BCNSAW", "BCNSPP", "LNDMRK", "PILPNT", "MORFAC"]
LINE_LAYERS = ["COALNE", "SLCONS", "BRIDGE", "DAMCON", "PONTON"]

def is_local(path):
    return not (os.stat(path).st_flags & SF_DATALESS)

def run(args):
    env = dict(os.environ, OGR_S57_OPTIONS="RETURN_PRIMITIVES=OFF,SPLIT_MULTIPOINT=ON,ADD_SOUNDG_DEPTH=OFF,UPDATES=APPLY,RETURN_LINKAGES=OFF")
    r = subprocess.run(args, capture_output=True, text=True, env=env)
    return r.stdout

CACHE = os.path.expanduser("~/Library/Caches/v-radar-enc-coverage.json")
_cov_cache = None

def coverage(cell):
    global _cov_cache
    if _cov_cache is None:
        try:
            _cov_cache = json.load(open(CACHE))
        except Exception:
            _cov_cache = {}
    key = cell + "@" + str(int(os.stat(cell).st_mtime))
    if key not in _cov_cache:
        _cov_cache[key] = _coverage_uncached(cell)
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        json.dump(_cov_cache, open(CACHE, "w"))
    v = _cov_cache[key]
    return tuple(v) if v else None

def _coverage_uncached(cell):
    out = run([OGR + "/ogrinfo", "-ro", "-so", cell, "M_COVR"])
    for l in out.splitlines():
        if l.startswith("Extent:"):
            nums = [float(x) for x in l.replace("(", " ").replace(")", " ").replace(",", " ").replace("-", " ", 1).split()[1:] if x.replace(".", "", 1).replace("-", "", 1).isdigit()]
            if len(nums) >= 4:
                return nums[0], nums[1], nums[2], nums[3]
    return None

def convert_cell(cell, bbox, gpkg):
    """Parse the S-57 cell once and write the layers we need to a GeoPackage."""
    if os.path.exists(gpkg):
        os.remove(gpkg)
    layers = ["LNDARE"] + LINE_LAYERS + POINT_LAYERS
    run([OGR + "/ogr2ogr", "-f", "GPKG", gpkg, cell, *layers, "-spat", *map(str, bbox), "-skipfailures"])

_present = {}

def present_layers(gpkg):
    if gpkg not in _present:
        out = run([OGR + "/ogrinfo", "-ro", "-q", gpkg])
        _present[gpkg] = {l.split(":")[1].split("(")[0].strip() for l in out.splitlines() if ":" in l}
    return _present[gpkg]

def layer_json(gpkg, layer, bbox):
    if layer not in present_layers(gpkg):
        return []
    out = run([OGR + "/ogr2ogr", "-f", "GeoJSON", "/vsistdout/", gpkg, layer])
    try:
        return json.loads(out)["features"]
    except Exception:
        return []

def rings(geom):
    t, c = geom["type"], geom["coordinates"]
    if t == "Polygon": return c
    if t == "MultiPolygon": return [r for p in c for r in p]
    return []

def lines(geom):
    t, c = geom["type"], geom["coordinates"]
    if t == "LineString": return [c]
    if t == "MultiLineString": return c
    if t in ("Polygon",): return c
    if t == "MultiPolygon": return [r for p in c for r in p]
    return []

def main():
    lat, lon, nm, out = float(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    enc = sys.argv[5] if len(sys.argv) > 5 else os.path.expanduser("~/Documents/Charts/ENC")
    dlat = nm / 60.0
    dlon = dlat / max(0.1, math.cos(math.radians(lat)))
    bbox = (lon - dlon, lat - dlat, lon + dlon, lat + dlat)
    n = {"L": 0, "E": 0, "P": 0}
    skipped = 0
    with open(out, "w") as f:
        f.write(f"# ENC extract around {lat} {lon} radius {nm} NM\n")
        for cell in sorted(glob.glob(enc + "/**/*.000", recursive=True)):
            if not is_local(cell):
                # Only check overlap cheaply from the file name is impossible; skip dataless cells.
                skipped += 1
                continue
            cov = coverage(cell)
            if not cov or cov[2] < bbox[0] or cov[0] > bbox[2] or cov[3] < bbox[1] or cov[1] > bbox[3]:
                continue
            print("cell", cell, cov, file=sys.stderr)
            gpkg = out + ".cell.gpkg"
            convert_cell(cell, bbox, gpkg)
            _present.pop(gpkg, None)
            for ft in layer_json(gpkg, "LNDARE", bbox):
                g = ft.get("geometry")
                if not g: continue
                for ring in rings(g):
                    for a, b in zip(ring, ring[1:]):
                        on_edge = all(abs(p[0] - cov[0]) < EPS or abs(p[0] - cov[2]) < EPS or abs(p[1] - cov[1]) < EPS or abs(p[1] - cov[3]) < EPS for p in (a, b)) and \
                                  (abs(a[0] - b[0]) < EPS or abs(a[1] - b[1]) < EPS)
                        f.write(f"{'E' if False else 'L'} {a[0]:.6f} {a[1]:.6f} {b[0]:.6f} {b[1]:.6f}{' X' if on_edge else ''}\n")
                        n["L"] += 1
            for layer in LINE_LAYERS:
                for ft in layer_json(gpkg, layer, bbox):
                    g = ft.get("geometry")
                    if not g: continue
                    for ln in lines(g):
                        for a, b in zip(ln, ln[1:]):
                            f.write(f"E {a[0]:.6f} {a[1]:.6f} {b[0]:.6f} {b[1]:.6f}\n"); n["E"] += 1
            for layer in POINT_LAYERS:
                for ft in layer_json(gpkg, layer, bbox):
                    g = ft.get("geometry")
                    if g and g["type"] == "Point":
                        f.write(f"P {g['coordinates'][0]:.6f} {g['coordinates'][1]:.6f} {layer}\n"); n["P"] += 1
    if os.path.exists(out + ".cell.gpkg"):
        os.remove(out + ".cell.gpkg")
    print(f"wrote {out}: {n}; skipped {skipped} dataless cells", file=sys.stderr)

main()
