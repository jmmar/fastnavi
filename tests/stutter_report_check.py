"""Check capture isolation and overlapping flight summaries, without launching a game."""
import contextlib
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import capture_stutter
import stutter_report


class CaptureChecks(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'build')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def frames(self, path, rows):
        path.mkdir(parents=True)
        (path / 'frames.tsv').write_text('# fixture\nframe\tinterval_ms\ttex_ms\tastc_ms\tpipe_ms\n' + rows,
                                        encoding='utf-8')

    def test_overlap_and_session_reset(self):
        self.frames(self.root / 'session1' / 'dump1', '1\t16\t0\t0\t0\n2\t200\t120\t90\t0\n')
        self.frames(self.root / 'session1' / 'dump2', '2\t200\t120\t90\t0\n3\t17\t0\t0\t0\n')
        self.frames(self.root / 'session2' / 'dump1', '1\t100\t0\t0\t60\n')
        text = stutter_report.report(self.root)
        self.assertIn('4 distinct captured frames; 2 at or above', text)
        self.assertIn('texture upload/decode 120.0 ms', text)
        self.assertIn('pipeline wait 60.0 ms', text)
        self.assertNotIn('210.0 ms', text)  # ASTC is nested inside texture time.
        self.assertIn('not whole-session FPS', text)

    def test_unknown_cause_stays_unknown(self):
        self.assertIn('no recorded work identifies', stutter_report.evidence({'interval_ms': 200}))

    def test_bug_capture_and_automatic_windows_share_session(self):
        (self.root / 'capture.json').write_text('{}', encoding='utf-8')
        self.frames(self.root / 'auto', '2\t200\t120\t90\t0\n')
        bug = self.root / 'bug-captures' / '001'
        self.frames(bug, '2\t200\t0\t0\t0\n')
        self.frames(bug / 'flight', '2\t200\t120\t90\t0\n')
        text = stutter_report.report(self.root)
        self.assertIn('1 distinct captured frames', text)
        self.assertIn('texture upload/decode 120.0 ms', text)

    def test_empty_and_damaged_input(self):
        with self.assertRaisesRegex(ValueError, 'No completed'):
            stutter_report.report(self.root)
        self.frames(self.root / 'broken', '1\tNaN\t0\t0\t0\n')
        with self.assertRaisesRegex(ValueError, 'non-finite'):
            stutter_report.report(self.root)

    def test_child_environment_isolated(self):
        game = self.root / 'ocarina.exe'
        game.touch()
        (self.root / 'romfs').touch()
        (self.root / 'exefs').mkdir()
        before = os.environ.copy()
        with patch.object(capture_stutter, 'ROOT', self.root), \
             patch.object(sys, 'argv', ['capture_stutter.py', str(game)]), \
             patch.object(capture_stutter.subprocess, 'call', return_value=0) as launch, \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(capture_stutter.main(), 0)
        options = launch.call_args.kwargs
        self.assertEqual(options['env']['NX2NAVI_FLIGHT_THUMBS'], '0')
        self.assertEqual(options['env']['NX2NAVI_TRACE'], '')
        self.assertEqual(options['cwd'], game.parent)
        self.assertEqual(os.environ.copy(), before)


if __name__ == '__main__':
    unittest.main()
