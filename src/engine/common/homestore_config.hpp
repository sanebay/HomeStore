
#ifndef _HOMESTORE_CONFIG_HPP_
#define _HOMESTORE_CONFIG_HPP_

#include "homestore_header.hpp"
#include <engine/common/error.h>
#include <cassert>
#include <boost/intrusive_ptr.hpp>
#include <settings/settings.hpp>
#include "generated/homestore_config_generated.h"
#include <nlohmann/json.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/optional.hpp>

SETTINGS_INIT(homestorecfg::HomeStoreSettings, homestore_config);

/* DM info size depends on these three parameters. If below parameter changes then we have to add
 * the code for upgrade/revert.
 */
constexpr uint32_t MAX_CHUNKS = 128;
constexpr uint32_t MAX_VDEVS = 16;
constexpr uint32_t MAX_PDEVS = 8;

namespace homestore {
#define HS_DYNAMIC_CONFIG_WITH(...) SETTINGS(homestore_config, __VA_ARGS__)
#define HS_DYNAMIC_CONFIG_THIS(...) SETTINGS_THIS(homestore_config, __VA_ARGS__)
#define HS_DYNAMIC_CONFIG(...) SETTINGS_VALUE(homestore_config, __VA_ARGS__)

#define HS_STATIC_CONFIG(cfg) homestore::HomeStoreStaticConfig::instance().cfg

/* This is the optional parameteres which should be given by its consumers only when there is no
 * system command to get these parameteres directly from disks. Or Consumer want to override
 * the default values.
 */
struct disk_attributes {
    uint32_t phys_page_size;        // page size of ssds. It should be same for all the disks.
                                    // It shouldn't be less then 8k
    uint32_t align_size;            // size alignment supported by disks. It should be
                                    // same for all the disks.
    uint32_t atomic_phys_page_size; // atomic page size of the disk

    nlohmann::json to_json() const {
        nlohmann::json json;
        json["phys_page_size"] = phys_page_size;
        json["align_size"] = align_size;
        json["atomic_phys_page_size"] = atomic_phys_page_size;
        return json;
    }
};

struct cap_attrs {
    uint64_t used_data_size;
    uint64_t used_index_size;
    uint64_t used_total_size;
    uint64_t initial_total_size;
    std::string to_string() {
        std::stringstream ss;
        ss << "used_data_size = " << used_data_size << ", used_index_size = " << used_index_size
           << ", used_total_size = " << used_total_size << ", initial_total_size = " << initial_total_size;
        return ss.str();
    }
};

struct hs_input_params {
public:
    std::vector< dev_info > devices; // name of the devices.
    bool is_file = false;            // Is the devices a file or raw device
    boost::uuids::uuid system_uuid;  // UUID assigned to the system
    io_flag open_flags = io_flag::DIRECT_IO;

    uint32_t min_virtual_page_size = 4096;          // minimum page size supported. Ideally it should be 4k.
    uint64_t app_mem_size = 1 * 1024 * 1024 * 1024; // memory available for the app (including cache)
    bool disk_init = false;                         // true if disk has to be initialized.
    bool is_read_only = false;                      // Is read only

    /* optional parameters - if provided will override the startup config */
    boost::optional< disk_attributes > disk_attr;

    nlohmann::json to_json() const {
        nlohmann::json json;
        json["system_uuid"] = boost::uuids::to_string(system_uuid);
        json["devices"] = nlohmann::json::array();
        for (auto& d : devices) {
            json["devices"].push_back(d.dev_names);
        }
        json["open_flags"] = open_flags;
        json["is_file"] = is_file;
        json["is_read_only"] = is_read_only;

        json["min_virtual_page_size"] = min_virtual_page_size;
        json["app_mem_size"] = app_mem_size;

        return json;
    }
};

struct hs_engine_config {
    size_t min_io_size = 8192; // minimum io size supported by HS
    uint64_t max_blk_cnt = 0;  // Total number of blks engine can support

    uint64_t max_chunks = MAX_CHUNKS; // These 3 parameters can be ONLY changed with upgrade/revert from device manager
    uint64_t max_vdevs = MAX_VDEVS;
    uint64_t max_pdevs = MAX_PDEVS;

    nlohmann::json to_json() const {
        nlohmann::json json;
        json["min_io_size"] = min_io_size;
        json["max_blk_count_supported"] = max_blk_cnt;
        json["max_chunks"] = max_chunks;
        json["max_vdevs"] = max_vdevs;
        json["max_pdevs"] = max_pdevs;
        return json;
    }
};

struct HomeStoreStaticConfig {
    static HomeStoreStaticConfig& instance() {
        static HomeStoreStaticConfig s_inst;
        return s_inst;
    }

    disk_attributes disk_attr;
    hs_engine_config engine;
    hs_input_params input;

    nlohmann::json to_json() const {
        nlohmann::json json;
        json["DriveAttributes"] = disk_attr.to_json();
        json["GenericConfig"] = engine.to_json();
        json["InputParameters"] = input.to_json();
        return json;
    }

#ifndef NDEBUG
    void validate() {
        assert(disk_attr.phys_page_size >= disk_attr.atomic_phys_page_size);
        assert(disk_attr.phys_page_size >= engine.min_io_size);
    }
#endif
};

constexpr uint32_t ID_BITS = 32;
constexpr uint32_t NBLKS_BITS = 8;
constexpr uint32_t CHUNK_NUM_BITS = 8;
constexpr uint32_t BLKID_SIZE_BITS = ID_BITS + NBLKS_BITS + CHUNK_NUM_BITS;
constexpr uint32_t MEMPIECE_ENCODE_MAX_BITS = 8;
constexpr uint64_t MAX_NBLKS = ((1 << NBLKS_BITS) - 1);
constexpr uint64_t MAX_CHUNK_ID = ((1 << CHUNK_NUM_BITS) - 1);
constexpr uint64_t BLKID_SIZE = ((ID_BITS + NBLKS_BITS + CHUNK_NUM_BITS) / 8);
constexpr uint32_t BLKS_PER_PORTION = 1024;
constexpr uint32_t TOTAL_SEGMENTS = 8;
constexpr uint32_t MAX_ID_BITS_PER_CHUNK = ((1lu << ID_BITS) - 1);

/* NOTE: it can give size more then the size passed in argument to make it aligned */
// #define ALIGN_SIZE(size, align) (((size % align) == 0) ? size : (size + (align - (size % align))))

/* NOTE: it can give size less then size passed in argument to make it aligned */
// #define ALIGN_SIZE_TO_LEFT(size, align) (((size % align) == 0) ? size : (size - (size % align)))

#define MEMVEC_MAX_IO_SIZE (HS_STATIC_CONFIG(engine.min_io_size) * ((1 << MEMPIECE_ENCODE_MAX_BITS) - 1))
#define MIN_CHUNK_SIZE (HS_STATIC_CONFIG(disk_attr.phys_page_size) * BLKS_PER_PORTION * TOTAL_SEGMENTS)
#define MAX_CHUNK_SIZE                                                                                                 \
    sisl::round_down((MAX_ID_BITS_PER_CHUNK * HS_STATIC_CONFIG(engine.min_io_size)), MIN_CHUNK_SIZE) // 16T

/* TODO: we store global unique ID in blkid. Instead it we only store chunk offset then
 * max cacapity will increase from MAX_CHUNK_SIZE to MAX_CHUNKS * MAX_CHUNK_SIZE.
 */
#define MAX_SUPPORTED_CAP (MAX_CHUNKS * MAX_CHUNK_SIZE)

#define MAX_UUID_LEN 128

/* 1 % of disk space is reserved for volume sb chunks. With 8k page it
 * will come out to be around 7 GB.
 */
#define MIN_DISK_CAP_SUPPORTED (MIN_CHUNK_SIZE * 100 / 99 + MIN_CHUNK_SIZE)

} // namespace homestore
#endif