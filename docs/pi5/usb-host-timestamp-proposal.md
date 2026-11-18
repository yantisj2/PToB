# First-register timestamp feasibility proposal

Copyright (c) 2026 Jonathan Yantis. MIT.

This is an **offline proposal**, not an enabled observer or authorization to
instrument the running gateway. Current work changes no tracing controls,
kernel code/modules, firmware, clock writers or serving services. The existing
read-only observer remains the supported live path.

## Question and existing limit

Does most of the measured runtime-view read interval occur before or after the
specific MFINDEX MMIO read? The native helper removed Python inter-read work,
but its syscall still invokes the complete eight-register view. A fitted
midpoint, minimum syscall time or dividing the interval by eight cannot answer
that question or timestamp the first register.

The [Raspberry Pi xHCI source](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/drivers/usb/host/xhci-debugfs.c)
places MFINDEX first in the runtime regset and registers it through
`debugfs_create_regset32`. The
[generic debugfs implementation](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/fs/debugfs/file.c)
uses `debugfs_regset32_show`, runtime-PM get/put, and `debugfs_print_regs32`.
The latter loops over register `readl` plus formatting. Function entry/exit
therefore brackets multiple reads and formatting, not a single MFINDEX sample.
Reading fewer output bytes does not identify an earlier timestamp inside that
show invocation.

## Concrete offline feasibility work

Check the existing static hooks first. Generic
[`readl` tracing](https://github.com/torvalds/linux/blob/v6.18/include/asm-generic/io.h)
can call `rwmmio_read` before the access and `rwmmio_post_read` after I/O ordering,
with [address/width/value fields](https://github.com/torvalds/linux/blob/v6.18/include/trace/events/rwmmio.h).
Those sites require `CONFIG_TRACE_MMIO_ACCESS` at compile time. Architecture
support or general tracing support does not establish that the hooks exist in
the installed binary. Missing compiled hooks cannot be enabled by a runtime
filter; a rebuild is outside this experiment. Existing hooks would still mark
software MMIO observations, not physical USB SOFs or an atomic cross timestamp.

1. Identify the **exact installed kernel build**, configuration and matching
   debug/binary identity. The reviewed public branch is not proof of installed
   instruction offsets. Obtain matching disassembly offline; retain source,
   binary/build-id and tool provenance. No offset is proposed from a different
   kernel build.
2. Locate the first `readl` and its ordering instructions in that binary's
   `debugfs_print_regs32`, including compiler inlining/alternatives. The
   [arm64 I/O implementation](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/arch/arm64/include/asm/io.h)
   uses inline MMIO loads and ordering machinery; a source-level function name
   alone does not identify the executed read. Verify the regset/base against
   the already guarded controller and first-register offset without issuing
   extra MMIO or bypassing its driver.
3. Determine whether the exact build supports **two instruction-location
   observations surrounding that read and required ordering**, carrying the
   captured value and a unique invocation identity. Function-only
   [fprobes](https://docs.kernel.org/trace/fprobetrace.html) cannot isolate an
   inlined read. [Kprobe events](https://docs.kernel.org/trace/kprobetrace.html)
   support symbol offsets subject to configuration and architecture limits;
   their feasibility is not established merely by naming offsets. Probes also
   [alter execution and add overhead](https://docs.kernel.org/trace/kprobes.html),
   so no unobserved zero-cost timestamp can be assumed.
4. Specify the clock before choosing a mechanism. Linux6.18
   [perf clock selection](https://github.com/torvalds/linux/blob/v6.18/kernel/events/core.c)
   maps `CLOCK_MONOTONIC_RAW` to `ktime_get_raw_fast_ns`; support and actual
   sample placement still require validation on the installed build. The
   ordinary [`bpf_ktime_get_ns`](https://github.com/torvalds/linux/blob/v6.18/include/uapi/linux/bpf.h)
   helper describes MONOTONIC, not RAW. It cannot silently replace the present
   RAW clock domain. A trace clock or additional cross-clock fit does not
   itself close physical phase.

An exact build/symbol/instruction/clock finding is the next deliverable. No
live probes, perf events, BPF program or tracefs writes are part of this work.
If the two locations cannot be identified and safely observed on the stock
build, retain the present complete syscall bracket and record that limit.

The offline `host_probe_plan.py` validator checks a pinned recorded function
digest and a declared load/ordering byte interval, then computes all-loop hit
budgets and explicitly assumed CPU-cost scenarios. It installs no probes and
does not declare instruction probing safe. Its public tests use synthetic
instruction fixtures; exact kernel identities/addresses/fragments remain in
ignored evidence. Passing byte checks establishes identity only.

The upstream6.18
[arm64 decoder](https://github.com/torvalds/linux/blob/v6.18/arch/arm64/kernel/probes/decode-insn.c)
accepts ordinary nonliteral, nonexclusive loads for out-of-line stepping, but
also scans the preceding atomic context. That is only source-level feasibility:
the exact installed decoder, exception tables and blacklist remain prerequisites.
The [arm64 implementation](https://github.com/torvalds/linux/blob/v6.18/arch/arm64/kernel/probes/kprobes.c)
replaces the original instruction with a breakpoint and executes the copied
instruction followed by another breakpoint. It masks interrupt state during
that single-step path. Therefore even a stock instruction probe changes live
kernel text and execution timing; it is not a passive register read. The
pre-load timestamp would precede trap return and out-of-line execution, while
the later timestamp would include additional trap handling. No bound for that
observer delay follows from opcode acceptance. Current scope enables neither
probe; further work here remains offline. Historical x86 probe-overhead examples
are not a Pi5 bound, and record filtering cannot remove the execution cost.

An alternative stock API is
[`PERF_TYPE_BREAKPOINT`](https://man7.org/linux/man-pages/man2/perf_event_open.2.html),
which uses CPU debug registers for instruction/data breakpoints. This avoids
the copied-instruction mechanism, but does not make an event timestamp a hardware
USB timestamp. The upstream6.18
[arm64 implementation](https://github.com/torvalds/linux/blob/v6.18/arch/arm64/kernel/hw_breakpoint.c)
rejects **per-task kernel breakpoints** in `hw_breakpoint_arch_parse`. Thus a
proposed reader-PID-scoped kernel instruction pair is not supported by that
path. Per-CPU kernel breakpoints use debug exceptions and single stepping;
other callers on that CPU and every register-loop iteration still matter.
The generic [breakpoint parser](https://github.com/torvalds/linux/blob/v6.18/kernel/events/hw_breakpoint.c)
also requires privileged kernel-address access and available slots. Installed
hardware-breakpoint configuration/slots, exact implementation, sample placement,
losses and perturbation remain unverified. This alternative stays an offline
feasibility finding; no perf event is opened or CPU debug control changed.

## Later discriminating experiment, if separately scoped

The candidate would observe only the existing guarded reader's first-register
instruction pair, using the same read budgets and CPU affinity. It would not
perform another register read or capture another device's activity. Each pair
must join the exact invocation/value and satisfy containment inside its saved
outer RAW syscall bracket. Preserve unmatched/nested pairs, migrations, buffer
losses, missing values, every wide bracket, complete polling cost and release.
Probe misses invalidate the affected association; they are not zero error.

The compiled loop is an important feasibility constraint: the **same load and
ordering instructions execute for every register**. Two instruction probes can
therefore incur16 hits per eight-register show, even if a process/address filter
records only MFINDEX. At the existing hard cap of1024 reads/s that is up to
16,384 probe hits/s, before any other callers of the shared generic routine.
Filtering captured records does not remove global trap/handler cost. Record
all such overhead and avoid collecting unrelated register/device contents.
This can make an otherwise narrower observation unsuitable at the permitted
load. Exact offsets are build-specific evidence, not reusable operating commands.

Use fixed short populations before/during/after observation to quantify added
load and compare contemporaneous cached M-HAT/PPS health. The outer and proposed
inner intervals must be compared on common invocations and denominators. The
outcome is a distribution of **first-MMIO-read brackets**, not a new midpoint
accuracy claim. Acceptability requires demonstrably narrower complete brackets
with no concealed misses or tail/observer regression.

Even success would timestamp a controller-register observation. It would not
calibrate MFINDEX-to-full-speed-wire SOF phase, integrated TT/PHY/device capture
bias, source UTC or GPIO electrical delay. Preserve integer-origin evidence,
the dependent frozen provisional correction and unknown absolute uncertainty.
An independent physical reference remains necessary for those terms; this
proposal cannot promote USB to chrony or demonstrate parity by itself.
