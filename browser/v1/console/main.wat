;; Same tty_get/tty_set ABI as wasix1/tests/proc.rs; Linux drivers/tty/n_tty.c:n_tty_set_termios.

    (module
      (import "wasix_32v1" "tty_get" (func $get (param i32) (result i32)))
      (import "wasix_32v1" "tty_set" (func $set (param i32) (result i32)))
      (import "wasix_32v1" "proc_exit2" (func $exit (param i32)))
      (memory (export "memory") 1)
      (func (export "_start")
        (if (call $get (i32.const 100)) (then (call $exit (i32.const 1))))
        (if (i32.ne (i32.load (i32.const 100)) (i32.const 80)) (then (call $exit (i32.const 2))))
        (if (i32.ne (i32.load (i32.const 104)) (i32.const 25)) (then (call $exit (i32.const 3))))
        ;; stdin, stdout and stderr are terminals; echo and line buffering are on
        (if (i32.ne (i32.load (i32.const 116)) (i32.const 0x01000101))
          (then (call $exit (i32.const 4))))
        (if (i32.eqz (i32.load8_u (i32.const 120))) (then (call $exit (i32.const 5))))
        ;; raw: echo and line buffering off
        (i32.store16 (i32.const 119) (i32.const 0))
        (if (call $set (i32.const 100)) (then (call $exit (i32.const 6))))
        (i32.store16 (i32.const 119) (i32.const 0x0101))
        (if (call $get (i32.const 100)) (then (call $exit (i32.const 7))))
        (if (i32.load16_u (i32.const 119)) (then (call $exit (i32.const 8))))
        (call $exit (i32.const 0))))
    