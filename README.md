## Why?

I wanted to see the difference between raw `io_uring` compared to Cloudflare's [pingora](https://github.com/cloudflare/pingora/blob/7ce6f4ac1c440756a63b0766f72dbeca25c6fc94/pingora-runtime/benches/hello.rs) non stealing task scheduler (single threaded tokio per core) and tokio's regular task stealing scheduler.

## What did I find out?

`io_uring` has better throughput and P50 latency, but worse P99 and max latencies.

`io_uring`:

```
  40 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.15ms    3.19ms 101.03ms   89.10%
    Req/Sec     8.86k     2.00k   46.60k    92.77%
  Latency Distribution
     50%    2.28ms
     75%    4.23ms
     90%    6.54ms
     99%   13.22ms
  1790291 requests in 5.10s, 128.05MB read
  Socket errors: connect 19, read 0, write 0, timeout 0
Requests/sec: 351146.31
Transfer/sec:     25.12MB
```

`tokio`:

```
  40 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.27ms    2.13ms  25.17ms   74.63%
    Req/Sec     7.67k     1.83k   33.47k    94.60%
  Latency Distribution
     50%    2.76ms
     75%    4.24ms
     90%    6.18ms
     99%   10.24ms
  1547893 requests in 5.09s, 110.71MB read
  Socket errors: connect 19, read 0, write 0, timeout 0
Requests/sec: 304181.03
Transfer/sec:     21.76MB
```

pigora's single threaded `tokio` per core:

```
  40 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.41ms    1.39ms  16.23ms   72.82%
    Req/Sec     7.11k     1.77k   35.11k    95.84%
  Latency Distribution
     50%    3.29ms
     75%    4.17ms
     90%    5.08ms
     99%    7.59ms
  1433327 requests in 5.10s, 102.52MB read
  Socket errors: connect 19, read 0, write 0, timeout 0
Requests/sec: 281234.92
Transfer/sec:     20.12MB
```

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
