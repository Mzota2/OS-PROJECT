# 🖥️ Custom Operating System

A from-scratch OS kernel built with x86-64 assembly and C, running on bare metal via QEMU.

---

## 🧠 Part 1: What Has Been Built

### Step 1: QEMU Starts a Virtual Computer

When you run:
```bash
qemu-system-x86_64 -cdrom os.iso
```

QEMU creates a virtual x86_64 machine with:
- Virtual CPU
- Virtual RAM  
- Virtual BIOS
- Virtual disk

**You are literally booting a fake computer.**

### Step 2: BIOS → GRUB Boot Sequence

The virtual BIOS looks at the ISO file and locates:
```
/boot/grub/grub.cfg
```

GRUB reads this config and:
- Loads your kernel into memory
- Jumps to its entry point

**GNU GRUB is your bootloader — it initializes the machine before your code runs.**

At this point:
- ❌ Windows is irrelevant
- ❌ Ubuntu is irrelevant  
- ✅ You are running inside a virtual machine

### Step 3: Kernel Loaded at 1MB

In `linker.ld`:
```nasm
. = 0x100000;  /* Load address (1MB) */
```

This tells GRUB to load the kernel at memory address `0x100000` because:
- Early PCs reserve lower memory for system use
- 1MB is a safe, standard location

GRUB then:
1. Loads kernel into RAM
2. Sets CPU to 32-bit protected mode (Multiboot2 standard)
3. Jumps to `_start`

### Step 4: Execution Starts at `_start`

In `boot.asm`:
```asm
_start:
    cli                 ; Disable interrupts
    call kernel_main    ; Enter kernel code
```

**This is your operating system's first instruction.**

At this moment you have:
- ❌ No scheduler
- ❌ No interrupts
- ❌ No multitasking
- ❌ No memory management

Just raw CPU execution — **you are in full control of hardware.**

### Step 5: Writing Directly to Video Memory

In `kernel.c`:
```c
volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;

void print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        vga_buffer[i] = (0x0F << 8) | str[i];
    }
}
```

What is `0xB8000`?
- That is a physical memory address mapped to VGA text mode
- When you write to it, you are writing directly to hardware memory

This demonstrates:
- ❌ No printf
- ❌ No operating system
- ❌ No drivers
- ❌ No library

**You directly control memory — that is what an OS does.**

---

## 🎯 What Has Actually Been Achieved

✅ Created a freestanding program that runs without any operating system  
✅ Bypassed Windows, Linux, and any existing OS  
✅ Booted your own kernel  

**That is huge.**

---

## 🚀 What's Missing?

Right now your system:
- Runs one function
- Prints once
- Hangs forever
- Cannot handle keyboard input
- Cannot handle timer interrupts
- Cannot switch tasks

It's just a single-threaded program running in kernel space.

---

## 🚀 Part 2: What Comes Next

Now we move toward making it a real operating system.

### Next Step 1: Interrupt Descriptor Table (IDT)

Right now:
- CPU error → crash
- Timer fires → ignored
- Keyboard pressed → ignored

You must build the IDT, which tells the CPU:
> "When interrupt X happens, jump to function Y"

This is the foundation of multitasking.

### Next Step 2: Timer Interrupt

You configure the PIT (Programmable Interval Timer):
- Timer fires every few milliseconds
- CPU pauses current code and jumps to your handler
- This gives you periodic execution and preemption capability

**Without a timer, you cannot build a scheduler.**

### Next Step 3: Context Switching

Right now CPU runs only one stack. To switch tasks you must:

**Save:**
- `RSP` (stack pointer)
- `RIP` (instruction pointer)
- All CPU registers

**Load:**
- Another task's saved registers

This is called **context switching** — the core of multitasking.

### Next Step 4: Scheduler

Implement round-robin scheduling:
```
Task A → Task B → Task A → Task B → ...
```

Task switches happen on every timer interrupt.

**Now your OS can run multiple tasks simultaneously.**

### Next Step 5: System Calls

Right now everything runs in kernel mode. System calls allow:
- User programs → Request kernel services

Examples:
- `write()` - output
- `spawn()` - create process
- `exit()` - terminate

This requires:
- Ring transitions (user ↔ kernel mode)
- Controlled entry into kernel
- **Makes your OS structured and secure**

---

## 🧠 Big Picture: How an OS Actually Works

The execution flow looks like:
```
Bootloader → Kernel → Interrupts → Scheduler → Processes → System Calls
```

You have completed:
✅ Bootloader → Kernel

Next:
⏳ Interrupts → Scheduler → Syscalls

---

## 🎓 Why This Matters

After completing this project you will understand:
- How Windows switches tasks
- How Linux handles system calls
- Why segmentation faults happen
- What context switching truly means
- **Memory protection and privilege levels**

**Very few developers understand this level of systems design.**

---

## 🔥 Important Reality

**The moment your timer interrupt works and switches two tasks, that's when you truly understand operating systems.**

It is a major mental shift from application programming to systems programming.

---

## 📌 Progress Tracker

- [✅] Booting your own kernel
- [ ] Handling interrupts
- [ ] Scheduling tasks
- [ ] Implementing syscalls
- [ ] Memory management
- [ ] File system

**You are at 20% of the real journey.**

## 🛠️ How to Build & Run

```bash
make clean
make              # Build the OS
make run          # Run in QEMU
```

The OS will load and display output to the VGA text buffer at address `0xB8000`.
