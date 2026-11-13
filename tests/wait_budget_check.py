"""Compile the draw wait-budget checks without a GPU or game files."""
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    local = ROOT / 'toolchain/launcher-msys/toolchain/msys64/ucrt64/bin'
    standard = Path('C:/msys64/ucrt64/bin')
    bins = next((p for p in (standard, local) if (p / 'clang.exe').is_file()), None)
    cc = os.environ.get('CC') or (str(bins / 'clang.exe') if bins else shutil.which('clang'))
    if not cc:
        raise SystemExit('MSYS2 UCRT64 clang or CC is required')
    work = ROOT / 'build/test/wait_budget'
    work.mkdir(parents=True, exist_ok=True)
    exe = work / ('wait_budget_check.exe' if os.name == 'nt' else 'wait_budget_check')
    env = os.environ.copy()
    if bins:
        env['PATH'] = str(bins) + os.pathsep + env.get('PATH', '')
    subprocess.run([cc, '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'runtime'),
                    str(ROOT / 'tests/wait_budget_check.c'), '-o', str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env)


if __name__ == '__main__':
    main()
