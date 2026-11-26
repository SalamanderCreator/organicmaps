#!/usr/bin/env python3
"""Maps&Fly: builds the paragliding takeoffs and landings of the flight preparation layer.

Reads ParaglidingEarth (CC BY-SA, https://paragliding.earth) and OpenStreetMap (ODbL, Overpass API),
merges the same sites of both and writes one JSON file that is packed into the APK. A source that cannot
be reached is left out, so the build still gets the other one. DHV data is never read here: its terms
forbid passing it on, so it is only imported privately on the phone.

Usage: flight_sites.py OUTPUT.json [--bbox SOUTH,WEST,NORTH,EAST]
"""

import argparse
import json
import math
import re
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime, timezone

# Europe and the Alps' neighbours, where Maps&Fly is used.
DEFAULT_BBOX = (34.0, -25.0, 72.0, 45.0)
# ParaglidingEarth moves to paragliding.earth, the old domain may stay for a while.
PGE_HOSTS = ["https://www.paragliding.earth", "https://www.paraglidingearth.com"]
PGE_PATH = "/api/geojson/getBoundingBoxSites.php?north={n}&south={s}&east={e}&west={w}&limit=5000&style=detailled"
PGE_SITE_URL = "https://www.paragliding.earth/?site="
PGE_TILE_DEG = 5
OVERPASS_URLS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]
OVERPASS_QUERY = '[out:json][timeout:600];nwr["free_flying:site"]({s},{w},{n},{e});out tags geom;'
OSM_URL = "https://www.openstreetmap.org/"
USER_AGENT = "Maps&Fly flight sites builder (github.com/SalamanderCreator/organicmaps)"

PGE_DIRECTIONS = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
OSM_DIRECTION_KEYS = ["free_flying:site:orientation", "free_flying:takeoff:direction", "free_flying:orientation",
                      "direction", "orientation"]
# Free texts of ParaglidingEarth, under the names used by its API versions.
PGE_TEXTS = {
    "desc": ["takeoff_description", "description"],
    "rules": ["flight_rules", "rules"],
    "access": ["going_there", "access", "takeoff_access"],
    "comments": ["comments"],
    "weather": ["weather"],
}
SECTOR_NAMES = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"]
SAME_SITE_M = 200


def log(*args):
    print("flight_sites:", *args, file=sys.stderr)


def download(url, data=None, timeout=900):
    request = urllib.request.Request(url, data=data, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read().decode("utf-8")


def to_sector(value):
    name = value.strip().upper().replace("O", "E")
    if name in SECTOR_NAMES:
        return SECTOR_NAMES.index(name)
    try:
        degrees = float(name)
    except ValueError:
        return -1
    if degrees < 0 or degrees > 360:
        return -1
    return int(round(degrees / 22.5)) % 16


def parse_directions(value):
    """Directions like "N;NW", "SW-NW", "NO" (German east), "270" or "250-300" as 16 sector bits."""
    if not value:
        return 0
    mask = 0
    for part in re.split(r"[;,/ ]+", value):
        part = part.strip()
        if not part:
            continue
        dash = part.find("-", 1)
        if dash > 0:
            start, end = to_sector(part[:dash]), to_sector(part[dash + 1:])
            if start < 0 or end < 0:
                continue
            s = start
            while True:
                mask |= 1 << s
                if s == end:
                    break
                s = (s + 1) % 16
        else:
            s = to_sector(part)
            if s >= 0:
                mask |= 1 << s
    return mask


def altitude(value):
    if value is None:
        return None
    m = re.match(r"^\s*(-?\d+(?:[.,]\d+)?)", str(value))
    if not m:
        return None
    alt = float(m.group(1).replace(",", "."))
    # ParaglidingEarth writes 0 for an unknown height.
    return None if alt == 0 else int(round(alt))


def number(value):
    try:
        v = float(value)
    except (TypeError, ValueError):
        return None
    return v if math.isfinite(v) else None


def clean(text):
    if text is None:
        return ""
    text = re.sub(r"<br\s*/?>", "\n", str(text), flags=re.I)
    text = re.sub(r"<[^>]+>", "", text)
    return re.sub(r"[ \t]+", " ", text).strip()


def site(id_, name, lat, lon, landing, alt, dirs, src, url, area=None, **texts):
    s = {"id": id_, "name": name, "lat": round(lat, 6), "lon": round(lon, 6), "landing": landing, "dirs": dirs,
         "src": src, "url": url}
    if alt is not None:
        s["alt"] = alt
    if area:
        s["area"] = [round(v, 6) for v in area]
    for key, value in texts.items():
        if value:
            s[key] = value
    return s


def parse_pge(text):
    sites = []
    for feature in json.loads(text).get("features") or []:
        p = feature.get("properties") or {}
        coords = (feature.get("geometry") or {}).get("coordinates") or []
        if len(coords) < 2:
            continue
        if "paragliding" in p and str(p.get("paragliding")).strip() in ("0", "false"):
            continue
        pid = str(p.get("pge_site_id") or p.get("id") or "").strip()
        if not pid:
            continue
        name = clean(p.get("name"))
        url = PGE_SITE_URL + pid
        orientations = p.get("orientations") if isinstance(p.get("orientations"), dict) else p
        dirs = 0
        for i, d in enumerate(PGE_DIRECTIONS):
            try:
                if int(float(orientations.get(d) or 0)) > 0:
                    dirs |= 1 << (i * 2)
            except (TypeError, ValueError):
                pass
        texts = {key: next((clean(p[k]) for k in keys if p.get(k)), "") for key, keys in PGE_TEXTS.items()}
        takeoff_id = "pge:" + pid
        takeoff = site(takeoff_id, name, float(coords[1]), float(coords[0]), False, altitude(p.get("takeoff_altitude")),
                       dirs, 1, url, **texts)
        sites.append(takeoff)
        lat, lon = number(p.get("landing_lat")), number(p.get("landing_lng") or p.get("landing_lon"))
        if lat is not None and lon is not None and (lat != 0 or lon != 0):
            landing_id = takeoff_id + ":landing"
            sites.append(site(landing_id, clean(p.get("landing_name")) or name, lat, lon, True,
                              altitude(p.get("landing_altitude")), 0, 1, url,
                              desc=clean(p.get("landing_description")), links=[takeoff_id]))
            takeoff["links"] = [landing_id]
    return sites


def parse_overpass(text):
    sites = []
    for e in json.loads(text).get("elements") or []:
        tags = e.get("tags") or {}
        if tags.get("free_flying:paragliding") == "no":
            continue
        kind = tags.get("free_flying:site", "").lower()
        if "landing" in kind:
            landing = True
        elif any(k in kind for k in ("takeoff", "launch", "towing", "winch")):
            landing = False
        else:
            continue
        type_ = e.get("type")
        area = None
        if type_ == "node":
            lat, lon = e.get("lat"), e.get("lon")
        elif type_ == "way" and e.get("geometry"):
            g = e["geometry"]
            if len(g) >= 4 and g[0] == g[-1]:
                area = [v for pt in g for v in (pt["lat"], pt["lon"])]
                lat = sum(pt["lat"] for pt in g[:-1]) / (len(g) - 1)
                lon = sum(pt["lon"] for pt in g[:-1]) / (len(g) - 1)
            else:
                lat, lon = g[len(g) // 2]["lat"], g[len(g) // 2]["lon"]
        elif e.get("bounds"):
            b = e["bounds"]
            lat, lon = (b["minlat"] + b["maxlat"]) / 2, (b["minlon"] + b["maxlon"]) / 2
        else:
            continue
        if lat is None or lon is None:
            continue
        dirs = 0
        if not landing:
            for key in OSM_DIRECTION_KEYS:
                dirs |= parse_directions(tags.get(key))
        sites.append(site("osm:%s%d" % (type_[0], e["id"]), tags.get("name", "").strip(), lat, lon, landing,
                          altitude(tags.get("ele")), dirs, 2, "%s%s/%d" % (OSM_URL, type_, e["id"]), area,
                          desc=tags.get("description", "").strip(), access=tags.get("access:description", "").strip(),
                          web=tags.get("website", "").strip()))
    return sites


def distance_m(lat1, lon1, lat2, lon2):
    d_lat = math.radians(lat2 - lat1)
    d_lon = math.radians(lon2 - lon1) * math.cos(math.radians((lat1 + lat2) / 2))
    return 6371000 * math.hypot(d_lat, d_lon)


def inside(ring, lat, lon):
    result = False
    n = len(ring) // 2
    j = n - 1
    for i in range(n):
        yi, xi, yj, xj = ring[2 * i], ring[2 * i + 1], ring[2 * j], ring[2 * j + 1]
        if (yi > lat) != (yj > lat) and lon < (xj - xi) * (lat - yi) / (yj - yi) + xi:
            result = not result
        j = i
    return result


def merge(pge, osm):
    """Adds the OpenStreetMap sites to the ParaglidingEarth ones, a site in both only once."""
    result = list(pge)
    # A coarse grid keeps the search fast for tens of thousands of sites.
    grid = {}
    for p in pge:
        grid.setdefault((int(p["lat"] * 100), int(p["lon"] * 100)), []).append(p)
    for o in osm:
        same = None
        cy, cx = int(o["lat"] * 100), int(o["lon"] * 100)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                for p in grid.get((cy + dy, cx + dx), []):
                    if same is None and p["landing"] == o["landing"] and (
                            distance_m(p["lat"], p["lon"], o["lat"], o["lon"]) < SAME_SITE_M
                            or (o.get("area") and inside(o["area"], p["lat"], p["lon"]))):
                        same = p
        if same is None:
            result.append(o)
            continue
        same["src"] |= 2
        same.setdefault("osm", o["url"])
        if not same["dirs"]:
            same["dirs"] = o["dirs"]
        for key in ("area", "alt", "desc", "access", "web"):
            if key not in same and key in o:
                same[key] = o[key]
        if not same["name"]:
            same["name"] = o["name"]
    return result


def load_pge(bbox):
    s0, w0, n0, e0 = bbox
    for host in PGE_HOSTS:
        sites = {}
        try:
            s = s0
            while s < n0:
                w = w0
                while w < e0:
                    url = host + PGE_PATH.format(s=s, w=w, n=min(s + PGE_TILE_DEG, n0), e=min(w + PGE_TILE_DEG, e0))
                    for x in parse_pge(download(url, timeout=120)):
                        sites[x["id"]] = x
                    w += PGE_TILE_DEG
                s += PGE_TILE_DEG
            log("ParaglidingEarth:", len(sites), "sites from", host)
            return list(sites.values())
        except Exception as ex:  # e.g. a "coming soon" page instead of JSON
            log("ParaglidingEarth not usable at", host, "-", ex)
    return None


def load_osm(bbox):
    s, w, n, e = bbox
    data = urllib.parse.urlencode({"data": OVERPASS_QUERY.format(s=s, w=w, n=n, e=e)}).encode()
    for url in OVERPASS_URLS:
        for attempt in range(2):
            try:
                sites = parse_overpass(download(url, data))
                log("OpenStreetMap:", len(sites), "sites from", url)
                return sites
            except Exception as ex:
                log("Overpass not usable at", url, "-", ex)
                time.sleep(10)
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    parser.add_argument("--bbox", default=",".join(str(v) for v in DEFAULT_BBOX))
    args = parser.parse_args()
    bbox = tuple(float(v) for v in args.bbox.split(","))

    pge = load_pge(bbox)
    osm = load_osm(bbox)
    if pge is None and osm is None:
        log("no source could be reached, no file written")
        return 1
    sites = merge(pge or [], osm or [])
    out = {"generated": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "sources": {"pge": pge is not None, "osm": osm is not None}, "sites": sites}
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, separators=(",", ":"))
    log(len(sites), "sites written to", args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
