//
// HTTP/1.1 message headers carried over INET 4.4 TCP.
//
// INET 4.x transports application data as Packet/Chunk objects, so the
// HTTP request and response headers are FieldsChunks. A response body is
// appended as an inet::ByteCountChunk of Content-Length bytes.
//
// Cache entries are keyed on the full URL; the caching proxy annotates each
// response as cache HIT or MISS; origins send
// Cache-Control: public, max-age=86400 (web) / 3600 (video).
//

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;

namespace leoproxy;

enum HttpCacheStatus
{
    HTTP_CACHE_NONE = 0;   // not processed by a caching proxy
    HTTP_CACHE_HIT  = 1;   // served from the reverse-proxy cache
    HTTP_CACHE_MISS = 2;   // fetched from an origin server
}

class HttpRequestHeader extends inet::FieldsChunk
{
    chunkLength = inet::B(350);   // typical GET request line + headers
    long requestId;
    int userId;
    string method = "GET";
    string url;                  // e.g. /web/obj/42 or /video/7/2/15.m4s
    string host;
    bool video = false;          // true for a DASH segment request
    int qualityLevel = 0;        // DASH representation index (0 = lowest)
}

class HttpResponseHeader extends inet::FieldsChunk
{
    chunkLength = inet::B(300);   // status line + headers
    long requestId;
    int userId;
    int statusCode = 200;
    string url;
    int64_t contentLength = 0;   // body length in bytes (body follows as ByteCountChunk)
    int cacheStatus @enum(HttpCacheStatus) = HTTP_CACHE_NONE;
    string cacheControl;         // e.g. "public, max-age=86400"
    double maxAge = 0;           // seconds, parsed form of cacheControl
    bool video = false;
    int qualityLevel = 0;
}
