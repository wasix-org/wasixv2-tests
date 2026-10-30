(module
        (import "wasix_http_client_v1" "client::new" (func $new (param i32)))
        (import "wasi_snapshot_preview1" "proc_exit" (func $exit (param i32)))
        (memory (export "memory") 1)
        (func (export "canonical_abi_realloc") (param i32 i32 i32 i32) (result i32) i32.const 1024)
        (func (export "_start")
            (call $new (i32.const 0))
            (if (i32.ne (i32.load8_u (i32.const 0)) (i32.const 1)) (then unreachable))
            (if (i32.ne (i32.load (i32.const 8)) (i32.const 24)) (then unreachable))
            ;; "No http client available" in the guest's allocated error string.
            (if (i32.ne (i32.load (i32.load (i32.const 4))) (i32.const 1746956110)) (then unreachable))
            (call $exit (i32.const 0))))
