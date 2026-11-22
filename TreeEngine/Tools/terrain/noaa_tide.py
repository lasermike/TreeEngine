"""Writes a `water` object's tide block from a NOAA CO-OPS station's harmonic constituents.

    python noaa_tide.py --station 9446807 --start 2026-06-21T19:00:00Z
    python noaa_tide.py --station 9446807 --fit-to 9446666 --start 2026-06-21T19:00:00Z

Prints JSON: `level` (the station's mean sea level above NAVD88, in meters, for the water's transform y on terrain
in NAVD88 heights) and `tide` (start time, constituents, and where they came from) for the scene file.

Many stations only have predictions, made from a reference station's tide with time and height offsets ("subordinate"
stations). `--fit-to SUBORDINATE` fits the harmonic station's tide to one: it scales the amplitudes and delays the
tide to best match the subordinate's predicted highs and lows over `--fit-month`, and reports how well it fits.

The prediction here is the engine's (Game/Tide.cpp); keep the two in step.
"""

import argparse
import datetime
import json
import math
import sys
import urllib.request

API = 'https://api.tidesandcurrents.noaa.gov'

# Doodson numbers (tau, s, h, p, N', p1) and a phase offset in degrees, as in Game/Tide.cpp.
BASIC = {
    'M2': (2, 0, 0, 0, 0, 0, 0), 'S2': (2, 2, -2, 0, 0, 0, 0), 'N2': (2, -1, 0, 1, 0, 0, 0), 'K2': (2, 2, 0, 0, 0, 0, 0),
    'K1': (1, 1, 0, 0, 0, 0, 90), 'O1': (1, -1, 0, 0, 0, 0, -90), 'P1': (1, 1, -2, 0, 0, 0, -90),
    'Q1': (1, -2, 0, 1, 0, 0, -90), '2N2': (2, -2, 0, 2, 0, 0, 0), 'NU2': (2, -1, 2, -1, 0, 0, 0),
    'MU2': (2, -2, 2, 0, 0, 0, 0), 'L2': (2, 1, 0, -1, 0, 0, 180), 'LAM2': (2, 1, -2, 1, 0, 0, 180),
    'T2': (2, 2, -3, 0, 0, 1, 0), 'R2': (2, 2, -1, 0, 0, -1, 180), 'J1': (1, 2, 0, -1, 0, 0, 90),
    'OO1': (1, 3, 0, 0, 0, 0, 90), '2Q1': (1, -3, 0, 2, 0, 0, -90), 'RHO': (1, -3, 2, 1, 0, 0, -90),
    'S1': (1, 1, -1, 0, 0, 0, 180), 'SA': (0, 0, 1, 0, 0, 0, 0), 'SSA': (0, 0, 2, 0, 0, 0, 0),
    'MM': (0, 1, 0, -1, 0, 0, 0), 'MF': (0, 2, 0, 0, 0, 0, 0), 'M3': (3, 0, 0, 0, 0, 0, 180),
    'S4': (4, 4, -4, 0, 0, 0, 0), 'S6': (6, 6, -6, 0, 0, 0, 0),
}
COMPOUND = {'M4': {'M2': 2}, 'M6': {'M2': 3}, 'M8': {'M2': 4}, 'MS4': {'M2': 1, 'S2': 1}, 'MN4': {'M2': 1, 'N2': 1},
            'MK3': {'M2': 1, 'K1': 1}, '2MK3': {'M2': 2, 'K1': -1}, '2SM2': {'S2': 2, 'M2': -1}}


def get(path):
    request = urllib.request.Request(API + path, headers={'User-Agent': 'TreeEngine terrain tools'})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def astronomy(seconds):
    t = (seconds / 86400 + 2440587.5 - 2451545.0) / 36525
    s = 218.3165 + 481267.8813 * t
    h = 280.4661 + 36000.7698 * t
    p = 83.3535 + 4069.0137 * t
    n = 125.0445 - 1934.1363 * t
    p1 = 282.9384 + 1.7195 * t
    tau = 15 * (seconds % 86400) / 3600 + h - s
    return tau, s, h, p, n, p1


def nodal(name, node):
    n = math.radians(node)
    c1, c2, c3, s1, s2, s3 = math.cos(n), math.cos(2 * n), math.cos(3 * n), math.sin(n), math.sin(2 * n), math.sin(3 * n)
    if name in ('M2', 'N2', '2N2', 'NU2', 'MU2', 'L2', 'LAM2', 'M3'):
        f, u = 1.0004 - 0.0373 * c1 + 0.0002 * c2, -2.14 * s1
        return (f ** 1.5, 1.5 * u) if name == 'M3' else (f, u)
    if name == 'K1':
        return 1.0060 + 0.1150 * c1 - 0.0088 * c2 + 0.0006 * c3, -8.86 * s1 + 0.68 * s2 - 0.07 * s3
    if name in ('O1', 'Q1', '2Q1', 'RHO'):
        return 1.0089 + 0.1871 * c1 - 0.0147 * c2 + 0.0014 * c3, 10.80 * s1 - 1.34 * s2 + 0.19 * s3
    if name == 'K2':
        return 1.0241 + 0.2863 * c1 + 0.0083 * c2 - 0.0015 * c3, -17.74 * s1 + 0.68 * s2 - 0.04 * s3
    if name == 'J1':
        return 1.1029 + 0.1676 * c1 - 0.0170 * c2 + 0.0016 * c3, -12.94 * s1 + 1.34 * s2 - 0.19 * s3
    if name == 'OO1':
        return 1.1027 + 0.6504 * c1 + 0.0317 * c2 - 0.0014 * c3, -36.68 * s1 + 4.02 * s2 - 0.57 * s3
    if name == 'MF':
        return 1.043 + 0.414 * c1, -23.74 * s1 + 2.68 * s2 - 0.38 * s3
    if name == 'MM':
        return 1.000 - 0.130 * c1, 0.0
    f, u = 1.0, 0.0
    for part, m in COMPOUND.get(name, {}).items():
        fk, uk = nodal(part, node)
        f, u = f * fk ** abs(m), u + m * uk
    return f, u


def argument(name, a):
    if name in BASIC:
        d = BASIC[name]
        return d[0] * a[0] + d[1] * a[1] + d[2] * a[2] + d[3] * a[3] - d[4] * a[4] + d[5] * a[5] + d[6]
    return sum(m * argument(part, a) for part, m in COMPOUND[name].items())


def height(constituents, seconds):
    a = astronomy(seconds)
    z = 0.0
    for c in constituents:
        f, u = nodal(c['name'], a[4])
        z += f * c['amplitude'] * math.cos(math.radians(argument(c['name'], a) + u - c['phase']))
    return z


def utc(text):
    return datetime.datetime.strptime(text, '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=datetime.timezone.utc).timestamp()


def fit(constituents, subordinate, month):
    """The scale and delay (minutes) that best match the subordinate station's predicted highs and lows."""
    first = datetime.date(int(month[:4]), int(month[4:]), 1)
    last = (first.replace(day=28) + datetime.timedelta(days=4)).replace(day=1) - datetime.timedelta(days=1)
    data = get(f'/api/prod/datagetter?product=predictions&application=TreeEngine&begin_date={first:%Y%m%d}'
               f'&end_date={last:%Y%m%d}&datum=MLLW&station={subordinate}&time_zone=gmt&units=metric&interval=hilo'
               '&format=json')
    if 'predictions' not in data:
        sys.exit(f'no predictions for station {subordinate}: {data}')
    times = [datetime.datetime.strptime(p['t'], '%Y-%m-%d %H:%M').replace(tzinfo=datetime.timezone.utc).timestamp()
             for p in data['predictions']]
    values = [float(p['v']) for p in data['predictions']]
    best = None
    for delay in range(-180, 181):
        h = [height(constituents, t - 60 * delay) for t in times]
        n = len(h)
        mh, mv = sum(h) / n, sum(values) / n
        scale = sum((x - mh) * (y - mv) for x, y in zip(h, values)) / sum((x - mh) ** 2 for x in h)
        offset = mv - scale * mh
        rms = math.sqrt(sum((scale * x + offset - y) ** 2 for x, y in zip(h, values)) / n)
        if best is None or rms < best[0]:
            best = (rms, delay, scale)
    return best[2], best[1], best[0], len(times)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--station', required=True, help='NOAA station with harmonic constituents')
    parser.add_argument('--start', required=True, help='UTC time at simulation time 0, YYYY-MM-DDTHH:MM:SSZ')
    parser.add_argument('--fit-to', help='a subordinate (prediction-only) station to fit the tide to')
    parser.add_argument('--fit-month', default='202606', help='month of predictions to fit, YYYYMM (default 202606)')
    parser.add_argument('--min-amplitude', type=float, default=0.001, help='drop smaller constituents (m)')
    args = parser.parse_args()
    utc(args.start)

    station = get(f'/mdapi/prod/webapi/stations/{args.station}.json')['stations'][0]
    harmonics = get(f'/mdapi/prod/webapi/stations/{args.station}/harcon.json?units=metric')['HarmonicConstituents']
    datums = {d['name']: d['value'] for d in get(f'/mdapi/prod/webapi/stations/{args.station}/datums.json?units=metric')
              .get('datums') or []}
    if not harmonics:
        sys.exit(f'station {args.station} publishes no harmonic constituents')
    if 'MSL' not in datums or 'NAVD88' not in datums:
        sys.exit(f'station {args.station} has no MSL or NAVD88 datum')
    constituents, dropped = [], []
    for h in harmonics:
        if h['amplitude'] < args.min_amplitude:
            continue
        if h['name'] in BASIC or h['name'] in COMPOUND:
            constituents.append({'name': h['name'], 'amplitude': h['amplitude'], 'phase': h['phase_GMT'],
                                 'speed': h['speed']})
        else:
            dropped.append(f"{h['name']} ({h['amplitude'] * 1000:.0f} mm)")
    if dropped:
        print('not supported, left out: ' + ', '.join(dropped), file=sys.stderr)

    source = f"NOAA {args.station} {station['name']}"
    if args.fit_to:
        scale, delay, rms, count = fit(constituents, args.fit_to, args.fit_month)
        subordinate = get(f'/mdapi/prod/webapi/stations/{args.fit_to}.json')['stations'][0]['name']
        print(f'fit to {args.fit_to} {subordinate}: x{scale:.3f}, {delay:+d} min, {rms * 100:.1f} cm RMS over {count} '
              f'highs and lows in {args.fit_month}', file=sys.stderr)
        for c in constituents:
            c['amplitude'] *= scale
            c['phase'] = (c['phase'] + c['speed'] * delay / 60) % 360   # A later tide: V(t - delay) = V(t) - speed * delay.
        source += f', x{scale:.3f} and {delay:+d} min to fit {args.fit_to} {subordinate}'

    level = datums['MSL'] - datums['NAVD88']
    tide = {'source': source, 'start': args.start,
            'constituents': [{'name': c['name'], 'amplitude': round(c['amplitude'], 4), 'phase': round(c['phase'], 2)}
                             for c in sorted(constituents, key=lambda c: -c['amplitude'])]}
    lows = [height(tide['constituents'], utc(args.start) + 600 * i) for i in range(6 * 24 * 30)]
    print(f'mean sea level {level:.3f} m above NAVD88; over the first 30 days the tide runs {min(lows):+.2f} to '
          f'{max(lows):+.2f} m about it', file=sys.stderr)
    print(json.dumps({'level': round(level, 3), 'tide': tide}, indent=2))


if __name__ == '__main__':
    main()
