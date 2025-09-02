# Dynamic Memory Allocator

A high-performance 64-bit dynamic memory allocator implementation using segregated free lists with optimized memory utilization and throughput.

## Overview

This project implements a custom dynamic memory allocator (`malloc`, `free`, `realloc`, `calloc`). The implementation uses a sophisticated segregated free list data structure with mini-block optimization to achieve high performance in both memory utilization and allocation throughput.

## Key Features

- **Segregated Free Lists**: 14 size-class buckets for efficient free block management
- **Mini-Block Optimization**: Special handling for minimum-sized blocks (16 bytes)
- **Coalescing**: Immediate coalescing of adjacent free blocks
- **64-bit Support**: Full support for 64-bit address space
- **High Performance**: Optimized for both throughput and memory utilization
- **Comprehensive Testing**: Extensive test suite with real-world and synthetic traces

## Architecture

### Block Structure
- **Header**: Contains size, allocation status, and metadata about previous block
- **Payload**: User data area or free list pointers (for free blocks)
- **Footer**: Present only for free non-mini blocks
- **Alignment**: 16-byte aligned blocks

### Memory Layout
```
Allocated Block:    Free Block (>16 bytes):    Mini Block (16 bytes):
┌─────────────┐    ┌─────────────┐            ┌─────────────┐
│   Header    │    │   Header    │            │   Header    │
├─────────────┤    ├─────────────┤            ├─────────────┤
│             │    │ Prev Pointer│            │ Next Pointer│
│   Payload   │    ├─────────────┤            └─────────────┘
│             │    │ Next Pointer│
├─────────────┤    ├─────────────┤
│   Footer    │    │             │
│ (if needed) │    │   Unused    │
└─────────────┘    │             │
                   ├─────────────┤
                   │   Footer    │
                   └─────────────┘
```

### Segregated List Classes
| Class | Size Range | Description |
|-------|------------|-------------|
| 0     | ≤ 32       | Small blocks |
| 1     | 33-64      | |
| 2     | 65-128     | |
| 3     | 129-256    | |
| 4     | 257-512    | |
| 5     | 513-1024   | Medium blocks |
| 6     | 1025-2048  | |
| 7     | 2049-3072  | |
| 8     | 3073-4096  | |
| 9     | 4097-6656  | |
| 10    | 6657-8192  | Large blocks |
| 11    | 8193-16384 | |
| 12    | 16385-32768| |
| 13    | >32768     | Extra large blocks |

## Building and Testing

### Prerequisites
- LLVM/Clang compiler
- Make build system
- Perl (for testing scripts)

### Build Commands
```bash
# Build all drivers
make

# Build without instrumented versions (for compatibility)
make all-but-instrumented

# Clean build artifacts
make clean
```

### Available Drivers
- **`mdriver`**: Standard performance testing driver
- **`mdriver-dbg`**: Debug version with optimization disabled and debug output
- **`mdriver-emulate`**: 64-bit address space emulation for correctness testing
- **`mdriver-uninit`**: Memory sanitizer version for detecting uninitialized memory

### Running Tests

#### Basic Testing
```bash
# Run all default traces
./mdriver

# Run with verbose output
./mdriver -V

# Run specific trace file
./mdriver -f traces/syn-array-short.rep

# Get help with available options
./mdriver -h
```

#### Debug Testing
```bash
# Run with debug information
./mdriver-dbg

# Test 64-bit correctness
./mdriver-emulate

# Check for uninitialized memory usage
./mdriver-uninit
```

#### Performance Testing
```bash
# Run performance benchmark
./driver.pl

# Calibrate throughput benchmarks
./calibrate.pl
```

## Test Traces

The `traces/` directory contains various test cases:

### Real-World Traces
- **`bdd-*.rep`**: Binary Decision Diagram operations
- **`cbit-*.rep`**: Constraint generation for BDD checker
- **`ngram-*.rep`**: N-gram counting from text processing

### Synthetic Traces
- **`syn-array*.rep`**: Array allocation patterns
- **`syn-string*.rep`**: String manipulation patterns
- **`syn-struct*.rep`**: Structure allocation patterns
- **`syn-mix*.rep`**: Mixed allocation patterns
- **`syn-giant*.rep`**: Large allocation testing (64-bit)

### Debug Traces
- **`*-short.rep`**: Shortened versions for debugging

## Performance Metrics

The allocator is evaluated on two main criteria:

### Memory Utilization
- **Target**: >55% minimum, up to 74% for maximum points
- **Formula**: `(Total Payload) / (Heap Size)`
- **Weight**: 60% of total score

### Throughput
- **Target**: >50% of reference implementation minimum
- **Measurement**: Operations per second (malloc/free/realloc)
- **Weight**: 40% of total score

### Performance Index
```
Performance Index = (UTIL_WEIGHT × Utilization) + ((1 - UTIL_WEIGHT) × Throughput)
```

## Implementation Details

### Core Functions

#### `mm_init()`
- Initializes heap with prologue and epilogue
- Sets up segregated free list array
- Initializes mini-block list
- Extends heap with initial free block

#### `malloc(size_t size)`
- Adjusts size for alignment and overhead
- Searches appropriate size class using best-fit strategy
- Extends heap if no suitable block found
- Splits large blocks when beneficial
- Updates allocation metadata

#### `free(void *ptr)`
- Marks block as free
- Updates adjacent block metadata
- Coalesces with neighboring free blocks
- Inserts into appropriate free list

#### `realloc(void *ptr, size_t size)`
- Handles edge cases (NULL ptr, zero size)
- Allocates new block and copies data
- Frees original block
- Optimizes for common reallocation patterns

#### `calloc(size_t nmemb, size_t size)`
- Allocates array of elements
- Initializes memory to zero
- Handles overflow detection

### Optimization Strategies

1. **Segregated Lists**: Reduces search time by organizing free blocks by size
2. **Mini-Block List**: Special handling for 16-byte blocks reduces overhead
3. **Immediate Coalescing**: Prevents fragmentation by merging adjacent free blocks
4. **Best-Fit Search**: Balances utilization and performance within size classes
5. **Block Splitting**: Minimizes internal fragmentation
6. **Metadata Optimization**: Packs allocation info in headers to reduce overhead

## Debugging and Validation

### Heap Checker (`mm_checkheap`)
Validates heap consistency:
- Prologue/epilogue integrity
- Block alignment and boundaries
- Header/footer consistency
- Free list pointer validity
- Coalescing correctness
- Size class organization

### Debug Macros
```c
dbg_printf(...)     // Debug output
dbg_requires(expr)  // Precondition checking
dbg_ensures(expr)   // Postcondition checking
dbg_assert(expr)    // General assertions
```

### Memory Sanitizers
- **AddressSanitizer**: Detects buffer overflows and use-after-free
- **MemorySanitizer**: Detects uninitialized memory reads
- **UndefinedBehaviorSanitizer**: Catches undefined behavior

## Configuration

Key parameters in `config.h`:
- `ALIGNMENT`: 16-byte alignment requirement
- `MAX_DENSE_HEAP`: 100MB maximum heap size
- `MIN_SPACE`: 55% minimum utilization threshold
- `MAX_SPACE`: 74% maximum utilization target
- `UTIL_WEIGHT`: 60% weight for utilization in scoring

## Files Structure

```
├── mm.c                    # Main allocator implementation
├── mm.h                    # Allocator interface
├── mm-naive.c             # Simple reference implementation
├── mdriver.c              # Test driver
├── memlib.c/h             # Heap simulation library
├── config.h               # Configuration parameters
├── Makefile               # Build system
├── traces/                # Test trace files
│   ├── README             # Trace format documentation
│   ├── *-short.rep        # Debug traces
│   ├── syn-*.rep          # Synthetic traces
│   ├── bdd-*.rep          # BDD traces
│   ├── cbit-*.rep         # Constraint traces
│   └── ngram-*.rep        # N-gram traces
├── inst/                  # Instrumentation plugins
└── helper.mk              # Additional make rules
```

## License

Copyright (c) 2002, 2016, R. Bryant and D. O'Hallaron, All rights reserved.  
May not be used, modified, or copied without permission.

---

*This implementation demonstrates advanced memory management techniques and serves as an educational example of high-performance system programming.*
