#!/usr/bin/env python3
"""Acceptance tests: measured CAD invariants, STEP units, and CLI rejection."""
import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

PARSER = argparse.ArgumentParser()
PARSER.add_argument('--executable', required=True)
ARGS, UNITTEST_ARGS = PARSER.parse_known_args()
EXECUTABLE = Path(ARGS.executable).resolve()


class GeometryAcceptance(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='cae-geometry-test-', dir=EXECUTABLE.parent)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def run_case(self, output, *options):
        return subprocess.run([str(EXECUTABLE), '--output', str(output), *options],
                              capture_output=True, text=True, timeout=30)

    def check_valid(self, width, height, radius, cx, cy, *options):
        output = self.root/'case'
        result = self.run_case(output, *options)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads((output/'geometry_report.json').read_text())
        self.assertEqual(report['status'], 'PASS')
        self.assertEqual(report['length_unit'], 'm')
        self.assertTrue(report['gmsh_runtime_version'].startswith('4.12'))
        self.assertEqual(report['parameters'], {
            'width_m': width, 'height_m': height, 'radius_m': radius,
            'center_x_m': cx, 'center_y_m': cy})
        area = width*height - math.pi*radius**2
        perimeter = 2*(width+height) + 2*math.pi*radius
        expected_loops = sorted([2*math.pi*radius, 2*(width+height)])
        for key in ('before_export', 'after_import'):
            snapshot = report[key]
            self.assertTrue(math.isclose(snapshot['area_m2'], area, rel_tol=1e-8))
            self.assertTrue(math.isclose(snapshot['perimeter_m'], perimeter, rel_tol=1e-8))
            self.assertEqual(snapshot['surface_count'], 1)
            self.assertEqual(snapshot['volume_count'], 0)
            self.assertEqual(snapshot['loop_count'], 2)
            self.assertEqual(snapshot['node_count'], 0)
            self.assertEqual(snapshot['element_count'], 0)
            self.assertTrue(snapshot['closed_loops'])
            self.assertEqual(len(snapshot['loops']), 2)
            for loop, length in zip(snapshot['loops'], expected_loops):
                self.assertTrue(math.isclose(loop['length_m'], length, rel_tol=1e-8))
                points = loop['points_m']
                self.assertGreaterEqual(len(points), 3)
                self.assertLessEqual(math.dist(points[0], points[-1]), 1e-6)
            for x, y, z in snapshot['loops'][0]['points_m']:
                self.assertLessEqual(abs(math.hypot(x-cx, y-cy)-radius), 1e-6)
                self.assertLessEqual(abs(z), 1e-6)
            for x, y, z in snapshot['loops'][1]['points_m']:
                self.assertLessEqual(min(abs(x), abs(x-width), abs(y), abs(y-height)), 1e-6)
                self.assertLessEqual(abs(z), 1e-6)
            bbox = snapshot['bounding_box_m']
            for measured, target in zip(bbox, [0, 0, 0, width, height, 0]):
                self.assertLessEqual(abs(measured-target), 1e-6)
        step = (output/'plate.step').read_text()
        self.assertIn('ISO-10303-21;', step)
        # Check a real STEP point in the file's declared unit, independently of
        # the CAD import. Header units alone do not establish coordinate scale.
        unit_to_m = report['step_length_unit_to_m']
        expected_x = width / unit_to_m
        pattern = r"CARTESIAN_POINT\s*\(\s*'[^']*'\s*,\s*\(([^)]*)\)\s*\)"
        point_x = [float(match.split(',')[0]) for match in re.findall(pattern, step)]
        self.assertTrue(any(math.isclose(x, expected_x, rel_tol=1e-8, abs_tol=1e-8)
                            for x in point_x), 'STEP units do not match coordinates')
        return output

    def test_default_centered_hole(self):
        self.check_valid(.1, .05, .01, .05, .025)

    def test_off_center_hole(self):
        self.check_valid(.1, .05, .007, .035, .018,
                         '--radius', '.007', '--cx', '.035', '--cy', '.018')

    def test_different_dimensions(self):
        self.check_valid(.16, .08, .015, .08, .04,
                         '--width', '.16', '--height', '.08', '--radius', '.015')

    def test_invalid_parameters_do_not_create_output(self):
        cases = [('--radius', '-.01'), ('--radius', '0'),
                 ('--radius', '.025'), ('--cx', '.005'), ('--cy', '.049'),
                 ('--width', '0'), ('--height', 'nan'), ('--radius', 'inf'),
                 ('--width', '1x'), ('--radius',), ('--unknown', '1'),
                 ('--width', '.1', '--width', '.2'),
                 ('--radius', '0.0000001')]
        for index, options in enumerate(cases):
            with self.subTest(options=options):
                output = self.root/str(index)
                result = self.run_case(output, *options)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('error:', result.stderr)
                self.assertFalse(output.exists())

    def test_existing_output_is_preserved(self):
        output = self.check_valid(.1, .05, .01, .05, .025)
        original = (output/'plate.step').read_bytes()
        result = self.run_case(output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('already exists', result.stderr)
        self.assertEqual((output/'plate.step').read_bytes(), original)

    def test_help_has_no_geometry_side_effect(self):
        result = subprocess.run([str(EXECUTABLE), '--help'], cwd=self.root,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        self.assertIn('--radius', result.stdout)
        self.assertEqual(list(self.root.iterdir()), [])


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *UNITTEST_ARGS], verbosity=2)
