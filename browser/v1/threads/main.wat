
(module
  (import "env" "memory" (memory 1 1 shared))
  (import "wasix_32v1" "thread_spawn_v2" (func $spawn (param i32 i32) (result i32)))
  (import "wasi" "thread-spawn" (func $wasi_spawn (param i32) (result i32)))
  (import "wasix_32v1" "futex_wait" (func $wait (param i32 i32 i32 i32) (result i32)))
  (import "wasix_32v1" "futex_wake" (func $wake (param i32 i32) (result i32)))
  (import "wasix_32v1" "futex_wake_all" (func $wake_all (param i32 i32) (result i32)))
  (import "wasix_32v1" "thread_parallelism" (func $parallelism (param i32) (result i32)))
  (import "wasix_32v1" "thread_id" (func $thread_id (param i32) (result i32)))
  (import "wasix_32v1" "proc_exit2" (func $exit (param i32)))
  (func (export "wasi_thread_start") (param $tid i32) (param $arg i32)
    (i32.atomic.store (local.get $arg) (local.get $tid))
    (i32.atomic.store (i32.const 200) (i32.add (i32.atomic.load (i32.const 200)) (i32.const 1)))
    (drop (call $wake_all (i32.const 200) (i32.const 308))))
  ;; Waits until the counter at 200 reaches `n`.
  (func $await (param $n i32)
    (block $done
      (loop $again
        (br_if $done (i32.ge_u (i32.atomic.load (i32.const 200)) (local.get $n)))
        (if (call $wait (i32.const 200) (i32.atomic.load (i32.const 200)) (i32.const 0) (i32.const 304))
          (then (call $exit (i32.const 20))))
        (br $again))))
  (func (export "_start")
    (if (call $spawn (i32.const 100) (i32.const 104)) (then (call $exit (i32.const 1))))
    (call $await (i32.const 1))
    ;; the tid the call returned is the one the thread started with
    (if (i32.ne (i32.atomic.load (i32.const 100)) (i32.load (i32.const 104)))
      (then (call $exit (i32.const 2))))
    (drop (call $thread_id (i32.const 108)))
    (if (i32.eq (i32.load (i32.const 104)) (i32.load (i32.const 108)))
      (then (call $exit (i32.const 3))))
    ;; wasi-threads' form returns the tid
    (if (i32.le_s (call $wasi_spawn (i32.const 112)) (i32.const 0)) (then (call $exit (i32.const 4))))
    (call $await (i32.const 2))
    ;; a timeout of 1ms: not woken
    (i32.store8 (i32.const 408) (i32.const 1))
    (i64.store (i32.const 416) (i64.const 1000000))
    (if (call $wait (i32.const 400) (i32.const 0) (i32.const 408) (i32.const 304))
      (then (call $exit (i32.const 5))))
    (if (i32.load8_u (i32.const 304)) (then (call $exit (i32.const 6))))
    ;; the value is not the expected one: woken
    (if (call $wait (i32.const 400) (i32.const 1) (i32.const 0) (i32.const 304))
      (then (call $exit (i32.const 7))))
    (if (i32.eqz (i32.load8_u (i32.const 304))) (then (call $exit (i32.const 8))))
    ;; nobody waits
    (if (call $wake (i32.const 400) (i32.const 304)) (then (call $exit (i32.const 9))))
    (if (i32.load8_u (i32.const 304)) (then (call $exit (i32.const 10))))
    (if (call $parallelism (i32.const 312)) (then (call $exit (i32.const 11))))
    (if (i32.eqz (i32.load (i32.const 312))) (then (call $exit (i32.const 12))))
    (call $exit (i32.const 0))))
