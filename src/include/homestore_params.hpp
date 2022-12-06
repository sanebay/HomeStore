#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include "homestore_types.hpp"

namespace homestore {
ENUM(HSDevType, uint8_t, Data, Fast);
ENUM(Op_type, uint8_t, READ, WRITE, UNMAP);
VENUM(PhysicalDevGroup, uint8_t, DATA = 0, FAST = 1, META = 2);

struct dev_info {
    explicit dev_info(std::string name, HSDevType type = HSDevType::Data) :
            dev_names{std::move(name)}, dev_type{type} {}
    std::string to_string() const { return fmt::format("{} - {}", dev_names, enum_name(dev_type)); }

    std::string dev_names;
    HSDevType dev_type;
};

ENUM(io_flag, uint8_t,
     BUFFERED_IO, // should be set if file system doesn't support direct IOs and we are working on a file as a
                  // disk. This option is enabled only on in debug build.
     DIRECT_IO,   // recommended mode
     READ_ONLY    // Read-only mode for post-mortem checks
);

static std::string _format_decimals(double val, const char* suffix) {
    return (val != (uint64_t)val) ? fmt::format("{:.2f}{}", val, suffix) : fmt::format("{}{}", val, suffix);
}

static std::string in_bytes(uint64_t sz) {
    static constexpr std::array< std::pair< uint64_t, const char* >, 5 > arr{
        std::make_pair(1, ""), std::make_pair(1024, "kb"), std::make_pair(1048576, "mb"),
        std::make_pair(1073741824, "gb"), std::make_pair(1099511627776, "tb")};

    const double size = (double)sz;
    for (size_t i{1}; i < arr.size(); ++i) {
        if ((size / arr[i].first) < 1) { return _format_decimals(size / arr[i - 1].first, arr[i - 1].second); }
    }
    return _format_decimals(size / arr.back().first, arr.back().second);
}

struct hs_input_params {
public:
    std::vector< dev_info > data_devices; // name of the data devices.
    uuid_t system_uuid;                   // Deprecated. UUID assigned to the system

    io_flag data_open_flags{io_flag::DIRECT_IO}; // All data drives open flags
    io_flag fast_open_flags{io_flag::DIRECT_IO}; // All index drives open flags

    uint32_t min_virtual_page_size{4096}; // minimum page size supported. Ideally it should be 4k.
    uint64_t app_mem_size{static_cast< uint64_t >(1024) * static_cast< uint64_t >(1024) *
                          static_cast< uint64_t >(1024)}; // memory available for the app (including cache)
    uint64_t hugepage_size{0};                            // memory available for the hugepage
    bool is_read_only{false};                             // Is read only
    bool auto_recovery{true};                             // Recovery of data is automatic or controlled by the caller
    // std::unordered_map< service_t, service_options > services;

#ifdef _PRERELEASE
    bool force_reinit{false};
#endif

    nlohmann::json to_json() const {
        nlohmann::json json;
        json["system_uuid"] = boost::uuids::to_string(system_uuid);
        json["devices"] = nlohmann::json::array();
        for (const auto& d : data_devices) {
            json["devices"].push_back(d.to_string());
        }
        json["data_open_flags"] = data_open_flags;
        json["fast_open_flags"] = fast_open_flags;
        json["is_read_only"] = is_read_only;

        json["min_virtual_page_size"] = in_bytes(min_virtual_page_size);
        json["app_mem_size"] = in_bytes(app_mem_size);
        json["hugepage_size"] = in_bytes(hugepage_size);
        json["auto_recovery?"] = auto_recovery;

        return json;
    }
    std::string to_string() const { return to_json().dump(4); }
    uint64_t io_mem_size() const { return (hugepage_size != 0) ? hugepage_size : app_mem_size; }
};

struct hs_engine_config {
    size_t min_io_size{4096};        // minimum io size supported by
    uint64_t max_chunks{MAX_CHUNKS}; // These 3 parameters can be ONLY changed with upgrade/revert from device manager
    uint64_t max_vdevs{MAX_VDEVS};
    uint64_t max_pdevs{MAX_PDEVS};
    uint32_t max_blks_in_blkentry{1}; // Max blks a represents in a single BlkId entry

    nlohmann::json to_json() const {
        nlohmann::json json;
        json["min_io_size"] = in_bytes(min_io_size);
        json["max_chunks"] = max_chunks;
        json["max_vdevs"] = max_vdevs;
        json["max_pdevs"] = max_pdevs;
        json["max_blks_in_blkentry"] = max_blks_in_blkentry;
        return json;
    }
};
} // namespace homestore