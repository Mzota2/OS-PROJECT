# OS Project Testing Guide

This document defines repeatable tests for the current bare-metal kernel.

## 1) Build Validation

Run:

```bash
make clean
make
```

Pass criteria:
- Build completes with no errors.
- `kernel.elf` and `os.iso` are generated.

## 2) Boot Stability Test (Blinking/Unstable Load Regression)

Run:

```bash
make run
```

Observe for at least 20 seconds.

Pass criteria:
- GRUB boots into kernel consistently.
- Screen content remains stable (no reboot loop/flicker/reset behavior).
- Tick counter increments steadily.
- System does not unexpectedly restart.

Note:
- `make run` now launches QEMU with `-no-reboot -no-shutdown` to make instability visible instead of auto-restarting.

## 3) PIT Timing Sanity Test

Goal: verify low requested PIT rate no longer causes unstable rapid updates.

Run:

```bash
make run
```

Pass criteria:
- Timer-driven UI updates appear steady, not chaotic/blinking.
- No visible rapid overwrite artifact caused by bad PIT divisor programming.

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
