"""Summarize existing flight-recorder TSVs without loading thumbnails or game data."""
import argparse
import csv
import math
import statistics
from pathlib import Path

WAITS = ('pace_ms', 'fence_ms', 'drain_ms', 'pipe_ms', 'xlate_ms', 'tex_ms', 'astc_ms')
COUNTS = ('shaders_xlated', 'pipe_built', 'pipe_waited', 'oneshot_waited',
          'draws_skip_pipe', 'draws_skip_shader', 'tex_new', 'tex_up', 'tex_reup')


def load(root):
    paths = [root] if root.is_file() else sorted(root.rglob('frames.tsv'))
    frames = {}
    for path in paths:
        # F8 writes a small timing table beside the detailed flight table; do not summarize both.
        if (path.parent / 'flight' / 'frames.tsv').is_file():
            continue
        session = next((parent for parent in path.parents if (parent / 'capture.json').is_file()), path.parent.parent)
        with path.open(encoding='utf-8-sig') as stream:
            for row in csv.DictReader((line for line in stream if not line.startswith('#')), delimiter='\t'):
                try:
                    values = {k: float(v) for k, v in row.items() if k and v is not None}
                    if not all(math.isfinite(v) for v in values.values()):
                        raise ValueError('non-finite value')
                    key = int(values['frame'])
                    if values['interval_ms'] < 0:
                        raise ValueError('negative interval')
                except (ValueError, KeyError) as error:
                    raise ValueError(f'{path}: invalid frame row: {error}') from error
                # Sibling dumps overlap; separate capture sessions restart frame numbers.
                identity = (session, key)
                source = path.parent.parent.name if path.parent.name == 'flight' else path.parent.name
                frames[identity] = (source, values)
    return list(frames.values())


def evidence(frame):
    leads = []
    for key, label in (('pipe_ms', 'pipeline wait'), ('xlate_ms', 'shader translation'),
                       ('tex_ms', 'texture upload/decode'), ('fence_ms', 'GPU-fence wait'),
                       ('drain_ms', 'queue drain')):
        if frame.get(key, 0) >= 2:
            leads.append(f'{label} {frame[key]:.1f} ms')
    counts = [f'{key}={int(frame[key])}' for key in COUNTS if frame.get(key, 0)]
    return '; '.join(leads + counts) or 'no recorded work identifies the cause; deeper capture needed'


def report(root, threshold=40, top=15):
    frames = load(root)
    if not frames:
        raise ValueError('No completed frames.tsv captures found. Wait at least 30 presents after a trigger, then exit normally.')
    values = sorted(row['interval_ms'] for _, row in frames)
    slow = sorted(((source, row) for source, row in frames if row['interval_ms'] >= threshold),
                  key=lambda pair: pair[1]['interval_ms'], reverse=True)
    lines = ['# Stutter capture summary', '',
             f'{len(frames)} distinct captured frames; {len(slow)} at or above {threshold:g} ms.',
             f'Captured median: {statistics.median(values):.2f} ms; worst: {values[-1]:.2f} ms.', '',
             'These are selected windows around triggers, not whole-session FPS or percentiles.',
             'Counts are correlations, not proof of causation. Waits come from render/replay instrumentation;',
             'they do not identify CPU utilization or prove the main game thread is blocked.',
             'ASTC time is included in texture time: do not add them. Other waits may also overlap.', '',
             '| Capture / frame | Interval | Recorded evidence |',
             '| --- | ---: | --- |']
    for source, row in slow[:top]:
        lines.append(f"| {source} / {int(row['frame'])} | {row['interval_ms']:.2f} ms | {evidence(row)} |")
    lines += ['', '## Next measurement', '',
              '- Repeat the same pot/water/route immediately, then after restarting. Keep settings and frame generation unchanged.',
              '- Pipeline/translation waits or skipped draws: inspect cache preparation and the matching game log first.',
              '- Texture time with new uploads: investigate streaming/decode; compare a repeated route before changing memory knobs.',
              '- Fence/drain waits: these establish waiting, not whether shader execution, contention or submission caused it.',
              '- Unexplained spikes: use a short --detail capture next. Sample CPU threads only if that still leaves CPU work unexplained.',
              '- White flashes: use a short --visuals capture, F8 at the flash, and inspect texture/pipeline skip events near it.',
              '- A regular spike interval alone does not establish a periodic CPU task. Capture several events before forming that conclusion.', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--threshold-ms', type=float, default=40)
    parser.add_argument('--top', type=int, default=15)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if not math.isfinite(args.threshold_ms) or args.threshold_ms <= 0 or args.top < 1:
        parser.error('threshold must be positive and finite; top must be positive')
    try:
        text = report(args.capture, args.threshold_ms, args.top)
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')
    if args.out:
        args.out.write_text(text, encoding='utf-8')
    print(text)


if __name__ == '__main__':
    main()
