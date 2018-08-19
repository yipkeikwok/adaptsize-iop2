#include <unordered_map>
#include <random>
#include <cmath>
#include <cassert>
#include "lru_variants.h"
#include "../random_helper.h"

/*
  LRU: Least Recently Used eviction
*/
bool LRUCache::lookup(SimpleRequest* req)
{
    CacheObject obj(req);
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        // log hit
        LOG("h", 0, obj.id, obj.size);
        hit(it, obj.size);
        return true;
    }
    return false;
}

void LRUCache::admit(SimpleRequest* req)
{
    const uint64_t size = req->getSize();
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req->getId(), size);
        return;
    }
    // check eviction needed
    while (_currentSize + size > _cacheSize) {
        evict();
    }
    // admit new object
    CacheObject obj(req);
    _cacheList.push_front(obj);
    _cacheMap[obj] = _cacheList.begin();
    _currentSize += size;
    LOG("a", _currentSize, obj.id, obj.size);
}

void LRUCache::evict(SimpleRequest* req)
{
    CacheObject obj(req);
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        ListIteratorType lit = it->second;
        LOG("e", _currentSize, obj.id, obj.size);
        _currentSize -= obj.size;
        _cacheMap.erase(obj);
        _cacheList.erase(lit);
    }
}

SimpleRequest* LRUCache::evict_return()
{
    // evict least popular (i.e. last element)
    if (_cacheList.size() > 0) {
        ListIteratorType lit = _cacheList.end();
        lit--;
        CacheObject obj = *lit;
        LOG("e", _currentSize, obj.id, obj.size);
        SimpleRequest* req = new SimpleRequest(obj.id, obj.size);
        _currentSize -= obj.size;
        _cacheMap.erase(obj);
        _cacheList.erase(lit);
        return req;
    }
    return NULL;
}

void LRUCache::evict()
{
    evict_return();
}

void LRUCache::hit(lruCacheMapType::const_iterator it, uint64_t size)
{
    _cacheList.splice(_cacheList.begin(), _cacheList, it->second);
}

/*
  FIFO: First-In First-Out eviction
*/
void FIFOCache::hit(lruCacheMapType::const_iterator it, uint64_t size)
{
}

/*
  FilterCache (admit only after N requests)
*/
FilterCache::FilterCache()
    : LRUCache(),
      _nParam(2)
{
}

void FilterCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("n") == 0) {
        const uint64_t n = std::stoull(parValue);
        assert(n>0);
        _nParam = n;
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}


bool FilterCache::lookup(SimpleRequest* req)
{
    CacheObject obj(req);
    _filter[obj]++;
    return LRUCache::lookup(req);
}

void FilterCache::admit(SimpleRequest* req)
{
    CacheObject obj(req);
    if (_filter[obj] <= _nParam) {
        return;
    }
    LRUCache::admit(req);
}


/*
  ThLRU: LRU eviction with a size admission threshold
*/
ThLRUCache::ThLRUCache()
    : LRUCache(),
      _sizeThreshold(524288)
{
}

void ThLRUCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("t") == 0) {
        const double t = stof(parValue);
        assert(t>0);
        _sizeThreshold = pow(2.0,t);
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}


void ThLRUCache::admit(SimpleRequest* req)
{
    const uint64_t size = req->getSize();
    // admit if size < threshold
    if (size < _sizeThreshold) {
        LRUCache::admit(req);
    }
}


/*
  ExpLRU: LRU eviction with size-aware probabilistic cache admission
*/
ExpLRUCache::ExpLRUCache()
    : LRUCache(),
      _cParam(262144)
{
}

void ExpLRUCache::setPar(std::string parName, std::string parValue) {
    if(parName.compare("c") == 0) {
        const double c = stof(parValue);
        assert(c>0);
        _cParam = pow(2.0,c);
    } else {
        std::cerr << "unrecognized parameter: " << parName << std::endl;
    }
}



void ExpLRUCache::admit(SimpleRequest* req)
{
    const double size = req->getSize();
    // admit to cache with probablity that is exponentially decreasing with size
    double admissionProb = exp(-size/ _cParam);
    std::bernoulli_distribution distribution(admissionProb);
    if (distribution(globalGenerator)) {
        LRUCache::admit(req);
    }
}


AdaptSizeCache::AdaptSizeCache()
	: LRUCache()
	, nextReconfiguration(RECONFIGURATION_INTERVAL)
	, c(1 << 15)
	, statSize(0)
{
	randGenerator = new std::mt19937_64(SEED); 
	uniform_int_distribution0 = 
		new std::uniform_int_distribution<unsigned long long>(0, 
		4294967296); 
	v=1.0-r;
}

bool AdaptSizeCache::lookup(SimpleRequest* req)
{
	reconfigure(); 

    CacheObject obj(req);
    auto it = _cacheMap.find(obj);
    if (it != _cacheMap.end()) {
        // log hit
        LOG("h", 0, obj.id, obj.size);
        hit(it, obj.size);
        return true;
    }
    return false;
}

void AdaptSizeCache::admit(SimpleRequest* req)
{
	//double roll = (uniform_int_distro(randGenerator) % RANGE) * 1. / RANGE;

	// Yipkei's note
	// refer to AdaptSize::admit() in adaptsize_stub.cpp 
	// understand the code below 
    const uint64_t size = req->getSize();
    // object feasible to store?
    if (size > _cacheSize) {
        LOG("L", _cacheSize, req->getId(), size);
        return;
    }
    // check eviction needed
    while (_currentSize + size > _cacheSize) {
        evict();
    }
    // admit new object
    CacheObject obj(req);
    _cacheList.push_front(obj);
    _cacheMap[obj] = _cacheList.begin();
    _currentSize += size;
    LOG("a", _currentSize, obj.id, obj.size);
}

void AdaptSizeCache::reconfigure() {
	--nextReconfiguration;
	if (nextReconfiguration > 0) {
		return;
	} else if(statSize <= getSize()*3) {
		// not enough data has been gathered
		nextReconfiguration+=10000;
	} else {
		nextReconfiguration = RECONFIGURATION_INTERVAL;
	}

	// smooth stats for objects 
	for(auto it = ewmaInfo.begin(); 
		it != ewmaInfo.end(); 
		it++) {
		it->second.requestCount *= EWMA_DECAY; 
	} 

	// persist intervalinfo in ewmaInfo 
	for (auto it = intervalInfo.begin(); 
		it != intervalInfo.end();
		it++) {
		auto ewmaIt = ewmaInfo.find(it->first); 
		if(ewmaIt != ewmaInfo.end()) {
			ewmaIt->second.requestCount += (1. - EWMA_DECAY) 
				* it->second.requestCount;
			ewmaIt->second.size = it->second.size; 
		} else {
			ewmaInfo.insert(*it);
		}
	}
	intervalInfo.clear(); 

	// copy stats into vector for better alignment 
	// and delete small values 
	alignedReqRate.clear(); 
	alignedObjSize.clear();
	double totalReqCount = 0.0; 
	uint64_t totalObjSize = 0.0; 
	for(auto it = ewmaInfo.begin(); 
		it != ewmaInfo.end(); 
		/*none*/) {
		if(it->second.requestCount < 0.1) {
			// delete from stats 
			statSize -= it->second.size; 
			it = ewmaInfo.erase(it); 
		} else {
			alignedReqRate.push_back(it->second.requestCount); 
			totalReqCount += it->second.requestCount; 
			alignedObjSize.push_back(it->second.size); 
			totalObjSize += it->second.size; 
			++it;
		}
	}

	return;
}

/*
  S4LRU
*/

void S4LRUCache::setSize(uint64_t cs) {
    uint64_t total = cs;
    for(int i=0; i<4; i++) {
        segments[i].setSize(cs/4);
        total -= cs/4;
        std::cerr << "setsize " << i << " : " << cs/4 << "\n";
    }
    if(total>0) {
        segments[0].setSize(cs/4+total);
        std::cerr << "bonus setsize " << 0 << " : " << cs/4 + total << "\n";
    }
}

bool S4LRUCache::lookup(SimpleRequest* req)
{
    for(int i=0; i<4; i++) {
        if(segments[i].lookup(req)) {
            // hit
            if(i<3) {
                // move up
                segments[i].evict(req);
                segment_admit(i+1,req);
            }
            return true;
        }
    }
    return false;
}

void S4LRUCache::admit(SimpleRequest* req)
{
    segments[0].admit(req);
}

void S4LRUCache::segment_admit(uint8_t idx, SimpleRequest* req)
{
    if(idx==0) {
        segments[idx].admit(req);
    } else {
        while(segments[idx].getCurrentSize() + req->getSize()
              > segments[idx].getSize()) {
            // need to evict from this partition first
            // find least popular item in this segment
            auto nreq = segments[idx].evict_return();
            segment_admit(idx-1,nreq);
        }
        segments[idx].admit(req);
    }
}

void S4LRUCache::evict(SimpleRequest* req)
{
    for(int i=0; i<4; i++) {
        segments[i].evict(req);
    }
}

void S4LRUCache::evict()
{
    segments[0].evict();
}
