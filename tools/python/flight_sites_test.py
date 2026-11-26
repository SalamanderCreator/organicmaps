#!/usr/bin/env python3
import json
import unittest

import flight_sites as fs

PGE = json.dumps({"type": "FeatureCollection", "features": [{
    "type": "Feature", "geometry": {"type": "Point", "coordinates": [11.0, 47.0]},
    "properties": {"name": "Hochfelln", "pge_site_id": "42", "takeoff_altitude": "1650", "paragliding": "1",
                   "N": "0", "W": "2", "NW": "1", "landing_lat": "47.01", "landing_lng": "11.01",
                   "landing_altitude": "0", "flight_rules": "Nur mit<br>Einweisung", "going_there": "Bergbahn"}}]})
OSM = json.dumps({"elements": [
    {"type": "way", "id": 7, "tags": {"free_flying:site": "landing", "ele": "600 m"},
     "geometry": [{"lat": 47.009, "lon": 11.009}, {"lat": 47.011, "lon": 11.009}, {"lat": 47.011, "lon": 11.011},
                  {"lat": 47.009, "lon": 11.011}, {"lat": 47.009, "lon": 11.009}]},
    {"type": "node", "id": 9, "lat": 47.5, "lon": 11.5,
     "tags": {"free_flying:site": "takeoff", "name": "Kleiner Start", "free_flying:site:orientation": "S;SW"}},
    {"type": "node", "id": 10, "lat": 47.6, "lon": 11.6,
     "tags": {"free_flying:site": "takeoff", "free_flying:paragliding": "no"}}]})


class FlightSitesTest(unittest.TestCase):
    def test_directions(self):
        self.assertEqual(fs.parse_directions("SW-NW"), sum(1 << s for s in range(10, 15)))
        self.assertEqual(fs.parse_directions("NO, so"), (1 << 2) | (1 << 6))
        self.assertEqual(fs.parse_directions("250-300"), (1 << 11) | (1 << 12) | (1 << 13))
        self.assertEqual(fs.parse_directions(None), 0)

    def test_pge(self):
        sites = fs.parse_pge(PGE)
        self.assertEqual(len(sites), 2)
        takeoff, landing = sites
        self.assertEqual(takeoff["alt"], 1650)
        self.assertEqual(takeoff["dirs"], (1 << 12) | (1 << 14))
        self.assertEqual(takeoff["rules"], "Nur mit\nEinweisung")
        self.assertEqual(takeoff["access"], "Bergbahn")
        self.assertEqual(takeoff["links"], ["pge:42:landing"])
        self.assertEqual(landing["links"], ["pge:42"])
        self.assertNotIn("alt", landing)

    def test_merge(self):
        merged = fs.merge(fs.parse_pge(PGE), fs.parse_overpass(OSM))
        self.assertEqual(len(merged), 3)
        landing = merged[1]
        self.assertEqual(landing["src"], 3)
        self.assertEqual(len(landing["area"]), 10)
        self.assertEqual(landing["alt"], 600)
        self.assertEqual(merged[2]["id"], "osm:n9")
        self.assertEqual(merged[2]["dirs"], (1 << 8) | (1 << 10))

    def test_osm_tiles_keep_previous_sites(self):
        previous = [{"id": "osm:n1", "lat": 47.5, "lon": 11.5}, {"id": "osm:n2", "lat": 47.5, "lon": 21.5},
                    {"id": "pge:3", "lat": 47.5, "lon": 11.6}]
        new = {"id": "osm:n4", "lat": 47.6, "lon": 21.6}
        original = fs.load_osm_tile
        try:
            # The first tile does not answer, the second does.
            fs.load_osm_tile = lambda tile: None if tile[1] < 20 else [new]
            sites = fs.load_osm((40, 10, 50, 30), previous)
        finally:
            fs.load_osm_tile = original
        self.assertEqual([x["id"] for x in sites], ["osm:n1", "osm:n4"])

        fs.load_osm_tile = lambda tile: None
        try:
            self.assertIsNone(fs.load_osm((40, 10, 50, 30), previous))
        finally:
            fs.load_osm_tile = original


if __name__ == "__main__":
    unittest.main()
