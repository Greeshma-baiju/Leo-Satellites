//
// Content-catalogue helpers shared by the HTTP client, proxy and server.
//
// Object identity is encoded in the URL so that every origin server (which
// all host an identical catalogue replica) returns the
// same object size for the same URL. Sizes are derived deterministically from
// the object identifier and a catalogue seed, so a cached copy and a fresh
// copy of an object always have identical length (required for meaningful
// byte-hit-ratio and redundancy metrics).
//
//  Web objects   : /web/obj/<rank>                      (rank 1..webCatalogSize)
//  Video segment : /video/<title>/<quality>/<segment>.m4s
//
#ifndef LEOPROXY_COMMON_HTTPUTILS_H
#define LEOPROXY_COMMON_HTTPUTILS_H

#include <cstdint>
#include <string>
#include <vector>

namespace leoproxy {
namespace httputil {

struct ObjectId
{
    bool video = false;
    int id = 0;          // web object rank, or video title rank
    int quality = 0;     // video representation index (0 = lowest)
    int segment = 0;     // video segment index
};

std::string makeWebUrl(int rank);
std::string makeVideoUrl(int title, int quality, int segment);
bool parseUrl(const std::string& url, ObjectId& out);

// Parses a whitespace/comma separated list of quantities, e.g. "1MB 3MB 6MB 9MB",
// into values in the given unit (e.g. "B").
std::vector<double> parseQuantityList(const char *text, const char *unit);

// SplitMix64 based hash -> uniform (0,1)
double hashUniform(uint64_t key, uint64_t seed);

// Inverse standard normal CDF (Acklam's rational approximation, |err| < 1.2e-9)
double inverseNormalCdf(double p);

struct CatalogParams
{
    double webSizeMedian = 15000;      // bytes
    double webSizeSigma = 2.45;        // lognormal sigma (fits p95 = 850 KB)
    double webSizeMin = 200;           // bytes
    double webSizeMax = 10e6;          // bytes (heavy tail up to several MB)
    std::vector<double> videoSegmentSizes; // bytes per quality level
    double videoSizeJitter = 0.10;     // +/- relative variation per segment
    uint64_t seed = 1;
};

// Deterministic body size (bytes) of an object.
int64_t objectSize(const ObjectId& obj, const CatalogParams& params);

// "public, max-age=<s>"
std::string makeCacheControl(double maxAgeSeconds);

} // namespace httputil
} // namespace leoproxy

#endif
