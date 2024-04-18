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
