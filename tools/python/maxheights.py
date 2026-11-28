#!/usr/bin/env python3
"""Maps&Fly: builds the clearances (maxheight) file of the car routing.

The Organic Maps maps do not keep the OpenStreetMap maxheight tags, so the APK carries them in a file of its
own (ODbL, OpenStreetMap contributors). The routing matches them to the roads of the maps: a way is stored as
two points at a quarter and three quarters of its length, a node as one point twice.

Only clearances below 5 m are kept, higher ones matter to no car or camper van.

Format (little endian): "MXH1", uint32 count, then per entry int32 lat/lon of both points in 1e-7 degrees and
uint16 height in cm. See libs/routing/car_restrictions.hpp.

Usage: maxheights.py OUTPUT.bin [--countries AT,CZ,DE]
"""

import argparse
import json
import math
import re
import socket
import struct
import sys
import time
import urllib.parse
import urllib.request

# Europe without Russia and Belarus comes later; one Overpass query per country keeps the answers small.
DEFAULT_COUNTRIES = ["AT", "CZ", "DE"]
OVERPASS_URLS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]
USER_AGENT = "Maps&Fly clearances builder (github.com/SalamanderCreator/organicmaps)"
CAR_HIGHWAYS = ("motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street|service|track|"
                "road|busway")
# Meters below 5, with an optional unit: "3", "3.8", "3,8 m", "2.5m".
VALUE_REGEX = "^ *[0-4]([.,][0-9]+)? *(m)? *$"
HEIGHT_KEYS = ["maxheight", "maxheight:physical"]
MIN_HEIGHT_CM = 100
MAX_HEIGHT_CM = 499
EARTH_M_PER_DEGREE = 111320.0


def ipv4_only():
    # GitHub runners have no IPv6 route, and Python does not always fall back to IPv4.
    original = socket.getaddrinfo

    def getaddrinfo(host, port, family=0, *args, **kwargs):
        return original(host, port, socket.AF_INET, *args, **kwargs)

    socket.getaddrinfo = getaddrinfo


def log(*args):
    print("maxheights:", *args, file=sys.stderr)


def overpass_query(country):
    parts = []
    for key in HEIGHT_KEYS:
        parts.append(f'way(area.c)[highway~"^({CAR_HIGHWAYS})(_link)?$"]["{key}"~"{VALUE_REGEX}"];')
        parts.append(f'node(area.c)["{key}"~"{VALUE_REGEX}"];')
    return (f'[out:json][timeout:900];area["ISO3166-1"="{country}"][admin_level=2]->.c;('
            + "".join(parts) + ");out tags geom;")


def parse_height_cm(value):
    match = re.fullmatch(r" *([0-9]+(?:[.,][0-9]+)?) *(?:m)? *", value or "")
    if not match:
        return None
    cm = round(float(match.group(1).replace(",", ".")) * 100)
    return cm if MIN_HEIGHT_CM <= cm <= MAX_HEIGHT_CM else None


def element_height_cm(tags):
    heights = [h for h in (parse_height_cm(tags.get(key)) for key in HEIGHT_KEYS) if h]
    return min(heights) if heights else None


def point_along(points, fraction):
    """The point at |fraction| of the polyline length, points as (lat, lon)."""
    lengths = []
    for (lat1, lon1), (lat2, lon2) in zip(points, points[1:]):
        scale = math.cos(math.radians((lat1 + lat2) / 2))
        lengths.append(math.hypot(lat2 - lat1, (lon2 - lon1) * scale))
    total = sum(lengths)
    if total == 0:
        return points[0]
    target = total * fraction
    for (start, end), length in zip(zip(points, points[1:]), lengths):
        if target <= length and length > 0:
            t = target / length
            return (start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t)
        target -= length
    return points[-1]


def parse_overpass(text):
    """Entries (lat_a, lon_a, lat_b, lon_b, cm) of an Overpass JSON answer."""
    entries = []
    for element in json.loads(text).get("elements", []):
        cm = element_height_cm(element.get("tags", {}))
        if not cm:
            continue
        if element["type"] == "node":
            entries.append((element["lat"], element["lon"], element["lat"], element["lon"], cm))
        elif element["type"] == "way":
            points = [(p["lat"], p["lon"]) for p in element.get("geometry", []) if p]
            if len(points) < 2:
                continue
            a, b = point_along(points, 0.25), point_along(points, 0.75)
            entries.append((a[0], a[1], b[0], b[1], cm))
    return entries


def download(url, data, timeout):
    request = urllib.request.Request(url, data=data, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read().decode("utf-8")


def fetch_country(country):
    data = urllib.parse.urlencode({"data": overpass_query(country)}).encode()
    for attempt in range(2):
        for url in OVERPASS_URLS:
            try:
                text = download(url, data, timeout=960)
                entries = parse_overpass(text)
                log(country, "from", url, len(entries), "clearances")
                return entries
            except Exception as e:  # Any failure of a busy server: try the next one.
                log(country, "failed at", url, e)
        time.sleep(30 * (attempt + 1))
    return None


def write_entries(path, entries):
    entries = sorted(set(entries))
    with open(path, "wb") as f:
        f.write(b"MXH1")
        f.write(struct.pack("<I", len(entries)))
        for lat_a, lon_a, lat_b, lon_b, cm in entries:
            f.write(struct.pack("<iiiiH", round(lat_a * 1e7), round(lon_a * 1e7), round(lat_b * 1e7),
                                round(lon_b * 1e7), cm))
    return len(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output")
    parser.add_argument("--countries", default=",".join(DEFAULT_COUNTRIES))
    args = parser.parse_args()

    ipv4_only()
    entries = []
    for country in args.countries.split(","):
        country_entries = fetch_country(country.strip())
        if country_entries is None:
            # A partial file would silently drop a country: the build keeps the previous file instead.
            log("no server answered for", country)
            return 1
        entries += country_entries
    log("wrote", write_entries(args.output, entries), "clearances to", args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
