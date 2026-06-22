# Limit Order Book & Matching Engine

A C++20 Limit Order Book (LOB) and matching engine designed for deterministic nanosecond latency. This engine achieves a peak synthetic processing rate of **19 million packets per second (pps)**.

**Latency:** ~52 nanoseconds per packet.

## Core Architecture & Features

### 1. Zero-Copy Network Ingress (Kernel Bypass)
Standard POSIX sockets invoke heavy context switches and TCP/IP stack overhead. This engine utilizes `AF_PACKET` with `PACKET_MMAP` to map the Network Interface Card (NIC) Rx Ring buffer directly into userspace virtual memory. 
* UDP multicast traffic is filtered at the hardware/driver level using a Classic BPF attached directly to the socket.

### 2. Lock-Free Concurrency Bridge
The architecture strictly isolates network I/O from execution logic to prevent dropped packets during microbursts.
* **Thread Affinity:** Ingress and Execution threads are pinned to isolated CPU cores using `pthread_setaffinity_np`.
* **SPSC Ring Buffer:** Bridged via a custom Single-Producer/Single-Consumer queue utilizing strict `std::memory_order_acquire` and `std::memory_order_release` semantics, optimized to reduce cache coherence overhead.
* **Cache Line Alignment:** Structs are padded with `alignas(64)` to completely eradicate false sharing across the CPU ring bus interconnect. Consumer spin-loops utilize `_mm_pause()` to improve performance.

### 3. O(1) Deterministic State Engine
No new allocations occur in the hot path by preallocating memory upon initialization, preventing latency spikes due to malloc() calls.
* **Memory Management:** Utilizes custom, `mmap`-backed contiguous virtual memory pools for node allocation.
* **BBO Tracking:** Best Bid and Offer tracking is achieved in true $O(1)$ time utilizing a 3-layer hierarchical bitset heavily accelerated by x86 hardware intrinsics `__builtin_ctzll`.

### 4. Zero-Copy Crash Recovery (Write-Ahead Log)
Resilience is maintained without sacrificing hot-path latency.
* **Mmap WAL:** State is persisted to a 1GB memory-mapped Write-Ahead Log. Standard `write()` syscalls (which cost ~283ns) are replaced with direct memory stores to mmap-ed addresses, reducing payload appends to **11ns**.
* **Torn-Write Protection:** Utilizes Intel CRC32 checksums (`_mm_crc32_u64`) and non-temporal memory hints to bypass the CPU cache entirely, writing directly to NVMe.
* **Recovery:** Achieves deterministic LOB reconstruction of 16.7 million frames in `<150ms`, bound strictly by raw sequential disk bandwidth.

## Performance Benchmarks
Tested on Intel i7-7700 Chip without OS optimizations such as Explicit HugePages or tickless kernels.

| Metric | Result |
| :--- | :--- |
| **Peak Throughput** | 19,000,000 pps |
| **Ingress Latency** | 52 ns / packet |
| **WAL Append Latency**| 11 ns / payload |
| **Full State Recovery** | < 150 ms (16.7M frames) |

## Future Architecture Advancements
* **Asynchronous Checkpointing:** Currently, crash recovery builds the order book entirely from scratch by replaying the Write-Ahead Log. This makes recovery time O(N) based on the number of frames processed. To fix this, the architecture will evolve to support periodic checkpointing. By running a secondary replica node that consumes the same log, we can save the exact state of the order book to disk at regular intervals without blocking the main execution thread. Upon a crash, the system will simply load the latest snapshot and only replay the few messages that arrived after it, significantly reducing recovery time.

* **Pre-Trade Risk Gateway:** The current engine processes all inbound traffic assuming it is safe. We need to introduce a risk evaluation layer before the order book. This gateway will inspect incoming ITCH messages (e.g., abnormally large order sizes), rejecting invalid orders before they are ever processed by the matching engine.