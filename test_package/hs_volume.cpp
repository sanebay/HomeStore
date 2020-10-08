#include <mutex>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

/* Facility headers */
#include <sds_logging/logging.h>
#include <sds_options/options.h>

/* IOPath */
#include <iomgr/iomgr.hpp>
#include <api/vol_interface.hpp>
#include <engine/common/homestore_header.hpp>

THREAD_BUFFER_INIT;

SDS_OPTION_GROUP(test_hs_vol,
                 (capacity, "", "capacity", "Size of volume", cxxopts::value< uint32_t >()->default_value("2"), "GiB"),
                 (io_threads, "", "io_threads", "Number of IO threads",
                  cxxopts::value< uint32_t >()->default_value("1"), "count"),
                 (name, "", "name", "Volume name", cxxopts::value< std::string >()->default_value("volume"), ""),
                 (addr, "", "addr", "Do IO on a PCIe address", cxxopts::value< std::string >(), "0000:02:00.0"))


#define ENABLED_OPTIONS logging, iomgr, home_blks, test_hs_vol
#define SPDK_LOG_MODS HOMESTORE_LOG_MODS

SDS_OPTIONS_ENABLE(ENABLED_OPTIONS)
SDS_LOGGING_INIT(SPDK_LOG_MODS)

constexpr size_t Ki = 1024;
constexpr size_t Mi = Ki * Ki;
constexpr size_t Gi = Ki * Mi;

static void init_homestore(boost::uuids::uuid const& system_uuid, std::string const& device_address) {
    std::condition_variable wait_cv;
    std::mutex init_lock;
    bool init_done{false};

    homestore::dev_info dev;
    dev.dev_names = device_address;

    homestore::init_params params;
    params.devices.push_back(dev);
    params.system_uuid = system_uuid;
    params.min_virtual_page_size = 4 * Ki;
    params.app_mem_size = 2 * Gi;
    params.disk_init = true;
    params.init_done_cb = [&](std::error_condition, struct homestore::out_params) mutable {
        LOGDEBUG("Homestore completed initialization");
        {
            std::lock_guard< std::mutex > lg(init_lock);
            init_done = true;
        }
        wait_cv.notify_one();
    };
    params.vol_mounted_cb = [&](std::shared_ptr< homestore::Volume > v, homestore::vol_state s) mutable {};
    params.vol_state_change_cb = [](std::shared_ptr< homestore::Volume >, homestore::vol_state, homestore::vol_state) {
    };
    params.vol_found_cb = [](boost::uuids::uuid) -> bool { return true; };

    homestore::VolInterface::init(params);
    {
        std::unique_lock< std::mutex > lk(init_lock);
        wait_cv.wait(lk, [&] { return init_done; });
    }
    LOGINFO("Volume interface init success.\n");
}

int main(int argc, char* argv[]) {
    SDS_OPTIONS_LOAD(argc, argv, ENABLED_OPTIONS)
    sds_logging::SetLogger("spdk_volume");
    sds_logging::install_crash_handler();
    spdlog::set_pattern("[%D %T.%e] [%n] [%^%l%$] [%t] %v");

    // Create a random Node ID for ourselves.
    auto const system_uuid = boost::uuids::random_generator()();

    // Configure backend BDev
    std::string device_address;
    if (0 < SDS_OPTIONS.count("addr")) {
        device_address = std::string(fmt::format("traddr={}", SDS_OPTIONS["addr"].as< std::string >()));
        LOGINFO("Binding device [{}] to SPDK NVMe-BDEV [{}...]", device_address, to_string(system_uuid));
    } else {
        LOGERROR("Please use --addr in the argument list.");
        return -1;
    }

    // Start the IOManager
    iomanager.start(SDS_OPTIONS["io_threads"].as< uint32_t >(), true);

    // Init HomeStore
    init_homestore(system_uuid, device_address);

    auto vol_interface = homestore::VolInterface::get_instance();
    if (!vol_interface) {
        LOGERROR("Could not lookup VolInterface");
        iomanager.stop();
        return -1;
    }

    // Create a volume within HomeBlks
    homestore::vol_params vol_params;
    auto const vol_name = SDS_OPTIONS["name"].as< std::string >();
    vol_params.page_size = 4 * Ki;
    vol_params.size = SDS_OPTIONS["capacity"].as< uint32_t >() * Gi; /* assuming unit is Bytes */
    vol_params.uuid = boost::uuids::random_generator()();
    auto const vol_uuid = boost::uuids::to_string(vol_params.uuid);
    LOGINFO("Creating volume: name={}, uuid={}, size={}, page_size={}", vol_name, vol_uuid, vol_params.size,
            vol_params.page_size);
    strcpy(vol_params.vol_name, vol_name.c_str());
    vol_interface->create_volume(vol_params);
    RELEASE_ASSERT(!vol_interface->remove_volume(vol_params.uuid), "Could not remove volume!");
    iomanager.stop();
    LOGINFO("Done.");
    return 0;
}