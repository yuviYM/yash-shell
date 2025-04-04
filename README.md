# yash: Yet Another SHell

**Author**: Yuvraj Maheshwary  
**Language**: C (compiled on Linux using GCC)

---

## Overview

`yash` is a simplified command-line shell built entirely in C, designed to mimic key features of modern Unix shells like `bash`. It supports job control, I/O redirection, signal handling, and single-level command piping.

This project was built from scratch for the EE461S Operating Systems course to demonstrate understanding of Unix process control, signals, pipes, and terminal behavior.

---

## Features

- **Job Control**
  - `jobs` — view background and stopped jobs
  - `fg` — bring most recent/stopped job to foreground
  - `bg` — resume a stopped job in the background
- **Foreground/Background Execution**
  - Commands can be run in the background using `&`
- **Signal Handling**
  - `Ctrl-C` (SIGINT): kills foreground job
  - `Ctrl-Z` (SIGTSTP): stops foreground job
  - `SIGCHLD`: reaps zombie processes
- **Redirection Support**
  - `>` for stdout
  - `<` for stdin
  - `2>` for stderr
- **Piping**
  - Single `|` supported for piping between two commands
- **Prompt**
  - Custom prompt: `# `
- **Environment Search**
  - Looks up commands via `PATH` environment variable
- **Clean Exit**
  - Handles `Ctrl-D` (EOF) to exit gracefully

---

## Build/Usage Instructions

Make sure you are on a Linux machine and have `gcc` installed.

```bash
make
./yash
```

Use it as a normal shell, here are a few examples

```bash
# run in foreground
ls -la

# run in background
sleep 10 &

# check jobs
jobs

# bring last job to foreground
fg

# stop a job with Ctrl-Z
# resume it in background
bg

# redirection
cat < input.txt > output.txt 2> error.txt

# single pipe
cat file.txt | grep "hello"
```

---

## References

- Linux man pages (execvp, waitpid, kill, tcsetpgrp, etc.)
- GNU C documentation
- Bash behavior used as a reference




