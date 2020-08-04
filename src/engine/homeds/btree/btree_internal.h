/*
 * btree_internal.h
 *
 *  Created on: 14-May-2016
 *      Author: Hari Kadayam
 *
 *  Copyright © 2016 Kadayam, Hari. All rights reserved.
 */
#pragma once
#include <vector>
#include <iostream>
#include <cmath>
#include <fds/utils.hpp>
#include <fds/freelist_allocator.hpp>
#include "engine/common/error.h"
#include "engine/common/homestore_header.hpp"
#include <metrics/metrics.hpp>
#include <utility/enum.hpp>
#include <boost/intrusive_ptr.hpp>
#include <fds/utils.hpp>
#include <fds/obj_allocator.hpp>
#include <utility/atomic_counter.hpp>
#include <utility/obj_life_counter.hpp>
#include <sds_logging/logging.h>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/control/if.hpp>
#include <boost/preprocessor/stringize.hpp>
#include "engine/common/homestore_assert.hpp"
#include "engine/blkalloc/blk.h"

ENUM(btree_status_t, uint32_t, success, not_found, item_found, closest_found, closest_removed, retry, has_more,
     read_failed, write_failed, stale_buf, refresh_failed, put_failed, space_not_avail, split_failed, insert_failed,
     cp_id_mismatch, merge_not_required, merge_failed, replay_not_needed, fast_path_not_possible, resource_full);

typedef enum {
    READ_NONE = 0,
    READ_FIRST = 1,
    READ_SECOND = 2,
    READ_BOTH = 3,
} diff_read_next_t;

namespace homestore {
struct blkalloc_cp_id;
}

/* We should always find the child smaller or equal then  search key in the interior nodes. */
#ifndef NDEBUG
#define ASSERT_IS_VALID_INTERIOR_CHILD_INDX(ret, node)                                                                 \
    DEBUG_ASSERT(((ret.end_of_search_index < (int)node->get_total_entries()) || node->has_valid_edge()),               \
                 "Is_valid_interior_child_check_failed: end_of_search_index={} total_entries={}, edge_valid={}",       \
                 ret.end_of_search_index, node->get_total_entries(), node->has_valid_edge())
#else
#define ASSERT_IS_VALID_INTERIOR_CHILD_INDX(ret, node)
#endif

#define THIS_BT_LOG(level, mod, node, msg, ...)                                                                        \
    HS_DETAILED_LOG(level, mod, , "btree", m_btree_cfg.get_name(), BOOST_PP_IF(BOOST_PP_IS_EMPTY(node), , "node"),     \
                    node->to_string(), msg, ##__VA_ARGS__)
#define BT_ASSERT(assert_type, cond, node, ...)                                                                        \
    HS_DETAILED_ASSERT(assert_type, cond, , "btree", m_btree_cfg.get_name(),                                           \
                       BOOST_PP_IF(BOOST_PP_IS_EMPTY(node), , "node"), node->to_string(), ##__VA_ARGS__)
#define BT_ASSERT_CMP(assert_type, val1, cmp, val2, node, ...)                                                         \
    HS_DETAILED_ASSERT_CMP(assert_type, val1, cmp, val2, , "btree", m_btree_cfg.get_name(),                            \
                           BOOST_PP_IF(BOOST_PP_IS_EMPTY(node), , "node"), node->to_string(), ##__VA_ARGS__)

#define BT_DEBUG_ASSERT(...) BT_ASSERT(DEBUG, __VA_ARGS__)
#define BT_RELEASE_ASSERT(...) BT_ASSERT(RELEASE, __VA_ARGS__)
#define BT_LOG_ASSERT(...) BT_ASSERT(LOGMSG, __VA_ARGS__)

#define BT_DEBUG_ASSERT_CMP(...) BT_ASSERT_CMP(DEBUG, ##__VA_ARGS__)
#define BT_RELEASE_ASSERT_CMP(...) BT_ASSERT_CMP(RELEASE, ##__VA_ARGS__)

#define BT_LOG_ASSERT_CMP(...) BT_ASSERT_CMP(RELEASE, ##__VA_ARGS__)
//#define BT_LOG_ASSERT_CMP(...) BT_ASSERT_CMP(LOGMSG, ##__VA_ARGS__)

#define MAX_ADJANCENT_INDEX 3

// clang-format off
/* Journal entry of a btree
 *---------------------------------------------------------------------------------------------------------------------------------------------
 * |  Journal_entry_hdr | list of old node IDs | list of stale node IDs | list of new node IDs | list of new node gen | list of modified keys |
 *---------------------------------------------------------------------------------------------------------------------------------------------
 */
// clang-format on
VENUM(journal_op, uint8_t, BTREE_SPLIT = 1, BTREE_MERGE = 2, BTREE_CREATE = 3);

#define INVALID_SEQ_ID UINT64_MAX
struct btree_cp_id;
using btree_cp_id_ptr = boost::intrusive_ptr< btree_cp_id >;
using cp_comp_callback = std::function< void(const btree_cp_id_ptr& cp_id) >;
using bnodeid_t = uint64_t;
static constexpr bnodeid_t empty_bnodeid = std::numeric_limits< bnodeid_t >::max();

struct btree_cp_superblock {
    int64_t active_psn = -1;
    int64_t cp_cnt = -1;
    int64_t blkalloc_cp_cnt = -1;
    int64_t btree_size = 0;
    /* we can add more statistics as well like number of interior nodes etc. */
} __attribute__((__packed__));

struct btree_cp_id : public boost::intrusive_ref_counter< btree_cp_id > {
    int64_t cp_cnt = -1;
    std::atomic< int > ref_cnt;
    std::atomic< int64_t > btree_size;
    int64_t start_psn = -1; // not inclusive
    int64_t end_psn = -1;   // inclusive
    cp_comp_callback cb;
    homestore::blkid_list_ptr free_blkid_list;
    btree_cp_id() : ref_cnt(1), btree_size(0){};
    ~btree_cp_id() {}
};

/********************* Journal Specific Section **********************/
struct bt_node_gen_pair {
    bnodeid_t node_id = empty_bnodeid;
    uint64_t node_gen = 0;

    bnodeid_t get_id() const { return node_id; }
    uint64_t get_gen() const { return node_gen; }
};

VENUM(bt_journal_node_op, uint8_t, inplace_write = 1, removal = 2, creation = 3);
struct bt_journal_node_info {
    bt_node_gen_pair node_info;
    bt_journal_node_op type = bt_journal_node_op::inplace_write;
    uint16_t key_size = 0;
    uint8_t* key_area() { return ((uint8_t*)this + sizeof(bt_journal_node_info)); }
    bnodeid_t node_id() const { return node_info.node_id; }
    uint64_t node_gen() const { return node_info.node_gen; }
};

struct btree_journal_entry {
#if 0
    static constexpr size_t alloc_increment = 256;
    static constexpr size_t min_size() { return std::max(alloc_increment, sizeof(btree_journal_entry)); }

    static sisl::io_blob make(journal_op op) {
        auto b = sisl::io_blob(
            min_size(), HomeLogStore::is_aligned_buf_needed(min_size()) ? HS_STATIC_CONFIG(disk_attr.align_size) : 0);
        new (b.bytes) btree_journal_entry(op);
        return b;
    }

    static inline constexpr btree_journal_entry* get(const sisl::io_blob& b) { return (btree_journal_entry*)b.bytes; }

    static btree_journal_entry* realloc_if_needed(sisl::io_blob& b, uint16_t append_size) {
        assert(b.size > get(b)->actual_size);
        uint16_t avail_size = b.size - get(b)->actual_size;
        if (avail_size < append_size) {
            auto new_size = sisl::round_up(entry->actual_size + append_size, alloc_increment);
            b.buf_realloc(new_size,
                          HomeLogStore::is_aligned_buf_needed(new_size) ? HS_STATIC_CONFIG(disk_attr.align_size) : 0);
        }
        return get(b);
    }

    static void append_node(sisl::io_blob& b, bt_journal_node_op node_op, bnodeid_t node_id, uint64_t gen,
                            sisl::blob key = {nullptr, 0}) {
        uint16_t append_size = sizeof(bt_journal_node_info) + key.size;
        btree_journal_entry* entry = realloc_if_needed(b, append_size);
        ++entry->node_count;

        bt_journal_node_info* info = entry->_append_area();
        info->node_info = {node_id, gen};
        info->type = node_op;
        info->key_size = key.size;
        if (key.size) memcpy(info->key_area(), key.bytes, key.size);
        entry->actual_size += append_size;
    }
#endif
    void append_node(bt_journal_node_op node_op, bnodeid_t node_id, uint64_t gen, sisl::blob key = {nullptr, 0}) {
        ++node_count;
        bt_journal_node_info* info = _append_area();
        info->node_info = {node_id, gen};
        info->type = node_op;
        info->key_size = key.size;
        if (key.size) memcpy(info->key_area(), key.bytes, key.size);
        actual_size += sizeof(bt_journal_node_info) + key.size;
    }

    void foreach_node(bt_journal_node_op node_op, const std::function< void(bt_node_gen_pair, sisl::blob) >& cb) const {
        bt_journal_node_info* info = (bt_journal_node_info*)((uint8_t*)this + sizeof(btree_journal_entry));
        for (auto i = 0u; i < node_count; ++i) {
            if (info->type == node_op) { cb(info->node_info, sisl::blob(info->key_area(), info->key_size)); }
            info = (bt_journal_node_info*)((uint8_t*)info + sizeof(bt_journal_node_info) + info->key_size);
        }
    }

    std::vector< bt_journal_node_info* > get_nodes(const std::optional< bt_journal_node_op >& node_op = {}) const {
        std::vector< bt_journal_node_info* > result;
        bt_journal_node_info* info = (bt_journal_node_info*)((uint8_t*)this + sizeof(btree_journal_entry));
        for (auto i = 0u; i < node_count; ++i) {
            if (!node_op || (info->type == *node_op)) { result.push_back(info); }
            info = (bt_journal_node_info*)((uint8_t*)info + sizeof(bt_journal_node_info) + info->key_size);
        }
        return result;
    }

    bt_journal_node_info* leftmost_node() const {
        return get_nodes(is_root ? bt_journal_node_op::creation : bt_journal_node_op::inplace_write)[0];
    }

    std::string to_string() const {
        auto str = fmt::format("op={} is_root={} cp_cnt={} size={} num_nodes={} ", enum_name(op), is_root, cp_cnt,
                               actual_size, node_count);
        str += fmt::format("[parent: id={}, gen={}] ", parent_node.node_id, parent_node.node_gen);

        bt_journal_node_info* info = (bt_journal_node_info*)((uint8_t*)this + sizeof(btree_journal_entry));
        for (auto i = 0u; i < node_count; ++i) {
            str += fmt::format("[node{}: id={} gen={} node_op={} key_size={}] ", i, info->node_info.node_id,
                               info->node_info.node_gen, enum_name(info->type), info->key_size);
            info = (bt_journal_node_info*)((uint8_t*)info + sizeof(bt_journal_node_info) + info->key_size);
        }
        return str;
    }

    /************** Actual header starts here **********/
    journal_op op;
    bool is_root = false;
    int64_t cp_cnt = 0;
    uint16_t actual_size = sizeof(btree_journal_entry);
    uint16_t node_count = 0;
    bt_node_gen_pair parent_node; // Info about the parent node
    // Additional node info follows this

private:
    btree_journal_entry(journal_op p, bool root, bt_node_gen_pair ninfo) : op(p), is_root(root), parent_node(ninfo) {}
    bt_journal_node_info* _append_area() { return (bt_journal_node_info*)((uint8_t*)this + actual_size); }
} __attribute__((__packed__));

#if 0
struct btree_journal_entry_hdr {
    journal_op op;
    bool is_root = false;
    uint64_t parent_node_id;
    uint64_t parent_node_gen;
    uint32_t parent_indx;
    uint64_t left_child_id;
    uint64_t left_child_gen;
    uint8_t num_old_nodes;
    uint8_t num_new_nodes;
    uint8_t new_key_size;
    int64_t cp_cnt;
} __attribute__((__packed__));
#endif

#if 0
struct btree_journal_data {
    static btree_journal_entry* make(journal_op op) {
        size_t size = std::max(
            alloc_increment,
            (sizeof(btree_journal_entry_hdr) +
             (HS_DYNAMIC_CONFIG(btree->max_nodes_to_rebalance) * btree_journal_entry_hdr::estimated_info_size)));

        btree_journal_data* entry = (btree_journal_data*)malloc(size);
        entry->op = op;
        entry->is_root = false;
        entry->actual_size = sizeof(btree_journal_entry_hdr);
        return entry;
    }

    btree_journal_entry_hdr btree_journal_entry_hdr* header() const { return &hdr; }

    uint64_t* reserve_old_nodes_list(uint8_t size) {
        hdr.old_nodes_size = size;
        return old_node_area();
    }

    std::pair< uint64_t*, uint32_t > old_nodes_list() const {
        return std::make_pair((uint64_t*)old_node_area(), hdr.old_nodes_size);
    }

    std::pair< uint64_t*, uint32_t > new_nodes_list() const {
        return std::make_pair((uint64_t*)new_node_area(), hdr.new_nodes_size);
    }

    std::pair< uint64_t*, uint32_t > new_node_gen() const {
        return std::make_pair((uint64_t*)new_node_area(), hdr.new_nodes_size);
    }

    std::pair< uint8_t*, uint32_t > new_keys() const { return std::make_pair(new_key_area(), hdr.new_key_size); }

private:
    btree_journal_entry() {}
    uint8_t* data_area() const { return ((uint8_t*)this + sizeof(btree_journal_entry_hdr)); }
    uint8_t* old_node_area() const { return data_area(); }
    uint8_t* new_node_area() const { return old_node_area() + (sizeof(uint64_t) * hdr.old_nodes_size); }
    uint8_t* new_node_gen_area() const { return new_node_area() + (sizeof(uint64_t) * hdr.new_nodes_size); }
    uint8_t* new_key_area() const { return new_node_gen_area() + (sizeof(uint64_t) * hdr.new_nodes_size); }
} __attribute__((__packed__));

struct btree_journal_entry {
    static btree_journal_entry_hdr* get_entry_hdr(uint8_t* mem) { return ((btree_journal_entry_hdr*)mem); }

    static std::pair< uint64_t*, uint32_t > get_old_nodes_list(uint8_t* mem) {
        auto hdr = get_entry_hdr(mem);
        uint64_t* old_node_id = (uint64_t*)((uint64_t)mem + sizeof(btree_journal_entry_hdr));
        return (std::make_pair(old_node_id, hdr->old_nodes_size));
    }

    static std::pair< uint64_t*, uint32_t > get_new_nodes_list(uint8_t* mem) {
        auto hdr = get_entry_hdr(mem);
        auto old_node_id = get_old_nodes_list(mem);
        uint64_t* new_node_id = (uint64_t*)(&(old_node_id.first[old_node_id.second]));
        return (std::make_pair(new_node_id, hdr->new_nodes_size));
    }

    static std::pair< uint64_t*, uint32_t > get_new_node_gen(uint8_t* mem) {
        auto hdr = get_entry_hdr(mem);
        auto new_node_id = get_new_nodes_list(mem);
        uint64_t* new_node_gen = (uint64_t*)(&(new_node_id.first[new_node_id.second]));
        return (std::make_pair(new_node_gen, hdr->new_nodes_size));
    }

    static std::pair< uint8_t*, uint32_t > get_key(uint8_t* mem) {
        auto hdr = get_entry_hdr(mem);
        auto new_node_gen = get_new_node_gen(mem);
        uint8_t* key = (uint8_t*)(&(new_node_gen.first[new_node_gen.second]));
        return (std::make_pair(key, hdr->new_key_size));
    }
} __attribute__((__packed__));
#endif

namespace homeds {
namespace btree {

#define BTREE_VMODULE_SET (VMODULE_ADD(bt_insert), VMODULE_ADD(bt_delete), )

template < uint8_t NBits >
constexpr uint64_t set_bits() {
    return (NBits == 64) ? -1ULL : ((static_cast< uint64_t >(1) << NBits) - 1);
}

#if 0
struct bnodeid {
    uint64_t m_id : 63; // TODO: We can reduce this if needbe later.
    uint64_t m_pc_gen_flag : 1;

    bnodeid() : bnodeid(set_bits< 63 >(), 0) {}
    bnodeid(uint64_t id, uint8_t gen_flag = 0) : m_id(id), m_pc_gen_flag(gen_flag) {}
    bnodeid(const bnodeid& other) = default;

    bool operator==(const bnodeid& bid) const { return (m_id == bid.m_id) && (m_pc_gen_flag == bid.m_pc_gen_flag); }
    std::string to_string() {
        std::stringstream ss;
        ss << " Id:" << m_id << ",pcgen:" << m_pc_gen_flag;
        return ss.str();
    }

    friend std::ostream& operator<<(std::ostream& os, const bnodeid& id) {
        os << " Id:" << id.m_id << ",pcgen:" << id.m_pc_gen_flag;
        return os;
    }

    bool is_valid() const { return (m_id != set_bits< 63 >()); }
    static bnodeid empty_bnodeid() { return bnodeid(); }
    uint64_t get_int() { return (m_id << 63 | 1); };
} __attribute__((packed));
#endif

struct btree_super_block {
    bnodeid_t root_node = 0;
    uint32_t journal_id = 0;
} __attribute((packed));

ENUM(btree_store_type, uint32_t, MEM_BTREE, SSD_BTREE);

ENUM(btree_node_type, uint32_t, SIMPLE, VAR_VALUE, VAR_KEY, VAR_OBJECT, PREFIX, COMPACT);

#if 0
enum MatchType { NO_MATCH = 0, FULL_MATCH, SUBSET_MATCH, SUPERSET_MATCH, PARTIAL_MATCH_LEFT, PARTIAL_MATCH_RIGHT };
#endif

ENUM(btree_put_type, uint16_t,
     INSERT_ONLY_IF_NOT_EXISTS, // Insert
     REPLACE_ONLY_IF_EXISTS,    // Upsert
     REPLACE_IF_EXISTS_ELSE_INSERT,
     APPEND_ONLY_IF_EXISTS, // Update
     APPEND_IF_EXISTS_ELSE_INSERT);

class BtreeSearchRange;

class BtreeKey {
public:
    BtreeKey() = default;
    // BtreeKey(const BtreeKey& other) = delete; // Deleting copy constructor forces the
    // derived class to define its own copy constructor
    virtual ~BtreeKey() = default;

    // virtual BtreeKey& operator=(const BtreeKey& other) = delete; // Deleting = overload forces the derived to
    // define its = overload
    virtual int compare(const BtreeKey* other) const = 0;

    /* Applicable only for extent keys. It compare start key of (*other) with end key of (*this) */
    virtual int compare_start(const BtreeKey* other) const { return compare(other); };
    virtual int compare_range(const BtreeSearchRange& range) const = 0;
    virtual sisl::blob get_blob() const = 0;
    virtual void set_blob(const sisl::blob& b) = 0;
    virtual void copy_blob(const sisl::blob& b) = 0;

    /* Applicable to extent keys. It doesn't copy the entire blob. Copy only the end key of the blob */
    virtual void copy_end_key_blob(const sisl::blob& b) { copy_blob(b); };

    virtual uint32_t get_blob_size() const = 0;
    virtual void set_blob_size(uint32_t size) = 0;

    virtual std::string to_string() const = 0;
    virtual bool is_extent_key() { return false; }
};

ENUM(_MultiMatchSelector, uint16_t, DO_NOT_CARE, LEFT_MOST, RIGHT_MOST,
     BEST_FIT_TO_CLOSEST,           // Return the entry either same or more then the search key. If
                                    // nothing is available then return the entry just smaller then the
                                    // search key.
     BEST_FIT_TO_CLOSEST_FOR_REMOVE // It is similar as BEST_FIT_TO_CLOSEST but have special
                                    // handling for remove This code will be removed once
                                    // range query is supported in remove
)

class BtreeSearchRange {
    friend struct BtreeQueryCursor;
    // friend class  BtreeQueryRequest;

private:
    const BtreeKey* m_start_key = nullptr;
    const BtreeKey* m_end_key = nullptr;

    bool m_start_incl = false;
    bool m_end_incl = false;
    _MultiMatchSelector m_multi_selector;

public:
    BtreeSearchRange() {}

    BtreeSearchRange(const BtreeKey& start_key) : BtreeSearchRange(start_key, true, start_key, true) {}

    BtreeSearchRange(const BtreeKey& start_key, const BtreeKey& end_key) :
            BtreeSearchRange(start_key, true, end_key, true) {}

    BtreeSearchRange(const BtreeKey& start_key, bool start_incl, _MultiMatchSelector option) :
            BtreeSearchRange(start_key, start_incl, start_key, start_incl, option) {}

    BtreeSearchRange(const BtreeKey& start_key, bool start_incl, const BtreeKey& end_key, bool end_incl) :
            BtreeSearchRange(start_key, start_incl, end_key, end_incl, _MultiMatchSelector::DO_NOT_CARE) {}

    BtreeSearchRange(const BtreeKey& start_key, bool start_incl, const BtreeKey& end_key, bool end_incl,
                     _MultiMatchSelector option) :
            m_start_key(&start_key),
            m_end_key(&end_key),
            m_start_incl(start_incl),
            m_end_incl(end_incl),
            m_multi_selector(option) {}

    void set(const BtreeKey& start_key, bool start_incl, const BtreeKey& end_key, bool end_incl) {
        m_start_key = &start_key;
        m_end_key = &end_key;
        m_start_incl = start_incl;
        m_end_incl = end_incl;
    }

    void set_start_key(BtreeKey* m_start_key) { BtreeSearchRange::m_start_key = m_start_key; }
    void set_start_incl(bool m_start_incl) { BtreeSearchRange::m_start_incl = m_start_incl; }
    void set_end_key(const BtreeKey* m_end_key) { BtreeSearchRange::m_end_key = m_end_key; }
    void set_end_incl(bool m_end_incl) { BtreeSearchRange::m_end_incl = m_end_incl; }

    const BtreeKey* get_start_key() const { return m_start_key; }
    const BtreeKey* get_end_key() const { return m_end_key; }

    BtreeSearchRange extract_start_of_range() const {
        return BtreeSearchRange(*m_start_key, m_start_incl, m_multi_selector);
    }
    BtreeSearchRange extract_end_of_range() const { return BtreeSearchRange(*m_end_key, m_end_incl, m_multi_selector); }

    // Is the key provided and current key completely matches.
    // i.e If say a range = [8 to 12] and rkey is [9 - 11], then compare will return 0,
    // but this method will return false. It will return true only if range exactly matches.
    // virtual bool is_full_match(BtreeRangeKey *rkey) const = 0;

    bool is_start_inclusive() const { return m_start_incl; }
    bool is_end_inclusive() const { return m_end_incl; }

    bool is_simple_search() const { return ((get_start_key() == get_end_key()) && (m_start_incl == m_end_incl)); }

    _MultiMatchSelector selection_option() const { return m_multi_selector; }
    void set_selection_option(_MultiMatchSelector o) { m_multi_selector = o; }
};

/* This type is for keys which is range in itself i.e each key is having its own
 * start() and end().
 */
class ExtentBtreeKey : public BtreeKey {
public:
    ExtentBtreeKey() = default;
    virtual ~ExtentBtreeKey() = default;
    virtual bool is_extent_key() { return true; }
    virtual int compare_end(const BtreeKey* other) const = 0;
    virtual int compare_start(const BtreeKey* other) const override = 0;

    virtual bool preceeds(const BtreeKey* other) const = 0;
    virtual bool succeeds(const BtreeKey* other) const = 0;

    virtual void copy_end_key_blob(const sisl::blob& b) override = 0;

    /* we always compare the end key in case of extent */
    virtual int compare(const BtreeKey* other) const override { return (compare_end(other)); }

    /* we always compare the end key in case of extent */
    virtual int compare_range(const BtreeSearchRange& range) const override {
        return (compare_end(range.get_end_key()));
    }
};

class BtreeValue {
public:
    BtreeValue() {}
    virtual ~BtreeValue() {}

    // BtreeValue(const BtreeValue& other) = delete; // Deleting copy constructor forces the derived class to define
    // its own copy constructor

    virtual sisl::blob get_blob() const = 0;
    virtual void set_blob(const sisl::blob& b) = 0;
    virtual void copy_blob(const sisl::blob& b) = 0;
    virtual void append_blob(const BtreeValue& new_val, BtreeValue& existing_val) = 0;

    virtual uint32_t get_blob_size() const = 0;
    virtual void set_blob_size(uint32_t size) = 0;
    virtual uint32_t estimate_size_after_append(const BtreeValue& new_val) = 0;

    virtual void get_overlap_diff_kvs(BtreeKey* k1, BtreeValue* v1, BtreeKey* k2, BtreeValue* v2, uint32_t param,
                                      diff_read_next_t& to_read,
                                      std::vector< std::pair< BtreeKey, BtreeValue > >& overlap_kvs) {
        LOGINFO("Not Implemented");
    }

    virtual std::string to_string() const { return ""; }
};

/* This class is a top level class to keep track of the locks that are held currently. It is
 * used for serializabke query to unlock all nodes in right order at the end of the lock */
class BtreeLockTracker {
public:
    virtual ~BtreeLockTracker() = default;
};

struct BtreeQueryCursor {
    std::unique_ptr< BtreeKey > m_last_key;
    std::unique_ptr< BtreeLockTracker > m_locked_nodes;
    const sisl::blob serialize() {
        sisl::blob b(nullptr, 0);
        if (m_last_key) { return (m_last_key->get_blob()); }
        return b;
    };
};

ENUM(BtreeQueryType, uint8_t,
     // This is default query which walks to first element in range, and then sweeps/walks
     // across the leaf nodes. However, if upon pagination, it again walks down the query from
     // the key it left off.
     SWEEP_NON_INTRUSIVE_PAGINATION_QUERY,

     // Similar to sweep query, except that it retains the node and its lock during
     // pagination. This is more of intrusive query and if the caller is not careful, the read
     // lock will never be unlocked and could cause deadlocks. Use this option carefully.
     SWEEP_INTRUSIVE_PAGINATION_QUERY,

     // This is relatively inefficient query where every leaf node goes from its parent node
     // instead of walking the leaf node across. This is useful only if we want to check and
     // recover if parent and leaf node are in different generations or crash recovery cases.
     TREE_TRAVERSAL_QUERY,

     // This is both inefficient and quiet intrusive/unsafe query, where it locks the range
     // that is being queried for and do not allow any insert or update within that range. It
     // essentially create a serializable level of isolation.
     SERIALIZABLE_QUERY)

// Base class for range callback params
class BRangeCBParam {

public:
    BRangeCBParam() {}
    BtreeSearchRange& get_input_range() { return m_input_range; }
    BtreeSearchRange& get_sub_range() { return m_sub_range; }
    // TODO - make setters private and make Query/Update req as friends to access these
    void set_sub_range(const BtreeSearchRange& sub_range) { m_sub_range = sub_range; }
    void set_input_range(const BtreeSearchRange& sub_range) { m_input_range = sub_range; }

private:
    BtreeSearchRange m_input_range; // Btree range filter originally provided
    BtreeSearchRange m_sub_range;   // Btree sub range used during callbacks.
};

// class for range query callback param
template < typename K, typename V >
class BRangeQueryCBParam : public BRangeCBParam {
public:
    BRangeQueryCBParam() {}
};

// class for range update callback param
template < typename K, typename V >
class BRangeUpdateCBParam : public BRangeCBParam {
public:
    BRangeUpdateCBParam(K& key, V& value) : m_new_key(key), m_new_value(value), m_state_modifiable(true) {}
    K& get_new_key() { return m_new_key; }
    V& get_new_value() { return m_new_value; }
    bool is_state_modifiable() const { return m_state_modifiable; }
    void set_state_modifiable(bool state_modifiable) { BRangeUpdateCBParam::m_state_modifiable = state_modifiable; }

private:
    K m_new_key;
    V m_new_value;
    bool m_state_modifiable;
};

// Base class for range requests
class BRangeRequest {
public:
    BtreeSearchRange& get_input_range() { return m_input_range; }

protected:
    BRangeRequest(BRangeCBParam* cb_param, BtreeSearchRange& search_range) :
            m_cb_param(cb_param),
            m_input_range(search_range) {}

    BRangeCBParam* m_cb_param;      // additional parameters that is passed to callback
    BtreeSearchRange m_input_range; // Btree range filter originally provided
};

template < typename K, typename V >
using match_item_cb_get_t = std::function< btree_status_t(
    std::vector< std::pair< K, V > >&, std::vector< std::pair< K, V > >&, BRangeQueryCBParam< K, V >*) >;
template < typename K, typename V >
class BtreeQueryRequest : public BRangeRequest {
public:
    BtreeQueryRequest(BtreeSearchRange& search_range,
                      BtreeQueryType query_type = BtreeQueryType::SWEEP_NON_INTRUSIVE_PAGINATION_QUERY,
                      uint32_t batch_size = 1000, match_item_cb_get_t< K, V > cb = nullptr,
                      BRangeQueryCBParam< K, V >* cb_param = nullptr) :
            BRangeRequest(cb_param, search_range),
            m_batch_search_range(search_range),
            m_start_range(search_range.extract_start_of_range()),
            m_end_range(search_range.extract_end_of_range()),
            m_query_type(query_type),
            m_batch_size(batch_size),
            m_cb(cb) {}

    ~BtreeQueryRequest() = default;

    void init_batch_range() {
        if (!is_empty_cursor()) {
            m_batch_search_range = BtreeSearchRange(*m_cursor.m_last_key, false, *m_input_range.get_end_key(),
                                                    m_input_range.is_end_inclusive(), m_input_range.selection_option());
            m_start_range = BtreeSearchRange(*m_cursor.m_last_key, false, m_input_range.selection_option());
        }
    }

    BtreeSearchRange& this_batch_range() { return m_batch_search_range; }
    BtreeQueryCursor& cursor() { return m_cursor; }
    BtreeSearchRange& get_start_of_range() { return m_start_range; }
    BtreeSearchRange& get_end_of_range() { return m_end_range; }

    bool is_empty_cursor() const { return ((m_cursor.m_last_key == nullptr) && (m_cursor.m_locked_nodes == nullptr)); }
    // virtual bool is_serializable() const = 0;
    BtreeQueryType query_type() const { return m_query_type; }
    uint32_t get_batch_size() const { return m_batch_size; }
    void set_batch_size(uint32_t count) { m_batch_size = count; }

    match_item_cb_get_t< K, V > callback() const { return m_cb; }
    BRangeQueryCBParam< K, V >* get_cb_param() const { return (BRangeQueryCBParam< K, V >*)m_cb_param; }

protected:
    BtreeSearchRange m_batch_search_range; // Adjusted filter for current batch
    BtreeSearchRange m_start_range;        // Search Range contaning only start key
    BtreeSearchRange m_end_range;          // Search Range containing only end key
    BtreeQueryCursor m_cursor;             // An opaque cursor object for pagination
    BtreeQueryType m_query_type;           // Type of the query
    uint32_t m_batch_size; // Count of items needed in this batch. This value can be changed on every cursor iteration
    const match_item_cb_get_t< K, V > m_cb;
};
template < typename K, typename V >
using match_item_cb_update_t = std::function< btree_status_t(
    std::vector< std::pair< K, V > >&, std::vector< std::pair< K, V > >&, BRangeUpdateCBParam< K, V >*) >;
template < typename K, typename V >
using get_size_needed_cb_t = std::function< uint32_t(std::vector< std::pair< K, V > >&, BRangeUpdateCBParam< K, V >*) >;
template < typename K, typename V >
class BtreeUpdateRequest : public BRangeRequest {
public:
    BtreeUpdateRequest(BtreeSearchRange& search_range, match_item_cb_update_t< K, V > cb = nullptr,
                       get_size_needed_cb_t< K, V > size_cb = nullptr,
                       BRangeUpdateCBParam< K, V >* cb_param = nullptr) :
            BRangeRequest(cb_param, search_range),
            m_cb(cb),
            m_size_cb(size_cb) {}

    match_item_cb_update_t< K, V > callback() const { return m_cb; }
    BRangeUpdateCBParam< K, V >* get_cb_param() const { return (BRangeUpdateCBParam< K, V >*)m_cb_param; }
    get_size_needed_cb_t< K, V > get_size_needed_callback() { return m_size_cb; }

protected:
    const match_item_cb_update_t< K, V > m_cb;
    const get_size_needed_cb_t< K, V > m_size_cb;
};

#if 0
class BtreeSweepQueryRequest : public BtreeQueryRequest {
public:
    BtreeSweepQueryRequest(const BtreeSearchRange& criteria, uint32_t iter_count = 1000,
            const match_item_cb_t& match_item_cb = nullptr) :
            BtreeQueryRequest(criteria, iter_count, match_item_cb) {}

    BtreeSweepQueryRequest(const BtreeSearchRange &criteria, const match_item_cb_t& match_item_cb) :
            BtreeQueryRequest(criteria, 1000, match_item_cb) {}

    bool is_serializable() const { return false; }
};

class BtreeSerializableQueryRequest : public BtreeQueryRequest {
public:
    BtreeSerializableQueryRequest(const BtreeSearchRange &range, uint32_t iter_count = 1000,
                             const match_item_cb_t& match_item_cb = nullptr) :
            BtreeQueryRequest(range, iter_count, match_item_cb) {}

    BtreeSerializableQueryRequest(const BtreeSearchRange &criteria, const match_item_cb_t& match_item_cb) :
            BtreeSerializableQueryRequest(criteria, 1000, match_item_cb) {}

    bool is_serializable() const { return true; }
};
#endif

class BtreeNodeInfo : public BtreeValue {
private:
    bnodeid_t m_bnodeid;

public:
    BtreeNodeInfo() : BtreeNodeInfo(empty_bnodeid) {}
    explicit BtreeNodeInfo(const bnodeid_t& id) : m_bnodeid(id) {}
    BtreeNodeInfo& operator=(const BtreeNodeInfo& other) = default;

    bnodeid_t bnode_id() const { return m_bnodeid; }
    void set_bnode_id(bnodeid_t bid) { m_bnodeid = bid; }
    bool has_valid_bnode_id() const { return (m_bnodeid != empty_bnodeid); }

    sisl::blob get_blob() const override {
        sisl::blob b;
        b.size = sizeof(bnodeid_t);
        b.bytes = (uint8_t*)&m_bnodeid;
        return b;
    }

    void set_blob(const sisl::blob& b) override {
        DEBUG_ASSERT_EQ(b.size, sizeof(bnodeid_t));
        m_bnodeid = *(bnodeid_t*)b.bytes;
    }

    void copy_blob(const sisl::blob& b) override { set_blob(b); }

    void append_blob(const BtreeValue& new_val, BtreeValue& existing_val) override { set_blob(new_val.get_blob()); }

    void get_overlap_diff_kvs(BtreeKey* k1, BtreeValue* v1, BtreeKey* k2, BtreeValue* v2, uint32_t param,
                              diff_read_next_t& to_read,
                              std::vector< std::pair< BtreeKey, BtreeValue > >& overlap_kvs) override {}

    uint32_t get_blob_size() const override { return sizeof(bnodeid_t); }
    static uint32_t get_fixed_size() { return sizeof(bnodeid_t); }
    void set_blob_size(uint32_t size) override {}
    uint32_t estimate_size_after_append(const BtreeValue& new_val) override { return sizeof(bnodeid_t); }

    std::string to_string() const override { return fmt::format("{}", m_bnodeid); }

    friend std::ostream& operator<<(std::ostream& os, const BtreeNodeInfo& b) {
        os << b.m_bnodeid;
        return os;
    }
};

class EmptyClass : public BtreeValue {
public:
    EmptyClass() {}

    sisl::blob get_blob() const override {
        sisl::blob b;
        b.size = 0;
        b.bytes = (uint8_t*)this;
        return b;
    }

    void set_blob(const sisl::blob& b) override {}

    void copy_blob(const sisl::blob& b) override {}

    void append_blob(const BtreeValue& new_val, BtreeValue& existing_val) override {}

    static uint32_t get_fixed_size() { return 0; }

    uint32_t get_blob_size() const override { return 0; }

    void set_blob_size(uint32_t size) override {}

    void get_overlap_diff_kvs(BtreeKey* k1, BtreeValue* v1, BtreeKey* k2, BtreeValue* v2, uint32_t param,
                              diff_read_next_t& to_read,
                              std::vector< std::pair< BtreeKey, BtreeValue > >& overlap_kvs) override {}

    EmptyClass& operator=(const EmptyClass& other) { return (*this); }

    uint32_t estimate_size_after_append(const BtreeValue& new_val) override { return 0; }

    std::string to_string() const override { return "<Empty>"; }
};

typedef std::function< void() > trigger_cp_callback;
struct BtreeConfig {
    uint64_t m_max_objs;
    uint32_t m_max_key_size;
    uint32_t m_max_value_size;

    uint32_t m_node_area_size;
    uint32_t m_node_size;

    uint8_t m_ideal_fill_pct;
    uint8_t m_split_pct;

    std::string m_btree_name; // Unique name for the btree
    uint64_t align_size;
    trigger_cp_callback trigger_cp_cb;
    void* blkstore;

    BtreeConfig(uint32_t node_size, const char* btree_name = nullptr) {
        m_max_objs = 0;
        m_max_key_size = m_max_value_size = 0;
        m_ideal_fill_pct = 90;
        m_split_pct = 50;
        m_btree_name = btree_name ? btree_name : std::string("btree");
        m_node_size = node_size;
    }

    uint32_t get_node_size() { return m_node_size; };
    uint32_t get_max_key_size() const { return m_max_key_size; }
    void set_max_key_size(uint32_t max_key_size) { m_max_key_size = max_key_size; }

    uint64_t get_max_objs() const { return m_max_objs; }
    void set_max_objs(uint64_t max_objs) { m_max_objs = max_objs; }

    uint32_t get_max_value_size() const { return m_max_value_size; }
    uint32_t get_node_area_size() const { return m_node_area_size; }

    void set_node_area_size(uint32_t size) { m_node_area_size = size; }
    void set_max_value_size(uint32_t max_value_size) { m_max_value_size = max_value_size; }

    uint32_t get_ideal_fill_size() const { return (uint32_t)(get_node_area_size() * m_ideal_fill_pct) / 100; }
    uint32_t get_merge_suggested_size() const { return get_node_area_size() - get_ideal_fill_size(); }
    uint32_t get_split_size(uint32_t filled_size) const { return (uint32_t)(filled_size * m_split_pct) / 100; }
    const std::string& get_name() const { return m_btree_name; }
};

#define DEFAULT_FREELIST_CACHE_COUNT 10000
template < size_t NodeSize, size_t CacheCount = DEFAULT_FREELIST_CACHE_COUNT >
class BtreeNodeAllocator {
public:
    static BtreeNodeAllocator< NodeSize, CacheCount >* create() {
        bool initialized = bt_node_allocator_initialized.load(std::memory_order_acquire);
        if (!initialized) {
            auto allocator = std::make_unique< BtreeNodeAllocator< NodeSize, CacheCount > >();
            if (bt_node_allocator_initialized.compare_exchange_strong(initialized, true, std::memory_order_acq_rel)) {
                bt_node_allocator = std::move(allocator);
            }
        }
        return bt_node_allocator.get();
    }

    static uint8_t* allocate() {
        DEBUG_ASSERT_EQ(bt_node_allocator_initialized, true);
        return bt_node_allocator->get_allocator()->allocate(NodeSize);
    }

    static void deallocate(uint8_t* mem) {
        // LOG(INFO) << "Deallocating memory " << (void *)mem;
        bt_node_allocator->get_allocator()->deallocate(mem, NodeSize);
    }

    static std::atomic< bool > bt_node_allocator_initialized;
    static std::unique_ptr< BtreeNodeAllocator< NodeSize, CacheCount > > bt_node_allocator;

    auto get_allocator() { return &m_allocator; }

private:
    sisl::FreeListAllocator< CacheCount, NodeSize > m_allocator;
};

class BtreeMetrics : public sisl::MetricsGroupWrapper {
public:
    explicit BtreeMetrics(btree_store_type store_type, const char* inst_name) :
            sisl::MetricsGroupWrapper(enum_name(store_type), inst_name) {
        REGISTER_COUNTER(btree_obj_count, "Btree object count", sisl::_publish_as::publish_as_gauge);
        REGISTER_COUNTER(btree_leaf_node_count, "Btree Leaf node count", "btree_node_count", {"node_type", "leaf"},
                         sisl::_publish_as::publish_as_gauge);
        REGISTER_COUNTER(btree_int_node_count, "Btree Interior node count", "btree_node_count",
                         {"node_type", "interior"}, sisl::_publish_as::publish_as_gauge);
        REGISTER_COUNTER(btree_split_count, "Total number of btree node splits");
        REGISTER_COUNTER(insert_failed_count, "Total number of inserts failed");
        REGISTER_COUNTER(btree_merge_count, "Total number of btree node merges");
        REGISTER_COUNTER(btree_depth, "Depth of btree", sisl::_publish_as::publish_as_gauge);

        REGISTER_COUNTER(btree_int_node_writes, "Total number of btree interior node writes", "btree_node_writes",
                         {"node_type", "interior"});
        REGISTER_COUNTER(btree_leaf_node_writes, "Total number of btree leaf node writes", "btree_node_writes",
                         {"node_type", "leaf"});
        REGISTER_COUNTER(btree_num_pc_gen_mismatch, "Number of gen mismatches to recover");

        REGISTER_HISTOGRAM(btree_int_node_occupancy, "Interior node occupancy", "btree_node_occupancy",
                           {"node_type", "interior"}, HistogramBucketsType(ExponentialOfTwoBuckets));
        REGISTER_HISTOGRAM(btree_leaf_node_occupancy, "Leaf node occupancy", "btree_node_occupancy",
                           {"node_type", "leaf"}, HistogramBucketsType(ExponentialOfTwoBuckets));
        REGISTER_COUNTER(btree_retry_count, "number of retries");
        REGISTER_COUNTER(write_err_cnt, "number of errors in write");
        REGISTER_COUNTER(split_failed, "split failed");
        REGISTER_COUNTER(query_err_cnt, "number of errors in query");
        REGISTER_COUNTER(read_node_count_in_write_ops, "number of nodes read in write_op");
        REGISTER_COUNTER(read_node_count_in_query_ops, "number of nodes read in query_op");
        REGISTER_COUNTER(btree_write_ops_count, "number of btree operations");
        REGISTER_COUNTER(btree_query_ops_count, "number of btree operations");
        REGISTER_COUNTER(btree_remove_ops_count, "number of btree operations");
        REGISTER_HISTOGRAM(btree_exclusive_time_in_int_node,
                           "Exclusive time spent (Write locked) on interior node (ns)", "btree_exclusive_time_in_node",
                           {"node_type", "interior"});
        REGISTER_HISTOGRAM(btree_exclusive_time_in_leaf_node, "Exclusive time spent (Write locked) on leaf node (ns)",
                           "btree_exclusive_time_in_node", {"node_type", "leaf"});
        REGISTER_HISTOGRAM(btree_inclusive_time_in_int_node, "Inclusive time spent (Read locked) on interior node (ns)",
                           "btree_inclusive_time_in_node", {"node_type", "interior"});
        REGISTER_HISTOGRAM(btree_inclusive_time_in_leaf_node, "Inclusive time spent (Read locked) on leaf node (ns)",
                           "btree_inclusive_time_in_node", {"node_type", "leaf"});

        register_me_to_farm();
    }

    ~BtreeMetrics() { deregister_me_from_farm(); }
};

template < size_t NodeSize, size_t CacheCount >
std::atomic< bool > BtreeNodeAllocator< NodeSize, CacheCount >::bt_node_allocator_initialized(false);

template < size_t NodeSize, size_t CacheCount >
std::unique_ptr< BtreeNodeAllocator< NodeSize, CacheCount > >
    BtreeNodeAllocator< NodeSize, CacheCount >::bt_node_allocator = nullptr;

} // namespace btree
} // namespace homeds