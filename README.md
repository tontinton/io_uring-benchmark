```sh
gcc main.c -O3 -luring -o main
./main

# On another terminal
wrk -t40 -c1000 -d10 http://127.0.0.1:3003 --latency
```
