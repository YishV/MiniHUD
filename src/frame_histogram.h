#pragma once
#include "frame_stats.h"
#include <cstdint>

// Bounded whole-session statistics: 0.1 ms bins to 100 ms, 1 ms to 1 s,
// 10 ms to 60 s, then one overflow bin. Exact count/sum; approximate quantiles.
class FrameHistogram {
    struct Bin { uint64_t count=0; double sum=0; };
    static constexpr size_t kBins=7802;
    std::vector<Bin> bins_;
    uint64_t count_=0;
    double sum_=0;
    static size_t Index(float ms) {
        if (ms<100) return static_cast<size_t>(ms*10);
        if (ms<1000) return 1000+static_cast<size_t>(ms-100);
        if (ms<60000) return 1900+static_cast<size_t>((ms-1000)/10);
        return kBins-1;
    }
public:
    void Add(float ms) {
        if (!std::isfinite(ms) || ms<=0) return;
        if (bins_.empty()) bins_.resize(kBins);
        auto& bin=bins_[Index(ms)]; ++bin.count; bin.sum+=ms; ++count_; sum_+=ms;
    }
    void Clear() { std::vector<Bin>().swap(bins_); count_=0; sum_=0; }
    size_t Bytes() const { return bins_.capacity()*sizeof(Bin); }
    FrameStats Summary() const {
        FrameStats result; result.frames=static_cast<size_t>(count_);
        if (!count_) return result;
        result.avgFps=static_cast<float>(1000.0*count_/sum_);
        auto percentile=[&](double p) {
            const uint64_t rank=std::min(count_-1,static_cast<uint64_t>(p*count_));
            uint64_t seen=0;
            for (const auto& bin:bins_) {
                seen+=bin.count;
                if (seen>rank) return static_cast<float>(bin.sum/bin.count);
            }
            return 0.0f;
        };
        auto low=[&](double fraction) {
            uint64_t remaining=std::max<uint64_t>(1,static_cast<uint64_t>(std::ceil(fraction*count_)));
            const auto wanted=remaining;
            double sum=0;
            for (auto it=bins_.rbegin();it!=bins_.rend() && remaining;++it) {
                const auto take=std::min(remaining,it->count);
                if (take) sum+=it->sum*double(take)/double(it->count);
                remaining-=take;
            }
            return static_cast<float>(1000.0*wanted/sum);
        };
        result.p50Ms=percentile(.5); result.p95Ms=percentile(.95); result.p99Ms=percentile(.99);
        result.low1Fps=low(.01); result.low01Fps=low(.001);
        const float limit=std::max(33.3f,2*result.p50Ms);
        for (const auto& bin:bins_) if (bin.count && bin.sum/bin.count>limit) result.stutters+=static_cast<size_t>(bin.count);
        return result;
    }
};
