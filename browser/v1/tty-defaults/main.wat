
    (module
      (import "wasix_32v1" "tty_get" (func $get (param i32) (result i32)))
      (import "wasix_32v1" "tty_set" (func $set (param i32) (result i32)))
      (import "wasix_32v1" "proc_exit2" (func $exit (param i32)))
      (memory (export "memory") 1)
      (func (export "_start")
        (if (call $get (i32.const 100)) (then (call $exit (i32.const 1))))
        (if (i32.ne (i32.load (i32.const 100)) (i32.const 80)) (then (call $exit (i32.const 2))))
        (if (i32.ne (i32.load (i32.const 104)) (i32.const 25)) (then (call $exit (i32.const 3))))
        (if (i32.load8_u (i32.const 117)) (then (call $exit (i32.const 4))))
        ;; echo on
        (i32.store8 (i32.const 119) (i32.const 1))
        (if (call $set (i32.const 100)) (then (call $exit (i32.const 5))))
        (i32.store8 (i32.const 119) (i32.const 0))
        (if (call $get (i32.const 100)) (then (call $exit (i32.const 6))))
        (if (i32.ne (i32.load8_u (i32.const 119)) (i32.const 1)) (then (call $exit (i32.const 7))))
        (call $exit (i32.const 0))))
