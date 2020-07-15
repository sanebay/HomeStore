/*
 * device.h
 *
 *  Created on: 05-Aug-2016
 *      Author: Hari Kadayam
 */

#pragma once

#define BOOST_UUID_RANDOM_PROVIDER_FORCE_POSIX 1

#include <boost/intrusive/list.hpp>
#include <sys/uio.h>
#include <unistd.h>
#include <exception>
#include <string>
#include <sds_logging/logging.h>
#include <fcntl.h>
#include <boost/uuid/uuid.hpp>            // uuid class
#include <boost/uuid/uuid_generators.hpp> // generators
#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <fds/sparse_vector.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <fds/utils.hpp>
#include <isa-l/crc.h>
#include <iomgr/iomgr.hpp>
#include <cstddef>
#include <engine/homestore_base.hpp>
#include "engine/meta/meta_blks_mgr.hpp"

using namespace iomgr;

namespace homestore {
class BlkAllocator;
struct blkalloc_cp_id;

#define MAGIC 0xCEEDDEEB
#define PRODUCT_NAME "OmStore"

/************* Super Block definition ******************/

#define CURRENT_SUPERBLOCK_VERSION 1
#define CURRENT_DM_INFO_VERSION 1

/*******************************************************************************************************
 *  _______________________             _________________________________________________________      *
 * |                       |           |                  |            |             |            |    *
 * |  Super block header   |---------->| Super Block info | Pdev Block | Chunk Block | Vdev Block |    *
 * |_______________________|           |__________________|____________|_____________|____________|    *
 *                                                                                                     *
 *******************************************************************************************************/

/************* Physical Device Info Block definition ******************/

struct pdevs_block {
    uint64_t magic;         // Header magic expected to be at the top of block
    uint32_t num_phys_devs; // Total number of physical devices in the entire system
    uint32_t max_phys_devs;
    uint64_t info_offset;

    uint64_t get_magic() const { return magic; }
    uint32_t get_num_phys_devs() const { return num_phys_devs; }
} __attribute((packed));

struct pdev_info_block {
    uint32_t dev_num;        // Device ID for this store instance.
    uint32_t first_chunk_id; // First chunk id for this physical device
    uint64_t dev_offset;     // Start offset of the device in global offset

    uint32_t get_dev_num() const { return dev_num; }
    uint32_t get_first_chunk_id() const { return first_chunk_id; }
    uint64_t get_dev_offset() const { return dev_offset; }
} __attribute((packed));

/************* chunk Info Block definition ******************/

struct chunks_block {
    uint64_t magic;      // Header magic expected to be at the top of block
    uint32_t num_chunks; // Number of physical chunks for this block
    uint32_t max_num_chunks;
    uint64_t info_offset;

    uint64_t get_magic() const { return magic; }
    uint32_t get_num_chunks() const { return num_chunks; }
} __attribute((packed));

struct chunk_info_block {
    uint64_t chunk_start_offset; // Start offset of the chunk within a pdev
    uint64_t chunk_size;         // Chunk size
    uint32_t chunk_id;           // Chunk id in global scope. It is the index in the array of chunk_info_blks
    uint32_t pdev_id;            // Physical device id this chunk is hosted on
    uint32_t vdev_id;            // Virtual device id this chunk hosts. UINT32_MAX if chunk is free
    uint32_t prev_chunk_id;      // Prev pointer in the chunk
    uint32_t next_chunk_id;      // Next pointer in the chunk
    uint32_t primary_chunk_id;   // Valid chunk id if this is a mirror of some chunk
    bool slot_allocated;         // Is this slot allocated for any chunks.
    bool is_sb_chunk;            // This chunk is not assigned to any vdev but super block
    off_t end_of_chunk_offset;   // The offset indicates end of chunk.

    uint64_t get_chunk_size() const { return chunk_size; }
    uint32_t get_chunk_id() const { return chunk_id; }
    bool is_slot_allocated() const { return slot_allocated; }
} __attribute((packed));

/************* Vdev Info Block definition ******************/
struct vdevs_block {
    uint64_t magic;     // Header magic expected to be at the top of block
    uint32_t num_vdevs; // Number of virtual devices
    uint32_t max_num_vdevs;
    uint32_t first_vdev_id; // First vdev id / Head of the vdev list;
    uint64_t info_offset;
    uint32_t context_data_size;

    uint32_t get_num_vdevs() const { return num_vdevs; }
    uint64_t get_magic() const { return magic; }
    uint32_t get_first_vdev_id() const { return first_vdev_id; }
} __attribute((packed));

#define MAX_VDEV_INFO_BLOCK_SZ 4096
#define MAX_VDEV_INFO_BLOCK_HDR_SZ 512
#define MAX_CONTEXT_DATA_SZ (MAX_VDEV_INFO_BLOCK_SZ - MAX_VDEV_INFO_BLOCK_HDR_SZ)

struct vdev_info_block {
    uint32_t vdev_id;            // 0: Id for this vdev. It is a index in the array of vdev_info_blk
    uint64_t size;               // 4: Size of the vdev
    uint32_t num_mirrors;        // 12: Total number of mirrors
    uint32_t page_size;          // 16: IO block size for this vdev
    uint32_t prev_vdev_id;       // 20: Prev pointer of vdevice list
    uint32_t next_vdev_id;       // 24: Next pointer of vdevice list
    bool slot_allocated;         // 28: Is this current slot allocated
    bool failed;                 // 29: set to true if disk is replaced
    uint32_t num_primary_chunks; // 30: number of primary chunks
    off_t data_start_offset;     // 34: Logical offset for the 1st written data;
    uint8_t padding[MAX_VDEV_INFO_BLOCK_HDR_SZ - 42]; // Ugly hardcode will be removed after moving to superblk blkstore
    char context_data[MAX_CONTEXT_DATA_SZ];

    uint32_t get_vdev_id() const { return vdev_id; }
    uint64_t get_size() const { return size; }

    static constexpr uint32_t max_context_size() { return MAX_CONTEXT_DATA_SZ; }
} __attribute((packed));

// This assert is trying catch mistakes of overlaping the header to context_data portion.
static_assert(offsetof(vdev_info_block, context_data) == MAX_VDEV_INFO_BLOCK_HDR_SZ,
              "vdev info block header size should be size of 512 bytes!");

static_assert(sizeof(vdev_info_block) == MAX_VDEV_INFO_BLOCK_SZ, "vdev info block size should be 4096 bytes!");

/*******************Super Block Definition*******************/

/* This header should be atomically written to the disks. It should always be smaller then ssd atomic page size */
#define SUPERBLOCK_PAYLOAD_OFFSET 4096
struct super_block {
    char empty_buf[SUPERBLOCK_PAYLOAD_OFFSET]; // don't write anything to first 4096 bytes.
    uint64_t magic;                            // Header magic expected to be at the top of block
    uint32_t version;                          // Version Id of this structure
    uint64_t gen_cnt;
    char product_name[64]; // Product name
    int cur_indx;
    pdev_info_block this_dev_info; // Info about this device itself
    chunk_info_block dm_chunk[2];  // chunk info blocks
    boost::uuids::uuid system_uuid;

    uint64_t get_magic() const { return magic; }
} __attribute((packed));

#define SUPERBLOCK_SIZE (HS_STATIC_CONFIG(disk_attr.atomic_phys_page_size) + SUPERBLOCK_PAYLOAD_OFFSET)

struct dm_info {
    /* header of pdev, chunk and vdev */
    uint64_t magic;    // Header magic expected to be at the top of block
    uint16_t checksum; // Payload Checksum
    uint32_t version;
    uint64_t size;
    pdevs_block pdev_hdr;
    chunks_block chunk_hdr;
    vdevs_block vdev_hdr;

    uint64_t get_magic() const { return magic; }
    uint64_t get_size() const { return size; }
    uint32_t get_version() const { return version; }
    uint16_t get_checksum() const { return checksum; }
} __attribute((packed));

#define PDEV_INFO_BLK_OFFSET sizeof(dm_info)
#define CHUNK_INFO_BLK_OFFSET (PDEV_INFO_BLK_OFFSET + (sizeof(pdev_info_block) * HS_STATIC_CONFIG(engine.max_pdevs)))
#define VDEV_INFO_BLK_OFFSET (CHUNK_INFO_BLK_OFFSET + sizeof(chunk_info_block) * HS_STATIC_CONFIG(engine.max_chunks))

#define DM_INFO_BLK_SIZE (VDEV_INFO_BLK_OFFSET + HS_STATIC_CONFIG(engine.max_vdevs) * sizeof(vdev_info_block))

#define DM_PAYLOAD_OFFSET 10

#define INVALID_PDEV_ID UINT32_MAX
#define INVALID_VDEV_ID UINT32_MAX
#define INVALID_CHUNK_ID UINT32_MAX
#define INVALID_DEV_ID UINT32_MAX

class PhysicalDev;

class DeviceManager;
typedef std::function< void(int status, uint8_t* cookie) > comp_callback;

class PhysicalDevChunk {
public:
    friend class DeviceManager;

    PhysicalDevChunk(PhysicalDev* pdev, chunk_info_block* cinfo);
    PhysicalDevChunk(PhysicalDev* pdev, uint32_t chunk_id, uint64_t start_offset, uint64_t size,
                     chunk_info_block* cinfo);

    const PhysicalDev* get_physical_dev() const { return m_pdev; }

    DeviceManager* device_manager() const;

    PhysicalDev* get_physical_dev_mutable() { return m_pdev; };

    void set_blk_allocator(std::shared_ptr< homestore::BlkAllocator > alloc) { m_allocator = alloc; }

    std::shared_ptr< BlkAllocator > get_blk_allocator() { return m_allocator; }

    void set_sb_chunk() { m_chunk_info->is_sb_chunk = true; }
    void set_start_offset(uint64_t offset) { m_chunk_info->chunk_start_offset = offset; }

    uint64_t get_start_offset() const { return m_chunk_info->chunk_start_offset; }

    void set_size(uint64_t size) { m_chunk_info->chunk_size = size; }

    uint64_t get_size() const { return m_chunk_info->chunk_size; }

    bool is_busy() const { return (m_chunk_info->vdev_id != INVALID_VDEV_ID || m_chunk_info->is_sb_chunk); }

    void set_free() {
        set_vdev_id(INVALID_VDEV_ID);
        m_chunk_info->primary_chunk_id = INVALID_CHUNK_ID;
        m_chunk_info->is_sb_chunk = false;
    }

    uint32_t get_vdev_id() const { return m_chunk_info->vdev_id; }

    void set_vdev_id(uint32_t vdev_id) { m_chunk_info->vdev_id = vdev_id; }

    void set_next_chunk_id(uint32_t next_chunk_id) { m_chunk_info->next_chunk_id = next_chunk_id; }

    void set_next_chunk(PhysicalDevChunk* next_chunk) {
        set_next_chunk_id(next_chunk ? next_chunk->get_chunk_id() : INVALID_CHUNK_ID);
    }

    uint32_t get_next_chunk_id() const { return m_chunk_info->next_chunk_id; }

    PhysicalDevChunk* get_next_chunk() const;

    void set_prev_chunk_id(uint32_t prev_chunk_id) { m_chunk_info->prev_chunk_id = prev_chunk_id; }

    void set_prev_chunk(PhysicalDevChunk* prev_chunk) {
        set_prev_chunk_id(prev_chunk ? prev_chunk->get_chunk_id() : INVALID_CHUNK_ID);
    }

    uint32_t get_prev_chunk_id() const { return m_chunk_info->prev_chunk_id; }

    PhysicalDevChunk* get_prev_chunk() const;

    chunk_info_block* get_chunk_info() { return m_chunk_info; }
    uint16_t get_chunk_id() const { return (uint16_t)m_chunk_info->chunk_id; }

    void free_slot() { m_chunk_info->slot_allocated = false; }

    PhysicalDevChunk* get_primary_chunk() const;

    void set_primary_chunk_id(uint32_t primary_id) { m_chunk_info->primary_chunk_id = primary_id; }

    std::string to_string() {
        std::stringstream ss;
        ss << "chunk_id = " << get_chunk_id() << " pdev_id = " << m_chunk_info->pdev_id
           << " vdev_id = " << m_chunk_info->vdev_id << " start_offset = " << m_chunk_info->chunk_start_offset
           << " size = " << m_chunk_info->chunk_size << " prev_chunk_id = " << m_chunk_info->prev_chunk_id
           << " next_chunk_id = " << m_chunk_info->next_chunk_id << " busy? = " << is_busy()
           << " slot_allocated? = " << m_chunk_info->slot_allocated;
        return ss.str();
    }

    void update_end_of_chunk(off_t off) { m_chunk_info->end_of_chunk_offset = off; }
    off_t get_end_of_chunk() { return m_chunk_info->end_of_chunk_offset; }

    void recover(std::unique_ptr< sisl::Bitset > recovered_bm, meta_blk* mblk);

    void recover();

    void cp_start(std::shared_ptr< blkalloc_cp_id > id);

    static std::shared_ptr< blkalloc_cp_id > attach_prepare_cp(std::shared_ptr< blkalloc_cp_id > cur_cp_id);

    void cp_done(std::shared_ptr< blkalloc_cp_id > id);

private:
    chunk_info_block* m_chunk_info;
    PhysicalDev* m_pdev;
    std::shared_ptr< BlkAllocator > m_allocator;
    uint64_t m_vdev_metadata_size;
    void* m_meta_blk_cookie = nullptr;
    std::unique_ptr< sisl::Bitset > m_recovered_bm;
};

class PhysicalDevMetrics : public sisl::MetricsGroupWrapper {
public:
    explicit PhysicalDevMetrics(const std::string& devname) : sisl::MetricsGroupWrapper("PhysicalDev", devname) {
        REGISTER_COUNTER(drive_sync_write_count, "Drive sync write count");
        REGISTER_COUNTER(drive_sync_read_count, "Drive sync read count");
        REGISTER_COUNTER(drive_async_write_count, "Drive async write count");
        REGISTER_COUNTER(drive_async_read_count, "Drive async read count");
        REGISTER_COUNTER(drive_write_vector_count, "Total Count of buffer provided for write");
        REGISTER_COUNTER(drive_read_vector_count, "Total Count of buffer provided for read");
        REGISTER_COUNTER(drive_read_errors, "Total drive read errors");
        REGISTER_COUNTER(drive_write_errors, "Total drive write errors");
        REGISTER_COUNTER(drive_spurios_events, "Total number of spurious events per drive");

        REGISTER_HISTOGRAM(drive_write_latency, "BlkStore drive write latency in us");
        REGISTER_HISTOGRAM(drive_read_latency, "BlkStore drive read latency in us");

        register_me_to_farm();
    }
};

class PhysicalDev {
    friend class PhysicalDevChunk;
    friend class DeviceManager;

public:
    PhysicalDev(DeviceManager* mgr, const std::string& devname, int const oflags, boost::uuids::uuid& uuid,
                uint32_t dev_num, uint64_t dev_offset, uint32_t is_file, bool is_init, uint64_t dm_info_size,
                bool* is_inited);
    ~PhysicalDev();

    void update(uint32_t dev_num, uint64_t dev_offset, uint32_t first_chunk_id);
    void attach_superblock_chunk(PhysicalDevChunk* chunk);
    uint64_t sb_gen_cnt();
    size_t get_total_cap();

    std::string get_devname() const { return m_devname; }
    uint64_t get_size() const { return m_devsize; }
    uint32_t get_first_chunk_id() const { return m_info_blk.first_chunk_id; }
    uint64_t get_dev_offset() const { return m_info_blk.dev_offset; }
    uint32_t get_dev_id() const { return m_info_blk.dev_num; }
    PhysicalDevMetrics& get_metrics() { return m_metrics; }

    void set_dev_offset(uint64_t offset) { m_info_blk.dev_offset = offset; }
    void set_dev_id(uint32_t id) { m_info_blk.dev_num = id; }

    DeviceManager* device_manager() const { return m_mgr; }

    std::string to_string();
    /* Attach the given chunk to the list of chunks in the physical device. Parameter after provides the position
     * it needs to attach after. If null, attach to the end */
    void attach_chunk(PhysicalDevChunk* chunk, PhysicalDevChunk* after);

    /* Merge previous and next chunk from the chunk, if either one or both of them free. Returns the array of
     * chunk id which were merged and can be freed if needed */
    std::array< uint32_t, 2 > merge_free_chunks(PhysicalDevChunk* chunk);

    /* Find a free chunk which closestly match for the required size */
    PhysicalDevChunk* find_free_chunk(uint64_t req_size);

    void write(const char* data, uint32_t size, uint64_t offset, uint8_t* cookie, bool part_of_batch = false);
    void writev(const iovec* iov, int iovcnt, uint32_t size, uint64_t offset, uint8_t* cookie,
                bool part_of_batch = false);

    void read(char* data, uint32_t size, uint64_t offset, uint8_t* cookie, bool part_of_batch = false);
    void readv(const iovec* iov, int iovcnt, uint32_t size, uint64_t offset, uint8_t* cookie,
               bool part_of_batch = false);

    ssize_t sync_write(const char* data, uint32_t size, uint64_t offset);
    ssize_t sync_writev(const struct iovec* iov, int iovcnt, uint32_t size, uint64_t offset);

    ssize_t sync_read(char* data, uint32_t size, uint64_t offset);
    ssize_t sync_readv(const struct iovec* iov, int iovcnt, uint32_t size, uint64_t offset);
    pdev_info_block get_info_blk();
    void read_dm_chunk(char* mem, uint64_t size);
    void write_dm_chunk(uint64_t gen_cnt, char* mem, uint64_t size);
    uint64_t inc_error_cnt() { return (m_error_cnt.increment(1)); }

private:
    inline void write_superblock();
    inline void read_superblock();

    /* Load the physical device info from persistent storage. If its not a valid device, it will throw
     * std::system_exception. Returns true if the device has already formatted for Omstore, false otherwise. */
    bool load_super_block();

    /* Format the physical device info. Intended to use first time or anytime we need to reformat the drives. Throws
     * std::system_exception if there is any write errors */
    void write_super_block(uint64_t gen_cnt);

    /* Validate if this device is a homestore validated device. If there is any corrupted device, then it
     * throws std::system_exception */
    bool validate_device();

private:
    DeviceManager* m_mgr; // Back pointer to physical device
    io_device_ptr m_iodev;
    std::string m_devname;
    super_block* m_super_blk; // Persisent header block
    uint64_t m_devsize;
    struct pdev_info_block m_info_blk;
    PhysicalDevChunk* m_dm_chunk[2];
    PhysicalDevMetrics m_metrics; // Metrics instance per physical device
    int m_cur_indx;
    bool m_superblock_valid;
    boost::uuids::uuid m_system_uuid;
    sisl::atomic_counter< uint64_t > m_error_cnt;
};

class AbstractVirtualDev {
public:
    virtual void add_chunk(PhysicalDevChunk* chunk) = 0;
};

class DeviceManager {
    typedef std::function< void(DeviceManager*, vdev_info_block*) > NewVDevCallback;
    typedef std::function< void(PhysicalDevChunk*) > chunk_add_callback;
    typedef std::function< void(vdev_info_block*) > vdev_error_callback;

    friend class PhysicalDev;
    friend class PhysicalDevChunk;

public:
    DeviceManager(NewVDevCallback vcb, uint32_t const vdev_metadata_size,
                  const iomgr::io_interface_comp_cb_t& io_comp_cb, bool is_file, boost::uuids::uuid system_uuid,
                  const vdev_error_callback& vdev_error_cb);

    ~DeviceManager();

    /* Initial routine to call upon bootup or everytime new physical devices to be added dynamically */
    void add_devices(std::vector< dev_info >& devices, bool is_init);
    size_t get_total_cap(void);
    void handle_error(PhysicalDev* pdev);

    /* This is not very efficient implementation of get_all_devices(), however, this is expected to be called during
     * the start of the devices and for that purpose its efficient enough */
    std::vector< PhysicalDev* > get_all_devices() {
        std::vector< PhysicalDev* > vec;
        std::lock_guard< decltype(m_dev_mutex) > lock(m_dev_mutex);

        vec.reserve(m_pdevs.size());
        for (auto& pdev : m_pdevs) {
            if (pdev) vec.push_back(pdev.get());
        }
        return vec;
    }

    /* Allocate a chunk for required size on the given physical dev and associate the chunk to provide virtual device.
     * Returns the allocated PhysicalDevChunk */
    PhysicalDevChunk* alloc_chunk(PhysicalDev* pdev, uint32_t vdev_id, uint64_t req_size, uint32_t primary_id);

    /* Free the chunk for later user */
    void free_chunk(PhysicalDevChunk* chunk);

    /* Allocate a new vdev for required size */
    vdev_info_block* alloc_vdev(uint32_t req_size, uint32_t nmirrors, uint32_t blk_size, uint32_t nchunks, char* blob,
                                uint64_t size);

    /* Free up the vdev_id */
    void free_vdev(vdev_info_block* vb);

    /* Given an ID, get the chunk */
    PhysicalDevChunk* get_chunk(uint32_t chunk_id) const {
        return (chunk_id == INVALID_CHUNK_ID) ? nullptr : m_chunks[chunk_id].get();
    }

    PhysicalDevChunk* get_chunk_mutable(uint32_t chunk_id) {
        return (chunk_id == INVALID_CHUNK_ID) ? nullptr : m_chunks[chunk_id].get();
    }

    PhysicalDev* get_pdev(uint32_t pdev_id) const {
        return (pdev_id == INVALID_PDEV_ID) ? nullptr : m_pdevs[pdev_id].get();
    }

    void add_chunks(uint32_t vid, chunk_add_callback cb);
    void inited();
    void write_info_blocks();
    void update_vb_context(uint32_t vdev_id, const sisl::blob& ctx_data);
    void get_vb_context(uint32_t vdev_id, const sisl::blob& ctx_data);

    void update_end_of_chunk(PhysicalDevChunk* chunk, off_t offset);
    void update_vb_data_start_offset(uint32_t vdev_id, off_t offset);

private:
    void load_and_repair_devices(std::vector< dev_info >& devices);
    void init_devices(std::vector< dev_info >& devices);

    void read_info_blocks(uint32_t dev_id);

    chunk_info_block* alloc_new_chunk_slot(uint32_t* pslot_num);
    vdev_info_block* alloc_new_vdev_slot();

    PhysicalDevChunk* create_new_chunk(PhysicalDev* pdev, uint64_t start_offset, uint64_t size,
                                       PhysicalDevChunk* prev_chunk);
    void remove_chunk(uint32_t chunk_id);
    void blk_alloc_meta_blk_found_cb(meta_blk* mblk, sisl::byte_view buf, size_t size);

private:
    int m_open_flags;
    NewVDevCallback m_new_vdev_cb;
    std::atomic< uint64_t > m_gen_cnt;
    bool m_is_file;

    char* m_chunk_memory;

    /* This memory is carved out of chunk memory. Any changes in any of the block should end up writing all the blocks
     * on disk.
     */
    dm_info* m_dm_info;
    pdevs_block* m_pdev_hdr;
    chunks_block* m_chunk_hdr;
    vdevs_block* m_vdev_hdr;
    pdev_info_block* m_pdev_info;
    chunk_info_block* m_chunk_info;
    vdev_info_block* m_vdev_info;

    std::mutex m_dev_mutex;

    sisl::sparse_vector< std::unique_ptr< PhysicalDev > > m_pdevs;
    sisl::sparse_vector< std::unique_ptr< PhysicalDevChunk > > m_chunks;
    sisl::sparse_vector< AbstractVirtualDev* > m_vdevs;
    uint32_t m_last_vdevid;
    uint32_t m_vdev_metadata_size; // Appln metadata size for vdev
    uint32_t m_pdev_id;
    bool m_scan_cmpltd;
    uint64_t m_dm_info_size;
    boost::uuids::uuid m_system_uuid;
    vdev_error_callback m_vdev_error_cb;
};

} // namespace homestore