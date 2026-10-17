#!/usr/bin/env python3
"""Offline checks for the baseline collector and derived metrics."""
import csv
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from summarize import summarize

ROOT = Path(__file__).resolve().parents[2]
HEADER = 'kind,elapsed_ns,duration_ns,bytes,success,part,mmap\n'


class SummaryTest(unittest.TestCase):
    def test_percentiles_rates_and_failed_time(self):
        data = HEADER
        for duration in range(1, 101):
            data += f'read,1000000000,{duration * 1000000},1024,1,0,0\n'
        data += 'read,1000000000,50000000,0,0,1,0\n'
        data += 'loop,1000000000,6000000000,0,0,0,0\n'
        result = summarize(io.StringIO(data))
        self.assertEqual([result[k] for k in ('p50_ms', 'p95_ms', 'p99_ms')], [50, 95, 99])
        self.assertEqual(result['reads_per_second'], 100)
        self.assertEqual(result['bytes_per_second'], 102400)
        self.assertEqual(result['failed_or_deferred_reads'], 1)
        self.assertEqual(result['read_share_active_percent'], 85)

    def test_mmap_mode_and_incomplete_runs(self):
        with self.assertRaisesRegex(ValueError, 'keep mmap mode fixed'):
            summarize(io.StringIO(HEADER + 'read,1,1,1,1,0,0\nread,1,1,1,1,0,1\n'))
        result = summarize(io.StringIO(HEADER + 'read,10,1,1,1,0,1\nloop,10,2,0,0,0,0\n'))
        self.assertTrue(result['mmap_enabled'])
        with self.assertRaisesRegex(ValueError, 'incomplete final loop'):
            summarize(io.StringIO(HEADER + 'read,1,1,1,1,0,0\n'))
        with self.assertRaisesRegex(ValueError, 'no completed loops'):
            summarize(io.StringIO(HEADER))


class CollectorTest(unittest.TestCase):
    def test_collector_lifecycle(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            source = directory / 'profile.cpp'
            binary = directory / 'profile'
            output = directory / 'profile.csv'
            source.write_text('''#include "UploadIOProfile.h"
int main() {
    UploadIOProfile profile;
    if (profile.Failed()) return 2;
    profile.BeginLoop();
    { UploadIOProfile::Read read(profile, 42, false, false);
      read.Start(); read.Finish(); }
    { UploadIOProfile::Read read(profile, 99, true, false);
      read.Start(); } // unsuccessful attempt (also used on exception/early return)
    { UploadIOProfile::Read read(profile, 1, false, false); }
    return profile.EndLoop() ? 0 : 3;
}
''')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src'), str(source), '-o', str(binary)], check=True)
            env = dict(os.environ)
            env.pop('AMULE_UPLOAD_IO_PROFILE', None)
            subprocess.run([binary], env=env, check=True)
            self.assertFalse(output.exists())
            env['AMULE_UPLOAD_IO_PROFILE'] = str(output)
            subprocess.run([binary], env=env, check=True)
            with output.open() as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual([row['kind'] for row in rows], ['read', 'read', 'loop'])
            self.assertEqual([row['success'] for row in rows[:2]], ['1', '0'])
            self.assertEqual([row['bytes'] for row in rows[:2]], ['42', '99'])
            self.assertGreaterEqual(int(rows[2]['duration_ns']),
                                    sum(int(row['duration_ns']) for row in rows[:2]))
            env['AMULE_UPLOAD_IO_PROFILE'] = str(directory / 'missing' / 'profile.csv')
            self.assertEqual(subprocess.run([binary], env=env).returncode, 2)
            if Path('/dev/full').exists():
                env['AMULE_UPLOAD_IO_PROFILE'] = '/dev/full'
                self.assertEqual(subprocess.run([binary], env=env).returncode, 3)


if __name__ == '__main__':
    unittest.main()
