# Finger for XenevaOS

Terminal finger client (RFC 1288).

## Usage

```
finger [user@]host[:port]
finger @host        (remote user list, default port 79)
```

One-shot TCP/79 query: sends `user\r\n`, prints everything until the
server closes. Same fetch skeleton as `curl`/`gopher`.
