;; Linux arch/x86/kernel/traps.c:do_error_trap maps invalid opcode to SIGILL.
(module
 (import "wasi_snapshot_preview1" "proc_exit" (func (param i32)))
 (memory (export "memory") 1)
 (func (export "_start") unreachable))
