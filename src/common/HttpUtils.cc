#include "common/HttpUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include <omnetpp.h>

namespace leoproxy {
namespace httputil {

std::string makeWebUrl(int rank)
{
    return "/web/obj/" + std::to_string(rank);
}

std::string makeVideoUrl(int title, int quality, int segment)
{
    return "/video/" + std::to_string(title) + "/" + std::to_string(quality) + "/" + std::to_string(segment) + ".m4s";
}

bool parseUrl(const std::string& url, ObjectId& out)
{
    int a = 0, b = 0, c = 0;
    if (std::sscanf(url.c_str(), "/web/obj/%d", &a) == 1) {
        out.video = false;
        out.id = a;
        out.quality = 0;
        out.segment = 0;
        return true;
    }
    if (std::sscanf(url.c_str(), "/video/%d/%d/%d.m4s", &a, &b, &c) == 3) {
        out.video = true;
        out.id = a;
        out.quality = b;
        out.segment = c;
        return true;
    }
    return false;
}

std::vector<double> parseQuantityList(const char *text, const char *unit)
{
    std::vector<double> result;
    std::string s(text);
    std::replace(s.begin(), s.end(), ',', ' ');
    std::istringstream iss(s);
    std::string token;
    while (iss >> token)
        result.push_back(omnetpp::cValue::parseQuantity(token.c_str(), unit));
    return result;
}

static uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

double hashUniform(uint64_t key, uint64_t seed)
{
    uint64_t h = splitmix64(key ^ splitmix64(seed));
    // 53 random bits -> (0,1)
    return ((h >> 11) + 0.5) * (1.0 / 9007199254740992.0);
}

double inverseNormalCdf(double p)
{
    static const double a[] = { -3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                                1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00 };
    static const double b[] = { -5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                                6.680131188771972e+01, -1.328068155288572e+01 };
    static const double c[] = { -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                                -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00 };
    static const double d[] = { 7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                                3.754408661907416e+00 };
    const double plow = 0.02425, phigh = 1 - plow;
    p = std::min(std::max(p, 1e-12), 1 - 1e-12);
    if (p < plow) {
        double q = std::sqrt(-2 * std::log(p));
        return (((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
               ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1);
    }
    if (p > phigh) {
        double q = std::sqrt(-2 * std::log(1 - p));
        return -(((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
                ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1);
    }
    double q = p - 0.5, r = q * q;
    return (((((a[0]*r + a[1])*r + a[2])*r + a[3])*r + a[4])*r + a[5]) * q /
           (((((b[0]*r + b[1])*r + b[2])*r + b[3])*r + b[4])*r + 1);
}

int64_t objectSize(const ObjectId& obj, const CatalogParams& params)
{
    if (!obj.video) {
        double u = hashUniform((uint64_t)obj.id, params.seed);
        double size = params.webSizeMedian * std::exp(params.webSizeSigma * inverseNormalCdf(u));
        size = std::min(std::max(size, params.webSizeMin), params.webSizeMax);
        return (int64_t)std::llround(size);
    }
    if (params.videoSegmentSizes.empty())
        throw omnetpp::cRuntimeError("videoSegmentSizes is empty");
    int q = std::min(std::max(obj.quality, 0), (int)params.videoSegmentSizes.size() - 1);
    uint64_t key = ((uint64_t)obj.id << 32) ^ ((uint64_t)q << 24) ^ (uint64_t)obj.segment ^ 0x5bd1e995ULL;
    double u = hashUniform(key, params.seed + 7);
    double size = params.videoSegmentSizes[q] * (1.0 + params.videoSizeJitter * (2.0 * u - 1.0));
    return (int64_t)std::llround(std::max(size, 1.0));
}

std::string makeCacheControl(double maxAgeSeconds)
{
    return "public, max-age=" + std::to_string((long long)std::llround(maxAgeSeconds));
}

} // namespace httputil
} // namespace leoproxy
