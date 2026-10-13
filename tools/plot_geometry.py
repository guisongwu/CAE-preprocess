#!/usr/bin/env python3
"""Plot actual reimported CAD boundary samples; this is not a temperature result."""
import argparse
import json
from pathlib import Path
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    if report['status'] != 'PASS' or report['length_unit'] != 'm':
        raise ValueError('Expected a validated geometry report in metres')
    stem = args.report.parent/'geometry_preview'
    outputs = [stem.with_suffix('.png'), stem.with_suffix('.svg')]
    if any(path.exists() for path in outputs):
        raise FileExistsError('Preview already exists; use a new geometry output directory')
    p = report['parameters']
    # A display-unit conversion only. Raw report/CAD coordinates remain in m.
    hole, outer = [np.asarray(loop['points_m'])[:, :2]*1000
                   for loop in report['after_import']['loops']]
    width, height, radius = [1000*p[key] for key in ('width_m', 'height_m', 'radius_m')]
    cx, cy = 1000*p['center_x_m'], 1000*p['center_y_m']
    with plt.rc_context({'font.size': 12, 'axes.spines.top': False,
                         'axes.spines.right': False, 'svg.fonttype': 'path'}):
        fig, ax = plt.subplots(figsize=(10, 6.3), layout='constrained')
        ax.add_patch(Polygon(outer, closed=True, facecolor='#E6EBF0', edgecolor='#17365D', linewidth=2))
        ax.add_patch(Polygon(hole, closed=True, facecolor='white', edgecolor='#17365D', linewidth=2))
        ax.plot(cx, cy, marker='+', markersize=9, color='#17365D')
        ax.annotate(f'Hole: r = {radius:g} mm', xy=(cx+radius, cy),
                    xytext=(width*.73, height*.73),
                    arrowprops={'arrowstyle': '->', 'color': '#17365D'}, color='#17365D')
        ax.text(width*.5, height+height*.09, f'W = {width:g} mm', ha='center')
        ax.text(width+width*.035, height*.5, f'H = {height:g} mm', rotation=90, va='center')
        fig.suptitle('Stage 01 | Perforated plate CAD geometry', fontsize=18)
        ax.set(xlabel='x (mm)', ylabel='y (mm)',
               title='STEP reimport verified | 1 face, 2 closed loops | Geometry only',
               xlim=(-width*.04, width*1.1), ylim=(-height*.1, height*1.18))
        ax.set_aspect('equal', adjustable='box')
        fig.savefig(outputs[0], dpi=180, facecolor='white')
        fig.savefig(outputs[1], facecolor='white')
        plt.close(fig)
    provenance = {
        'source_report': str(args.report.resolve()),
        'source': 'after_import.loops[].points_m: sampled STEP-reimported CAD curves',
        'transform': 'display coordinates = source metres * 1000 (millimetres)',
        'representation': 'line curves use 2 samples; other curves use 65 samples; equal aspect ratio',
        'limitations': 'Display uses straight segments between samples; analytic CAD geometry supplies verification measures. No mesh or temperature field.',
        'outputs': [str(path.resolve()) for path in outputs],
        'matplotlib_version': matplotlib.__version__,
    }
    (args.report.parent/'geometry_preview.provenance.json').write_text(
        json.dumps(provenance, ensure_ascii=False, indent=2)+'\n')
    print('\n'.join(str(path.resolve()) for path in outputs))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'error: {error}', file=sys.stderr)
        sys.exit(1)
