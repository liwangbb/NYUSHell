# NYUShell — OS Labs (Shell, Encoder, FAT32)

**Overview**
- Course projects implementing a Unix-like shell, a multithreaded run-length encoder, and a FAT32 recovery tool.
- Focuses on systems programming in C: processes, signals, pipes, threads, synchronization, memory-mapped I/O, and low-level file system manipulation.

**Repo Structure**
- `Shell-structure/`: Interactive shell `nyush` with job control, pipes, and redirection.
- `Shell-multithred/`: Parallel run-length encoder `nyuenc` with a thread pool.
- `Shell-fat32/`: FAT32 tool `nyufile` to inspect the file system and recover deleted files.

**Build**
- Prereqs: `gcc`, `make`; POSIX environment (Linux/macOS). For `nyufile`, OpenSSL crypto (`-lcrypto`).
- Build each component:
  - `cd Shell-structure && make`
  - `cd Shell-multithred && make`
  - `cd Shell-fat32 && make`

**nyush (Shell-structure)**
- Features: prompt `[nyush <cwd>]$`, builtins `cd`, `exit`, `jobs`, `fg <index>`; pipelines `|`; redirection `<`, `>`, `>>`; foreground job control; robust error handling.
- Signals: shell ignores `SIGINT`, `SIGQUIT`, `SIGTSTP`; restored to default in children. Stopped jobs are tracked and resumable with `fg`.
- Usage:
  - Run: `./nyush`
  - Examples:
    - `ls -l | grep '^d' > dirs.txt`
    - `cat < input.txt | sort | uniq -c >> stats.txt`
    - `sleep 100` then press `Ctrl+Z`, run `jobs`, then `fg 1`.

**nyuenc (Shell-multithred)**
- What: Multithreaded Run-Length Encoding (RLE) that concatenates encoded outputs across files, preserving global runs across chunk boundaries.
- Concurrency: Thread pool (POSIX threads), mutex + condition variable for a task queue, per-task semaphores to stream results to stdout in-order as they become ready.
- I/O: Input files memory-mapped (`mmap`) read-only; output written as binary pairs `<byte><count>` with `count` in `[1,255]` (long runs split correctly).
- Chunking: Files split into 4KB tasks; merging logic coalesces runs spanning adjacent chunks/files.
- Flags: `-j <N>` to set worker threads (default 1).
- Usage examples:
  - Single-thread: `./nyuenc input1 > out.enc`
  - Parallel: `./nyuenc -j 8 fileA fileB fileC > out.enc`

**nyufile (Shell-fat32)**
- What: FAT32 inspector and recovery utility operating directly on a disk image via `mmap`.
- Options:
  - `-i`: Print FS info (FAT count, bytes/sector, sectors/cluster, reserved sectors).
  - `-l`: List root directory entries (files/dirs, sizes, starting clusters) with 8.3 name validation.
  - `-r <filename> [-s <sha1>]`: Recover a deleted, contiguous file; optional SHA-1 disambiguates candidates.
  - `-R <filename> -s <sha1>`: Recover a possibly non-contiguous file using SHA-1-guided search.
- Recovery: Restores directory entry name, starting cluster, and updates all FAT copies; uses OpenSSL SHA1 for content verification.
- Usage examples:
  - Inspect: `./nyufile fat32.disk -i` and `./nyufile fat32.disk -l`
  - Recover contiguous: `./nyufile fat32.disk -r FILE.TXT -s da39a3...`
  - Recover non-contiguous: `./nyufile fat32.disk -R FILE.TXT -s <40-hex-sha1>`

**Design Highlights**
- Shell:
  - Tokenization via `strtok`, argv construction, and `execvp` for external programs.
  - Multi-stage pipelines using `pipe()` arrays and `dup2()`; per-segment child setup; redirections only at ends of pipelines as specified.
  - Job control with a simple job table; `waitpid(..., WUNTRACED)` to detect stops; `SIGCONT` to resume.
- Encoder:
  - Task queue of 4KB segments; workers encode segments; a writer thread merges sequential results to preserve runs across boundaries.
  - Poison-pill task/result to signal termination; bounded run counts with correct splitting at 255.
- FAT32:
  - Direct struct mapping of Boot/Dir entries; cluster math for data region addressing; FAT updates propagated to all copies.
  - 8.3 name handling/validation; SHA-1 verification for ambiguity resolution.

**Assumptions & Limits**
- Shell: basic parsing (space-delimited), no quoting/escaping; redirection at the pipeline ends; max args and job table sizes are bounded.
- Encoder: chunk size 4KB; max file count and total size bounded by queue limits; output format is binary pairs.
- FAT32: targets 8.3 names; root directory only; sample search bounds for non-contiguous recovery are limited (e.g., small unallocated window, small file cluster count) consistent with course spec/autograder.

**Testing & Artifacts**
- Provided autograder inputs/reference outputs are included under each component’s `*-autograder/` or `refoutputs/` folders for comparison.

**References**
- Shell: GNU libc manual (job control), POSIX `open/dup2/execvp/signal`, IBM docs for `getcwd`.
- Encoder: pthreads, condition variables, semaphores, RLE references; task-queue/thread-pool approach.
- FAT32: OSDev FAT docs, FAT32 boot sector notes, OpenSSL SHA1 usage, 8.3 filename spec.

