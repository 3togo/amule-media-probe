# RC4 loop alternatives (issue #1606)

`rc4bench.cpp` is a standalone C++17 benchmark of the original production byte
loop and cumulative alternatives:

- `wide-locals`: unsigned 32-bit local indices, retaining byte state.
- `cached`: also retain the swapped values when computing the output index.
- `split`: also separate encryption from keystream discard. This is the
  production candidate in `src/RC4Encrypt.cpp`.
- `unroll4`: also unroll encryption four bytes at a time.
- `wide-state`: use a 32-bit state table with the `split` loop; no unrolling.

All variants use the same fixed 16-byte key and KSA. Each checks its output
against the original byte loop, including a 1024-byte discard. The unit test
additionally checks the actual production class against Crypto++.

Build and run separately, with other heavy jobs stopped where possible:

```sh
g++ -std=c++17 -O2 -Wall -Wextra experiments/rc4/rc4bench.cpp -o /tmp/rc4bench-gcc
/tmp/rc4bench-gcc
clang++-18 -std=c++17 -O2 -Wall -Wextra experiments/rc4/rc4bench.cpp -o /tmp/rc4bench-clang
/tmp/rc4bench-clang
```

On Linux, optionally pin each run with `taskset -c <available-cpu>`. Run the
compilers and binaries sequentially. Output is CSV; each value is the median
of seven runs. Bulk throughput uses decimal MB/s and 32 MiB per sample. TCP
setup includes KSA and a 1024-byte discard. The UDP workload includes KSA,
4-byte and 1-byte calls, an 8-byte discard, and a 100-byte payload. Setup
workloads report thousands of operations per second. The benchmark excludes
buffer construction, MD5, sockets and application overhead.

On Linux, timing uses process CPU time, excluding time descheduled by the OS. This
reduces scheduling noise on a busy desktop but does not remove contention,
frequency scaling or thermal effects. These are loop measurements, not an
application upload profile. The noinline annotation supports GCC, Clang and MSVC. CPU clock failures
and zero-duration samples are rejected; clock semantics vary by platform.

The production candidate keeps the existing 258-byte key state. Widening the
whole table would increase it to 1032 bytes. Neither the wire format nor
key setup/discard policy changes.

## Historical exploratory measurements

CPU: Intel Core i7-7500U (Kaby Lake), Linux x86-64. GCC 15.3.0 and Clang
18.1.8, `-O2`, pinned to CPU 3, run sequentially. This was a busy desktop;
treat the numbers as provisional, not a cross-platform performance guarantee.
Raw results are in `gcc-15-O2.csv` and `clang-18-O2.csv`.

| Compiler / variant | 180 KiB MB/s | 10 KiB MB/s | 1300 B MB/s | UDP kdatagrams/s | TCP kkeys/s |
|---|---:|---:|---:|---:|---:|
| GCC / baseline | 237.8 | 240.0 | 240.4 | 821.7 | 288.9 |
| GCC / split | 285.1 | 290.7 | 287.7 | 842.0 | 298.8 |
| GCC / unroll4 | 361.4 | 358.1 | 359.7 | 908.5 | 288.7 |
| GCC / wide-state | 268.4 | 264.6 | 275.9 | 850.7 | 293.1 |
| Clang / baseline | 264.8 | 255.6 | 259.1 | 1045.4 | 308.1 |
| Clang / split | 351.6 | 358.4 | 357.9 | 1202.1 | 373.2 |
| Clang / unroll4 | 354.5 | 370.1 | 384.8 | 1063.3 | 304.6 |
| Clang / wide-state | 346.2 | 327.3 | 361.4 | 1055.8 | 359.2 |

The exploratory `split` variant showed about 20–21% higher bulk throughput
with GCC and 33–40% with Clang in these runs. These duplicated-loop results
do not establish a production or application speedup.
Widening the table brought no consistent advantage over that variant.
Unrolling improved bulk throughput further, particularly with GCC, but its
Clang setup workloads were worse than `split` and TCP setup was slightly
below baseline. The smaller loop is the conservative candidate here.

Before claiming a general gain, repeat on an otherwise idle host and on AMD,
ARM64 and MSVC, and profile an uploading daemon. No end-to-end upload CPU
reduction has been measured here.

Validation: `RC4Test` passed against the original loop before the production
change and against the selected loop afterward. It checks fixed outputs at
keystream offsets 0 and 1024, differential behavior against Crypto++ with
multiple MD5-derived keys, chunking, in-place/out-of-place calls, explicit
skips, and buffer reset/rekey behavior. The public API only accepts 16-byte
MD5 keys, so the issue's 5-byte RFC vector is not injected through a private
test seam.

The affected test target and the non-MULEUNIT production translation unit
compiled successfully with the existing Debug configuration. Formatting
passed with clang-format-18. clang-tidy-18 reported no project diagnostics
for the production source or new test; its temporary configuration removed
only `ExcludeHeaderFilterRegex`, which LLVM 18 does not support. The repository
lint configuration was left unchanged.


## Paired production benchmark

Use a Release configuration. The two executables link the identical public-API
harness with the actual production source or the original source snapshot
in `RC4EncryptBaseline.cpp`, which records its source commit. This snapshot
is used only by the opt-in benchmark. Separate translation units avoid
benchmark-only inlining of the cipher into its caller.

```sh
cmake -S . -B /tmp/amule-rc4-release -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DAMULE_BUILD_RC4_BENCHMARKS=ON
cmake --build /tmp/amule-rc4-release \
  --target RC4ProductionBenchmark RC4BaselineBenchmark RC4Test -j "$(($(nproc)-1))"
ctest --test-dir /tmp/amule-rc4-release -R '^RC4Test$' --output-on-failure
python3 experiments/rc4/compare-production.py \
  /tmp/amule-rc4-release/unittests/tests/RC4BaselineBenchmark \
  /tmp/amule-rc4-release/unittests/tests/RC4ProductionBenchmark \
  --output /tmp/rc4-production-samples.csv
```

For multi-configuration generators, build with `--config Release`, run CTest
with `-C Release`, and use the executable paths under the `Release` directory.
Debug timings are unsuitable for performance claims. The targets use the
configuration's normal optimization flags, avoiding incompatible MSVC Debug
optimization/runtime-check options.

The runner alternates execution order for seven paired samples per workload,
saves all samples, and reports median rates and median/minimum/maximum paired
throughput ratios. It rejects mismatching stream checksums. These checksums
are sanity checks; unit tests provide independent byte-for-byte verification.
On Linux, optionally prefix the runner with `taskset -c <available-cpu>`.
Do not overlap timing with compiler, sanitizer or lint jobs. A busy host can
still invalidate comparisons; inspect the sample spread.

Bulk workloads encrypt 64 MiB in place in 180 KiB, 10 KiB and 1300-byte calls.
TCP measures reset, key setup and 1024-byte discard. UDP measures reset, key
setup without discard, 4-byte and 1-byte calls, 8-byte discard and 100-byte
payload. MD5 is outside timing. Measurements use wall-clock elapsed time,
decimal MB/s for bulk, and thousands of operations/s for setup.

The expanded regression tests also observe the output after the final skip,
exercise mixed calls and null input with a non-null untouched output buffer,
and verify the zero-key constants with Crypto++. Their Crypto++ includes
use the configured prefix and the project's warning wrappers.

## Production validation results

Local Release build: Intel Core i7-7500U, Linux x86-64, GCC 15.3.0, CMake's
normal `-O3 -DNDEBUG` configuration. The timing runner was pinned to CPU 3.
Our compiler, lint and sanitizer jobs had finished before timing. The desktop
still had other activity, so wall-time variation remains substantial.

The first wall-time-only run (`production-gcc15-release-*.csv`) showed a
1300-byte median ratio of 0.931. A 15-pair repeat with both CPU and wall clocks
(`production-gcc15-1300-repeat-*.csv`) showed every pair faster and a median
ratio of 1.231 for both clocks. We retained the initial results rather than
silently discarding the apparent regression.

The final seven-pair run across all workloads is recorded in
`production-gcc15-confirmed-samples.csv` and
`production-gcc15-confirmed-summary.csv`. Ratios are production/baseline
throughput; greater than one means faster.

| Workload | Median paired wall ratio | Median paired CPU ratio |
|---|---:|---:|
| 180 KiB | 1.093 | 1.092 |
| 10 KiB | 1.071 | 1.112 |
| 1300 B | 1.057 | 1.162 |
| TCP setup | 1.034 | 1.015 |
| UDP datagram | 1.027 | 1.027 |

There was no median slowdown in the final run. Individual pairs still varied
in both directions, so these results do not establish a universal gain or
exclude small platform-specific regressions. All paired stream checksums
matched. The current runner reports both wall and CPU timing; process CPU
clock semantics described here apply to Linux and can differ on other OSes.
Use `--workloads 1300 --samples 15` to repeat just the small-buffer workload.

All five unit cases passed in Debug and Release, and against both the original
and modified cipher with AddressSanitizer and UndefinedBehaviorSanitizer.
Relevant test/cipher/buffer sources were instrumented; third-party libraries
were not rebuilt with sanitizers. The test target no longer compiles Format
and strerror helpers already supplied by mulecommon, eliminating the duplicate
symbols observed in the instrumented link. The alternate `crypto++` include
prefix compiled under Clang using an isolated generated config header.
