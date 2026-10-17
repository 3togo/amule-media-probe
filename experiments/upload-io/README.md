# Upload disk baseline — issue #1586

This is temporary measurement instrumentation, before any prefetch implementation.
No CSV is opened and no per-read clock is sampled unless `AMULE_UPLOAD_IO_PROFILE`
is set to a nonempty output path. The file is overwritten at startup. An output
open/write failure logs a message and disables profiling without stopping uploads.
Only the upload disk thread writes the CSV. No peer identifiers or file paths are recorded.

## First screening run

1. Configure and build the instrumented application:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_MMAP=ON
   cmake --build build --target amule -j "$(($(nproc)-1))"
   ```

   Start with **MMapEnabled** off and keep it fixed throughout the run. Current upstream also calls `CFileArea::Prefault()`
   inside the measured read path, so separate mmap-enabled runs include page
   faults. The summary script rejects a run that mixes mmap modes. Record the
   build options along with the runtime setting.
2. On a machine with known HDD storage, prepare many active upload slots serving
   different shared files. Record the commit/local patch, filesystem, storage,
   slot count, upload cap, dataset size, RAM and download activity.
3. Start device monitoring in a separate terminal:

   ```sh
   iostat -x -y 1 > /tmp/upload-iostat.txt
   ```

4. Launch the application with an absolute output path on a different device or
   tmpfs where possible, so profiling writes do not compete with shared-file reads:

   ```sh
   AMULE_UPLOAD_IO_PROFILE=/dev/shm/upload-cold.csv ./build/src/amule
   ```

5. Collect a fixed observation window (for example 5 minutes once slots are busy),
   note aMule's sustained upload rate, and stop aMule normally. Summarize:

   ```sh
   python3 experiments/upload-io/summarize.py /dev/shm/upload-cold.csv
   ```

Repeat under warm cache with the same settings and workload. Start measurement
runs with slots already arranged where practical: startup and waiting for peers
count toward wall time and can depress rates. `bytes_per_second` is disk-thread
payload read throughput, **not** actual network upload throughput.

For cold cache, use a dedicated Linux test machine. After preparing the data,
`sync` followed by `echo 3 | sudo tee /proc/sys/vm/drop_caches` drops system caches;
this affects other workloads on that machine. Record precisely when you do it.
Do not interpret a small cached dataset as a cold disk baseline.

## What is measured

Each attempted read records steady-clock nanoseconds. The timer starts immediately
before `CPartFile::ReadData()` (including its mutex acquisition), or before the
complete-file `ReadAt()` after the file has opened. It ends after
`CFileArea::Prefault()` and `CFileArea::CheckError()`. Complete-file close time is included, matching the
existing scope. Validation/open failures before the timer starts are not reads.
Returns or exceptions after timing starts count as failed/deferred attempts,
separate from successful latency percentiles and bytes. Their time is included
in the read share.

Each disk-thread iteration records active time from before the upload-list lock
through completion processing and throttler notification, ending before CSV
serialization and the condition-variable wait. Thus idle sleep and CSV writes
are excluded from active time; mutex waits, packet building and compression are
included. On current upstream, disk reads run without the upload-list and
block-list locks; part-file reads still acquire the part-file handle mutex.
Successful latency percentiles use the nearest-rank definition. Rates
use wall time since profiler startup through the last completed loop, including
sleep and CSV writes. Read rows are buffered until that loop ends and share its
end timestamp. The CSV is flushed once per loop; stop normally before analyzing.

Instrumentation still has clock, sample-storage and flush overhead. It is a
screening tool, not a zero-overhead benchmark. CSV grows with the number of reads;
watch tmpfs/container memory use and copy results out after each run.

## Decision and subsequent validation

| Run | p50/p95/p99 ms | Reads/s | Read MiB/s | Read % active | Upload MiB/s | r_await | aqu-sz | %util |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HDD cold | pending | | | | | | | |
| HDD warm | pending | | | | | | | |

If read stalls are small relative to active work, stop and report that evidence
on the issue. If reads dominate and upload delivery is disk-limited, proceed to
a capped prefetch prototype. A high read share alone is insufficient: a network
upload cap can mask any disk benefit. No universal numeric cutoff is assumed.

Before merging an optimization, extend the same baseline and comparison to
HDD, RAID and SSD, cold/warm cache, with/without concurrent downloads, and a
memory-limited container, as required by the issue. Keep datasets, settings and
observation windows comparable. Do not infer an HDD gain from a synthetic SSD run.

No representative baseline results are included yet. A machine with known
rotating storage and a controlled many-peer workload is needed to fill the table.

Check the instrumentation and summary tool without a running aMule:

```sh
python3 experiments/upload-io/test_profile.py
```
