# sonic-httpd
Sonic Speed HTTP Server

## Development

### Week 1

- **networking**: binds to tcp/ip socket address and listens on port 8080. Any other port may be used as well.
- **http-headers**: the server reads the http request header in chunks with lowlevel read calls. Since the tcp/ip socket is a stream socket reading fewer bytes than the chunk size mean that we have reached the end of the header. That's the reason why we don't need to check for the presence of the **CRLF** terminator.
- **http-response**: at the moment we respond with a minimal respond header, the client would know that there are no more bytes. This last aspect has been verified with both browsers such as firefox and the curl utility.
- **main loop**: adds the main loop that waits for incomming connections and then delegates the task of respoding to the client to a child process.
- **signal hanlding**: the server halts execution gracefully upon signal interrupts by registration of the respective signal handler. The child processes are setup to respond to the signal in the default way to avoid confusion (interrupting a child process should not affect the server, for instance see `man clone`).

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

## References:

https://www.rfc-editor.org/info/rfc9112/#message.body
