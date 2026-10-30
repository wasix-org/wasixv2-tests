
(module
  (import "wasix_32v1" "proc_exec3" (func $exec (param i32 i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "./show")
  (data (i32.const 16) "show\nuser")
  (data (i32.const 32) "A=caller")
  (func (export "_start")
    (drop (call $exec (i32.const 0) (i32.const 6) (i32.const 16) (i32.const 9)
      (i32.const 32) (i32.const 8) (i32.const 0) (i32.const 0) (i32.const 0)))
    (call $proc_exit (i32.const 99))))
