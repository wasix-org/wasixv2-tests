
(module
  (import "wasi_snapshot_preview1" "fd_write" (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasix_32v1" "proc_signals_sizes_get" (func $count (param i32) (result i32)))
  (import "wasix_32v1" "proc_signals_get" (func $signals (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 8) "\10\00\00\00\05\00\00\00")
  (data (i32.const 16) "child")
  (func (export "_start") (local $i i32) (local $ignored i32)
    (drop (call $fd_write (i32.const 1) (i32.const 8) (i32.const 1) (i32.const 32)))
    (drop (call $count (i32.const 40)))
    (drop (call $signals (i32.const 64)))
    ;; (signal, disposition) pairs; IGNORE is 1
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $i) (i32.load (i32.const 40))))
        (if (i32.eq (i32.load8_u (i32.add (i32.const 65) (i32.shl (local.get $i) (i32.const 1)))) (i32.const 1))
          (then (local.set $ignored (i32.add (local.get $ignored) (i32.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $next)))
    (call $exit (i32.add (i32.const 3) (local.get $ignored)))))
