# Testing Your OS Kernel: A Beginner's Guide

This guide helps you test the kernel step-by-step. Testing ensures your code works and teaches you how the OS behaves. We'll use QEMU (the simulator) and check for expected results.

**Why Test?** OS code is tricky – bugs can crash the whole system. Testing builds confidence and helps learning.

## Before Testing: Build the Kernel

Always build first:

```bash
make clean  # Remove old files
make        # Compile code
```

Look for "successfully" at the end. If errors, fix them (check code for typos).

## Test 1: Does It Build and Boot?

**What it tests:** Basic setup – can the kernel start?

**How to run:**
```bash
make run
```

**What to look for (in VGA window):**
- GRUB menu appears briefly, then "Custom OS Kernel" on screen.
- "Shell ready" appears (row 6).
- No crashes or restarts.

**Pass:** Screen stays stable for 20+ seconds. A counter (ticks) increases.

**Fail:** Screen flickers, reboots, or freezes. Check serial output (in terminal) for error messages.

**Why important:** If boot fails, nothing else works. Like checking if your car starts.

## Test 2: Does the Timer Work?

**What it tests:** Interrupt handling – does the timer send signals correctly?

**How to run:**
```bash
make run
```

**What to look for:**
- Steady updates, not fast blinking.
- Serial output shows "[INIT] pit_init() completed".

**Pass:** Screen updates smoothly, no rapid flickering.

**Fail:** Chaotic updates. Means timer interrupts are misconfigured.

**Why important:** Timer drives task switching. Broken timer = no multitasking.

## Test 3: Do Tasks Switch?

**What it tests:** Scheduler – does it run multiple tasks?

**How to run:**
```bash
make run
```

**What to look for (VGA window):**
- Rows 3-5 show "Task A running", "Task B running", etc., switching.
- Serial: "[TASK] A/B/C start" messages.

**Pass:** Tasks switch every few seconds.

**Fail:** Only one task runs, or none.

**Why important:** Multitasking is key to OSes. Like ensuring all students get turns.

## Test 4: Does Keyboard Input Work?

**What it tests:** Input handling – can you type and see responses?

**How to run:**
```bash
qemu-system-i386 -cdrom os.iso -no-reboot -no-shutdown
```

**What to do:**
- Click VGA window to focus.
- Type keys (e.g., "abc").

**What to look for:**
- Row 2 shows "Keyboard: abc" (typed chars).
- Serial: "[KBD] Pressed: a" etc.

**Pass:** Input appears on screen.

**Fail:** No response. Keyboard interrupts broken.

**Why important:** User interaction. Without it, no shell.

## Test 5: Does the Shell Work?

**What it tests:** Command processing – does the text interface respond?

**How to run:** Same as Test 4.

**What to do:**
- Wait for "Shell ready".
- Type commands (one key at a time, no enter).

**Commands to try:**
- `h` or `H`: Shows help on row 7 (lists commands).
- `a`, `b`, `c`: Show info about tasks on rows 8-10.
- `w`: Tests syscall, adds 'A' to VGA row 8.
- `q`, `r`, `t`: Spawn new tasks (see more switching).
- `x`: Exits shell (system continues).
- `m`: Tests memory, shows PASS/FAIL on row 11.
- `c`: Clears rows 7-11.

**Pass:** Commands work as described. Unknown keys show "Unknown command".

**Fail:** No output or wrong responses.

**Why important:** Shell lets you interact. Tests syscalls and task management.

## Test 6: Do Syscalls Work?

**What it tests:** System calls – can tasks ask kernel for services?

**How to run:** Same as Test 5.

**What to do:**
- In shell, type `w` (write), `q/r/t` (spawn), `x` (exit).

**What to look for:**
- `w`: 'A' appears on VGA.
- `q/r/t`: More task switching (serial shows new tasks).
- `x`: Shell stops, other tasks continue.

**Pass:** All work without crashes.

**Fail:** Errors or no effect.

**Why important:** Syscalls are how programs use OS services.

## Test 7: Does Memory Management Work?

**What it tests:** Memory allocation – can kernel give out RAM?

**How to run:** Same as Test 5.

**What to do:**
- Type `m` in shell.

**What to look for:**
- Row 11: "Memory test: PASS" (allocation works).
- Or "FAIL" (corruption), "OOM" (out of memory).

**Pass:** PASS appears.

**Fail:** FAIL or OOM (if heap too small).

**Why important:** Memory bugs crash systems. Like managing hotel rooms.

## Test 8: Does Preemptive Scheduling Work?

**What it tests:** Forced task switching – does kernel switch automatically?

**How to run:**
```bash
make run
```

**What to look for (serial output):**
- "[SCHED] preempt triggered" every ~5-10 seconds.

**Pass:** Messages appear regularly.

**Fail:** No messages, or only manual switches.

**Why important:** Ensures fair multitasking. Without it, one task could hog CPU.

## Troubleshooting

- **No output:** Check QEMU focus (click window).
- **Crashes:** Look at serial logs for clues (e.g., "undefined reference").
- **Slow:** QEMU might be emulating slowly.
- **Build fails:** Check for missing tools (`sudo apt install nasm qemu-system-x86`).

## Next Steps

Once all tests pass, try modifying code:
- Change timer speed in `pit_init(18)` – see what happens.
- Add a new shell command.
- Experiment with memory limits.

Remember: Testing is learning! If stuck, check README.md or ask for help.
- File system tests
- Multi-user tests

## 4) Scheduler Task Rotation Test

Run:

```bash
make run
```

Pass criteria:
- Task rows for A, B, C are visible.
- Syscall output row receives A/B/C characters over time.
- Kernel remains responsive (no hang/reboot) while tasks rotate cooperatively.

Current scheduler mode:
- Rotation currently uses cooperative `scheduler_yield()` in task loops.
- Verify all tasks continue to rotate and emit syscall output under timer IRQ load.

## 5) Keyboard IRQ Smoke Test

Run:

```bash
make run
```

Then type in QEMU window.

Pass criteria:
- Keyboard input does not crash or reset kernel.
- Kernel continues ticking and switching tasks after key presses.

## 6) Exception Safety Test (Manual)

If adding new handlers, validate that unexpected faults do not cause silent reset loops.

Suggested run:

```bash
qemu-system-i386 -cdrom os.iso -no-reboot -no-shutdown
```

Pass criteria:
- On fatal fault, system halts predictably (or prints diagnostic), instead of immediate reboot cycling.

## 7) Recommended Debug Run

For deeper diagnostics:

```bash
qemu-system-i386 -cdrom os.iso -d int,cpu_reset -no-reboot -no-shutdown
```

Use this when investigating interrupt storms, triple faults, or reset loops.

## 8) WSL-Specific Test Commands (Required Environment)

Run all validation from Ubuntu WSL terminal:

```bash
cd ~/os-project
make clean
make
make run
```

If instability persists, run:

```bash
cd ~/os-project
make run-debug
```

Pass criteria:
- `qemu.log` is generated for diagnostics.
- No continuous CPU reset loop in log (look for repeated reset patterns).

## 9) Context Switch Integrity Test (Timer + Interrupt Frame)

Run:

```bash
make clean
make
make run-debug
```

Pass criteria:
- System boots and enters tasks without immediate fault.
- Task output continues while timer ticks advance.
- No repeated `cpu_reset` storm in `qemu.log`.
- Keyboard IRQ still works while task switching is active.
