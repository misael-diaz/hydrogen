# sonic-httpd
Sonic Speed HTTP Server

## Build

Build the server from source code with GCC:

```sh
gcc -Wall -Wextra -Wformat -g -gdwarf-4 -O0 main.cpp -o sonic-httpd.bin
```

## Run

You can run this server in the usual way:

```sh
./sonic-httpd.bin
```

## Testing

This is how we are testing the http-server at the moment:

```sh
curl http://127.0.1.1:8080 --output -
```

for the time being we need to pass the `--output -` argument to `curl` so that it won't complain about displaying binary output on the console.
