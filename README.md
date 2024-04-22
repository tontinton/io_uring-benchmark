## Why?

I wanted to see the difference between raw `io_uring` compared to Cloudflare's [pingora](https://github.com/cloudflare/pingora/blob/7ce6f4ac1c440756a63b0766f72dbeca25c6fc94/pingora-runtime/benches/hello.rs) non stealing task scheduler (single threaded tokio per core) and tokio's regular task stealing scheduler.

## What did I find out?

`io_uring` has better throughput and P50 latency, but worse P99 and max latencies.

`io_uring`:

```
  40 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.63ms    2.40ms  57.33ms   90.01%
    Req/Sec    22.13k     5.74k   99.39k    92.79%
  Latency Distribution
     50%  837.00us
     75%    2.00ms
     90%    4.03ms
     99%    9.73ms
  4478070 requests in 5.10s, 320.30MB read
  Socket errors: connect 19, read 0, write 0, timeout 0
Requests/sec: 877837.44
Transfer/sec:     62.79MB
```

`tokio`:

```
  40 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.37ms    1.14ms  24.87ms   82.29%
    Req/Sec    19.62k     4.69k  116.06k    93.26%
  Latency Distribution
     50%    1.03ms
     75%    1.70ms
     90%    2.81ms
     99%    5.56ms
  3952985 requests in 5.09s, 282.74MB read
  Socket errors: connect 19, read 0, write 0, timeout 0
Requests/sec: 776239.06
Transfer/sec:     55.52MB
```

pigora's single threaded `tokio` per core:

```
  40 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.34ms  499.93us   9.27ms   76.29%
    Req/Sec    18.15k     5.09k  207.43k    95.67%
  Latency Distribution
     50%    1.28ms
     75%    1.59ms
     90%    1.92ms
     99%    2.89ms
  3672542 requests in 5.10s, 262.68MB read
  Socket errors: connect 19, read 0, write 0, timeout 0
Requests/sec: 720086.93
Transfer/sec:     51.50MB
```

These were run on my machine ([System76 lemp11](https://tech-docs.system76.com/models/lemp11/README.html)).

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
