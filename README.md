## Why?

I wanted to see the difference between raw `io_uring` compared to Cloudflare's [pingora](https://github.com/cloudflare/pingora/blob/7ce6f4ac1c440756a63b0766f72dbeca25c6fc94/pingora-runtime/benches/hello.rs) non stealing task scheduler (single threaded tokio per core) and tokio's regular task stealing scheduler.

## How?

For regular run:

```sh
gcc main.c -O3 -luring -o main
./main

# On another terminal
wrk -t40 -c1000 -d10 http://127.0.0.1:3003 --latency
```

For flamegraph:

```sh
gcc main.c -O3 -luring -g -fno-omit-frame-pointer -o main
perf record -g ./main

# On another terminal
wrk -t40 -c1000 -d10 http://127.0.0.1:3003 --latency

# ^C perf when done measuring and then:
./gen_flamegraph.sh

firefox out.svg
```
