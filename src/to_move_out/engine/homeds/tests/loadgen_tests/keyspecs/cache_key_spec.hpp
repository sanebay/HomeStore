//
// Modified by Amit Desai
//

#ifndef HOMESTORE_CACHE_KEY_SPEC_HPP
#define HOMESTORE_CACHE_KEY_SPEC_HPP

#include <cassert>
#include <cstdint>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/ostream.h>

#include "homeds/loadgen/loadgen_common.hpp"
#include "homeds/loadgen/spec/key_spec.hpp"

namespace homeds {
namespace loadgen {
class CacheKey : public BlkId, public KeySpec {

    static CacheKey generate_random_key() {
        /* Seed */
        std::random_device rd{};

        /* Random number generator */
        std::default_random_engine generator{rd()};

        /* Distribution on which to apply the generator */
        std::uniform_int_distribution< long long unsigned > distribution{0, KeySpec::MAX_KEYS};

        const auto sblkid{distribution(generator)};
        return CacheKey{sblkid, 1};
    }

public:
    static CacheKey gen_key(const KeyPattern spec, CacheKey* const ref_key = nullptr) {
        switch (spec) {
        case KeyPattern::SEQUENTIAL: {
            uint64_t newblkId = 0;
            if (ref_key) {
                newblkId = (ref_key->get_id() + 1) % KeySpec::MAX_KEYS;
                return CacheKey(newblkId, 1, 0);
            } else {
                return generate_random_key();
            }
        }
        case KeyPattern::UNI_RANDOM: { // start key is random
            return generate_random_key();
        }
        case KeyPattern::OUT_OF_BOUND:
            return CacheKey(std::numeric_limits<uint64_t>::max(), 1, 0);

        default:
            // We do not support other gen spec yet
            assert(0);
            return CacheKey(0, 0);
        }
    }

    explicit CacheKey() : BlkId{0, 0, 0} {}
    explicit CacheKey(const uint64_t id, const uint8_t nblks, const uint16_t chunk_num = 0) :
            BlkId{id, nblks, chunk_num} {}
    CacheKey(const CacheKey& key) : BlkId{key} {}

    virtual bool operator==(const KeySpec& other) const override {
        CacheKey otherKey = (CacheKey&)other;
        return compare(*this, otherKey);
    }

    BlkId* getBlkId() { return static_cast<BlkId*>(this); }

    virtual bool is_consecutive(KeySpec& k) override {
        CacheKey* nk = (CacheKey*)&k;
        if (get_id() + get_nblks() == nk->get_id())
            return true;
        else
            return false;
    }

    int compare(const CacheKey* other) const { return compare(*this, *other); }

    static int compare(const CacheKey& one, const CacheKey& two) {
        const BlkId& bid1{static_cast< BlkId >(one)};
        const BlkId& bid2{static_cast< BlkId >(two)};
        const int v{BlkId::compare(bid2, bid1)};
        return v;
    }

    static void gen_keys_in_range(const CacheKey& k1, const uint32_t num_of_keys, std::vector< CacheKey >& keys_inrange) {
        uint64_t start{k1.get_id()};
        const uint64_t end{start + num_of_keys - 1};
        while (start <= end) {
            keys_inrange.push_back(CacheKey(start, 1, 0));
            ++start;
        }
    }
};

template < typename charT, typename traits >
std::basic_ostream< charT, traits >& operator<<(std::basic_ostream< charT, traits >& outStream, const CacheKey& cache_key) {
    // copy the stream formatting
    std::basic_ostringstream< charT, traits > outStringStream;
    outStringStream.copyfmt(outStream);

    // print the stream
    outStringStream << cache_key.to_string();
    outStream << outStringStream.str();

    return outStream;
}

}; // namespace loadgen

} // namespace homeds

// hash function definitions
namespace std {
template <>
struct hash<homeds::loadgen::CacheKey > {
    typedef homeds::loadgen::CacheKey argument_type;
    typedef size_t result_type;
    result_type operator()(const argument_type& cache_key) const noexcept {
        return std::hash< uint64_t >()(static_cast<const homestore::BlkId&>(cache_key).to_integer());
    }
};
} // namespace std

#endif // HOMESTORE_CACHE_KEY_SPEC_HPP
