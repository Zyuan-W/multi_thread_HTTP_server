## Assignment 4 directory

This project created a Multi-Threaded HTTP Server, that can process request and send response concurrency.

## notes
cited code from cited from cse130_resources/practica/asgn4-starter.

## Files
```
Makefile
README.md
asgn2_helper_funcs.h
asgn4_helper_funcs.a
connection.h
debug.h
httpserver.c
queue.h
request.h
response.h
```

## Design
Using a circle queue as bunded buffer and flock and mutex to impletement thread safe.

## Build
```
run $make to build httpserver
```

## Run
```
./httpserver -t threads port
```

