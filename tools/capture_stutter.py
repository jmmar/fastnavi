"""Launch a built game with bounded flight capture, then summarize completed captures.

python tools/capture_stutter.py C:/Games/NX2navi/ocarina.exe [--detail] [--visuals]
No graphics settings or installed files are changed. Diagnostics affect this child process only.
"""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import sys

from stutter_report import report

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--detail', action='store_true', help='enable performance tracing for a short follow-up run')
    parser.add_argument('--visuals', action='store_true', help='record thumbnails; adds GPU copies and about 106 MiB of ring memory')
    args = parser.parse_args()
    exe = args.game.resolve()
    if not exe.is_file() or exe.name.lower() not in ('ocarina.exe', 'nx2navi.exe', 'ocarina', 'nx2navi'):
        parser.error('give the built game executable, not the launcher or setup')
    # The launcher's stub shares one name with the old game; require a build's data alongside it.
    if not (exe.parent / 'romfs').exists() or not (exe.parent / 'exefs').is_dir():
        parser.error('expected the built game folder with romfs and exefs beside the executable')
    output = ROOT / 'build' / 'diagnostics' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    output.mkdir(parents=True)
    env = os.environ.copy()
    settings = {'NX2NAVI_FLIGHT': '1', 'NX2NAVI_FLIGHT_FRAMES': '120' if args.visuals else '600',
                'NX2NAVI_FLIGHT_THUMBS': '1' if args.visuals else '0', 'NX2NAVI_FLIGHT_SPIKE_MS': '40',
                'NX2NAVI_FLIGHT_EVERY': '0', 'NX2NAVI_FLIGHT_AT': '', 'NX2NAVI_FLIGHT_DIR': str(output),
                'NX2NAVI_TRACE': 'perf' if args.detail else '', 'NX2NAVI_PROFILE': '',
                'NX2NAVI_CAPTURE_DIR': str(output / 'bug-captures')}
    env.update(settings)
    # Only diagnostic settings are recorded: never copy the full environment or settings file.
    (output / 'capture.json').write_text(json.dumps({'started': datetime.now().isoformat(),
        'game': str(exe), 'diagnostics': settings}, indent=2), encoding='utf-8')
    print(f'Capture folder: {output}', flush=True)
    print('Play for 1-2 minutes. F8 makes a bug capture with picture/settings/save and flight timing; wait a few seconds before exiting.', flush=True)
    print('Automatic captures are limited to one every 10 seconds. Instrumentation and dumping have some overhead.', flush=True)
    # Running directly preserves the installed game and uses its existing configuration/cache/save paths.
    code = subprocess.call([str(exe)], cwd=exe.parent, env=env)
    (output / 'exit.json').write_text(json.dumps({'exit_code': code}), encoding='utf-8')
    try:
        summary = report(output)
    except ValueError as error:
        print(error)
    else:
        (output / 'summary.md').write_text(summary, encoding='utf-8')
        print(summary)
    return code


if __name__ == '__main__':
    sys.exit(main())
