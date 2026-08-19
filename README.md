# sonic-httpd
Sonic Speed HTTP Server

## Development

### Week 1

- **networking**: binds to tcp/ip socket address and listens on port 8080. Any other port may be used as well.
- **http-headers**: the server reads the http request header in chunks with lowlevel read calls. Since the tcp/ip socket is a stream socket reading fewer bytes than the chunk size mean that we have reached the end of the header. That's the reason why we don't need to check for the presence of the **CRLF** terminator.
- **http-response**: at the moment we respond with a minimal respond header, the client would know that there are no more bytes. This last aspect has been verified with both browsers such as firefox and the curl utility.
- **main loop**: adds the main loop that waits for incomming connections and then delegates the task of respoding to the client to a child process.
- **signal hanlding**: the server halts execution gracefully upon signal interrupts by registration of the respective signal handler. The child processes are setup to respond to the signal in the default way to avoid confusion (interrupting a child process should not affect the server, for instance see `man clone`).
- **non-blocking**: the server is now able to respond to incoming connections in non-blocking mode. Still need to work on this so that the server is not put to sleep to keep the CPU usage low when there are no connections. Strongly considering to register a signal handler for `SIGIO`, I think it would be an interesting solution to implement because I have seen polling on the `xserver`.
- **async requests**: server responds to incoming requests asynchronously by means of signal handlers. The children still process the request in the usual way which is the right call.
- **response timestamp**: includes the response timestamp according to [RFC9110](https://www.rfc-editor.org/info/rfc9110/#name-date-time-formats). Setting the child process timezone environment variable to zero UTC does the trick to respond with the localtime in GMT. Linux date and time utilities were used to implement this.
- **favicon**: responds to content-type image, the favicon must be located in `public/favicons` so that the server will write its contents to the HTTP GET response. This has been done in commit [a23e21c7165af3c9bbf5ea54ba0d4358c3bb8c17](https://github.com/misael-diaz/sonic-httpd/tree/a23e21c7165af3c9bbf5ea54ba0d4358c3bb8c17).

### Week 2

- **CORS**: initial handling of cors when `Origin` is in the headers; this means that the server responds with `Access-Control-Allow-Origin: *`. This should be fine because the user agent does not send credentials. Have checked that this works on firefox and chrome. This work was done in commit [e82bcf5d](https://github.com/misael-diaz/sonic-httpd/tree/e82bcf5d9912771707aab3a6f1e43cb05a7766a2).
- **fixes chromium hang**: the fault was the http server implementation, by switching to non-blocking reads from the socket and checking the available space in the read buffer we were able to avoid this problem. The hint was that the process would get a 255 status probably by a buffer overrun (we tried to write to a memory region that did not belong to the process). By checking the available space we pass in to `read()` the right number of bytes which might be less than the chunk size if there are fewer bytes left in the buffer. Fix was applied on commit [de3fe009](https://github.com/misael-diaz/sonic-httpd/tree/de3fe0094a95206bda137b3c33d3e55e96c940a3).
- **addresses resource unavaiable issues**: in linux tcp/ip errors can be encountered when reading from the socket and so the right thing to do is to try to read again. During tests I found out that if we get the same problem consecutively it's best to defer the reading for about 20 milliseconds before trying again. So far have not seen this problem to happen on firefox, chromium, and google chrome. This work was done in commit [c445ae5e](https://github.com/misael-diaz/sonic-httpd/tree/c445ae5e3054b40728e9cf60b7f016477beaee47).
- **implements router**: implements a module-based router that makes it possible to modularize the http-server. The server code which implements http is now independent from the code that makesup the webpage. The server only needs the modules that encapsulate the routes and the supported methods for each one. This feature has been inspired by how the `xserver` adds device input drivers at runtime. The driver code can call code that's part of the xserver and that's the basic idea that we have followed here. The developers that work on the endpoints would call utility functions that are defined in the http server. This avoid needless code duplication. To have this working I had to pass the `--export-dynamic` flag to the linker; otherwise the symbols would not be found and that was a pain point now in the past, for I have learned this the hardway by reading the man pages. This has been done in commit [c239fbce6b44f0b81131a522adc7a1b194d25af9](https://github.com/misael-diaz/sonic-httpd/tree/c239fbce6b44f0b81131a522adc7a1b194d25af9).
- **responds with 501 status if not implemented**: according to the RFC9110 specification an origin server that does not implement a method for a resource should return a status 501. This has been done in commit [3a21fb67](https://github.com/misael-diaz/sonic-httpd/tree/3a21fb6761a5c2339e63f8fa3769ece051416634).

## Build

Build the server from source code with GCC:

```sh
mkdir -p modules && gcc -DDEVBUILD=1 -DDIRBUILD="\"$PWD\"" -Wall -Wextra -Wformat -fPIC -g -gdwarf-4 -shared -O0 favicon.cpp -o modules/favicon.so && gcc -DDEVBUILD=1 -DDIRBUILD="\"$PWD\"" -Wall -Wextra -Wformat -fPIC -g -gdwarf-4 -shared -O0 hero.cpp -o modules/hero.so && gcc -DDEVBUILD=1 -DDIRBUILD="\"$PWD\"" -Wall -Wextra -Wformat -fPIC -g -gdwarf-4 -shared -O0 root.cpp -o modules/root.so && gcc -DDEVBUILD=1 -DDIRBUILD="\"$PWD\"" -Wall -Wextra -Wformat -g -gdwarf-4 -O0 -c main.cpp -o main.o && gcc -DDEVBUILD=1 -DDIRBUILD="\"$PWD\"" -Wall -Wextra -Wformat -g -gdwarf-4 -O0 main.o -o sonic-httpd.bin -Wl,--export-dynamic -ldl
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

## Debugging

It is useful to follow the child process instead of the parent process (the server):

```gdb
set follow-fork-mode child
```

and even though the server uses `clone` instead of `fork` to create the child processes this works nicely. It is important to set the breakpoints ahead of hitting run.

## References:

https://www.rfc-editor.org/info/rfc9112/#message.body
https://www.linuxfoundation.org/blog/blog/classic-sysadmin-the-linux-filesystem-explained
