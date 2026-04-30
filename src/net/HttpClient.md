# Net — HTTP Client

The `net` module provides synchronous HTTP operations backed by libcurl.

## HTTP_CLIENT

A thin wrapper around libcurl for GET requests and file downloads.

```cpp
#include "net/HttpClient.h"

sneeze::net::HTTP_CLIENT http;
http.Initialize ();

// GET a URL into a string
std::string sBody;
long nCode = 0;
if (http.Get ("https://example.com/api/data", sBody, nCode))
{
   // nCode == 200, sBody contains the response
}

// Download directly to a file
long nFileCode = 0;
http.DownloadToFile ("https://example.com/model.glb", "C:/temp/model.glb", nFileCode);

http.Shutdown ();
```

### Methods

| Method           | Description                                           |
|------------------|-------------------------------------------------------|
| `Initialize()`   | Calls `curl_global_init`, logs libcurl + SSL versions |
| `Shutdown()`     | Calls `curl_global_cleanup`                           |
| `Get()`          | HTTP GET, response body written to a `std::string`    |
| `DownloadToFile()`| HTTP GET, response body written directly to disk     |

Both request methods follow redirects and return the HTTP status code via
the `nHttpCode` out-parameter.

### Timeouts

- `Get()` uses a 10-second timeout.
- `DownloadToFile()` uses a 300-second (5-minute) timeout.

## Dependencies

- **libcurl** — linked at build time. SSL backend reported at initialization.

## Unimplemented / Future Work

- **Async requests** — all operations are currently synchronous and block
  the calling thread. The file cache will eventually need non-blocking
  fetches dispatched to a background thread or the WASM thread pool.
- **POST / PUT / DELETE** — only GET is implemented.
- **Request headers** — no support for custom headers (e.g., auth tokens).
- **Connection pooling** — each request creates and destroys a CURL handle.
  A persistent multi-handle would improve throughput for batch fetches.
