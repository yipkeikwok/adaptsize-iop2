#pragma once

#include <vector>
#include "lru.hpp" // not in this repo, use lru_variants.h
#include "rand.hpp" // not in this repo, use random_helper.h

class AdaptSize {
public:

    AdaptSize(uint64_t _cacheSize);
    ~AdaptSize();

    void request(candidate_t id, const parser::Request& req);
    virtual void setSize(uint64_t cs);

  private:
    uint64_t nextReconfiguration;
    double c;
    misc::Rand rand;
    double cacheSize;
    uint64_t statSize;

    struct ObjInfo {
      double requestRate;
      int64_t size;

      ObjInfo() : requestRate(0.), size(0) { }
    };

    std::unordered_map<candidate_t, ObjInfo> ewmaInfo;
    std::unordered_map<candidate_t, ObjInfo> intervalInfo;

    void reconfigure();
    bool admit(int64_t size);
    double modelHitRate(double c);

    // align data for vectorization
    std::vector<double> alignedReqRate;
    std::vector<double> alignedObjSize;
    std::vector<double> alignedAdmProb;

    // stuff from another simulator
    List<candidate_t> list;
    Tags<candidate_t> tags;
    typedef typename List<candidate_t>::Entry Entry;
  };

}
