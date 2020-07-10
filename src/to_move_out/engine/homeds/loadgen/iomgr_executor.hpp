#pragma once
#include <folly/MPMCQueue.h>
#include <iomgr/iomgr.hpp>

namespace homeds {
namespace loadgen {

class IOMgrExecutor {
    typedef std::function< void() > callback_t;

public:
    // Create a bounded lock protected queue.
    IOMgrExecutor(int num_threads, int num_priorities, uint32_t max_queue_size);
    ~IOMgrExecutor();

    // Queues this function to execute in other thread and return back.
    // If the num_entries in queue > size given in max_queue_size, block and wait until queue becomes less.
    // IOMgr thread should dequeue the requests and start executing.
    void add(callback_t done_cb);
    bool is_empty();
    void stop(bool wait_io_complete = true);

private:
    // void process_ev_callback(const int fd, const void* cookie, const int event);
    void handle_iothread_msg(const iomgr::iomgr_msg& msg);
    void process_new_request();
    bool is_running() const;
    void start();

private:
    folly::MPMCQueue< callback_t, std::atomic, true > m_cq;
    // int m_ev_fd;
    // std::shared_ptr< iomgr::fd_info > m_ev_fdinfo = nullptr;
    std::atomic_bool m_running;
    std::atomic_ullong m_read_cnt;
    std::atomic_ullong m_write_cnt;
};

} // namespace loadgen
} // namespace homeds