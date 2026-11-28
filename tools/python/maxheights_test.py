#!/usr/bin/env python3
"""Tests of maxheights.py: python3 tools/python/maxheights_test.py"""

import json
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import maxheights  # noqa: E402


class MaxHeightsTest(unittest.TestCase):
    def test_parse_height(self):
        self.assertEqual(maxheights.parse_height_cm("3"), 300)
        self.assertEqual(maxheights.parse_height_cm("2,9"), 290)
        self.assertEqual(maxheights.parse_height_cm("3.85 m"), 385)
        self.assertEqual(maxheights.parse_height_cm("2.5m"), 250)
        self.assertIsNone(maxheights.parse_height_cm("default"))
        self.assertIsNone(maxheights.parse_height_cm("12'6\""))
        self.assertIsNone(maxheights.parse_height_cm("0.5"))
        self.assertIsNone(maxheights.parse_height_cm("5"))
        self.assertIsNone(maxheights.parse_height_cm(None))

    def test_lowest_of_both_keys(self):
        self.assertEqual(maxheights.element_height_cm({"maxheight": "3.2", "maxheight:physical": "3.05"}), 305)
        self.assertEqual(maxheights.element_height_cm({"maxheight": "none", "maxheight:physical": "4"}), 400)

    def test_way_quarters_and_nodes(self):
        answer = {"elements": [
            {"type": "way", "tags": {"highway": "residential", "maxheight": "2.8"},
             "geometry": [{"lat": 48.0, "lon": 11.0}, {"lat": 48.0, "lon": 11.004}]},
            {"type": "node", "lat": 47.5, "lon": 12.5, "tags": {"barrier": "height_restrictor", "maxheight": "2"}},
            {"type": "way", "tags": {"highway": "service", "maxheight": "below_default"},
             "geometry": [{"lat": 48.0, "lon": 11.0}, {"lat": 48.0, "lon": 11.004}]},
        ]}
        entries = maxheights.parse_overpass(json.dumps(answer))
        self.assertEqual(len(entries), 2)
        lat_a, lon_a, lat_b, lon_b, cm = entries[0]
        self.assertEqual(cm, 280)
        self.assertAlmostEqual(lon_a, 11.001)
        self.assertAlmostEqual(lon_b, 11.003)
        self.assertEqual(entries[1], (47.5, 12.5, 47.5, 12.5, 200))

    def test_point_along_bent_way(self):
        points = [(48.0, 11.0), (48.0, 11.002), (48.001, 11.002)]
        lat, lon = maxheights.point_along(points, 0.5)
        self.assertAlmostEqual(lat, 48.0, places=4)

    def test_query_has_both_keys(self):
        query = maxheights.overpass_query("AT")
        self.assertIn('"ISO3166-1"="AT"', query)
        self.assertIn('"maxheight:physical"', query)

    def test_binary_format(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "maxheights.bin")
            count = maxheights.write_entries(path, [(48.0, 11.00025, 48.0, 11.00075, 290)] * 2)
            self.assertEqual(count, 1)
            with open(path, "rb") as f:
                data = f.read()
        self.assertEqual(data[:4], b"MXH1")
        self.assertEqual(struct.unpack("<I", data[4:8])[0], 1)
        self.assertEqual(struct.unpack("<iiiiH", data[8:]), (480000000, 110002500, 480000000, 110007500, 290))


if __name__ == "__main__":
    unittest.main()
