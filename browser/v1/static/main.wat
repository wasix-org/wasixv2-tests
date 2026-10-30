
(module
  (import "wasi_snapshot_preview1" "fd_write" (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 8) "\10\00\00\00\0c\00\00\00")
  (data (i32.const 16) "hello world\n")
  (func (export "_start")
    (if (call $fd_write (i32.const 1) (i32.const 8) (i32.const 1) (i32.const 32))
      (then (call $proc_exit (i32.const 1))))
    ;; nwritten
    (if (i32.ne (i32.load (i32.const 32)) (i32.const 12))
      (then (call $proc_exit (i32.const 2))))
    (call $proc_exit (i32.const 7))))
