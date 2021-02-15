#include "umcache.hpp"
#include "utility.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/eventfd.h>
#include <linux/userfaultfd.h>
#include <poll.h>

#include <thread>
#include <cstring>

#define MREMAP_DONTUNMAP 4

static constexpr const std::size_t TAG_USED = (std::size_t(1) << 63);
static constexpr const std::size_t TAG_MASK = (std::size_t(1) << 63) - 1;

struct EventFd
{
    int fd = -1;
    EventFd() {
        this->fd = eventfd(0, O_NONBLOCK);
    }
    ~EventFd() {
        if( this->fd >= 0 ) {
            close(this->fd);
        }
        this->fd = -1;
    }
    void put() {
        eventfd_write(this->fd, 1);
    }
    void get() {
        eventfd_t n = 0;
        eventfd_read(this->fd, &n);
    }
    int raw() const { return this->fd; }
};

UserModeCache::UserModeCache(std::size_t cache_size, void* backend, std::size_t backend_size)
{
    this->page_size = getpagesize();
    assert(cache_size > 0);
    assert((cache_size % this->page_size) == 0);
    assert(backend_size >= cache_size);
    assert((backend_size % this->page_size) == 0);
    assert(backend != nullptr);

    this->shutdown_event = std::make_unique<EventFd>();

    this->uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    uffdio_api uffdio_api = {
        .api = UFFD_API,
        .features = 0,
        .ioctls = 0,
    };
    if( ioctl(this->uffd, UFFDIO_API, &uffdio_api) < 0 ) {
        close(this->uffd);
        this->uffd = -1;
        return;
    }
    this->frontend = (void*)mmap(nullptr, backend_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if( this->frontend == MAP_FAILED ) {
        close(this->uffd);
        this->uffd = -1;
        this->frontend = nullptr;
        return;
    }
    uffdio_register uffdio_register = {
        .range = {
            .start = (unsigned long)this->frontend,
            .len = backend_size,
        },
        .mode = UFFDIO_REGISTER_MODE_MISSING,
    };
    if( ioctl(this->uffd, UFFDIO_REGISTER, &uffdio_register) == -1 ) {
        close(this->uffd);
        this->uffd = -1;
        munmap(this->frontend, backend_size);
        return;
    }
    this->cache_size = cache_size;
    auto number_of_lines = cache_size / page_size;
    this->tag_shift = bits(cache_size) - 1;
    this->tags.resize(number_of_lines);
    std::fill(this->tags.begin(), this->tags.end(), 0);

    this->backend = backend;
    this->backend_size = backend_size;

    this->handler_thread = std::make_unique<std::thread>([this](){this->fault_handler();});
}

UserModeCache::~UserModeCache()
{
    if( this->handler_thread && this->handler_thread->joinable() ) {
        this->shutdown_event->put();
        this->handler_thread->join();
    }
    if( this->uffd >= 0 ) {
        close(this->uffd);
        this->uffd = -1;
    }
}

void UserModeCache::fault_handler()
{
    void* page_buffer = nullptr;
    posix_memalign(&page_buffer, this->page_size, this->page_size);

    const auto page_align_mask = ~(this->page_size-1);
    const auto page_shift = bits(this->page_size) - 1;
    const auto index_mask = (this->cache_size - 1) >> page_shift;
    const auto frontend_ptr = reinterpret_cast<std::uintptr_t>(this->frontend);
    const auto backend_ptr = reinterpret_cast<std::uint8_t*>(this->backend);

    printf("page_align_mask: %llx\n", page_align_mask);
    printf("page_shift:      %d\n", page_shift);
    printf("index_mask:      %llx\n", index_mask);
    for(;;) {
        pollfd pollfd[] = {
            {
                .fd = uffd,
                .events = POLLIN,
            },
            {
                .fd = this->shutdown_event->raw(),
                .events = POLLIN,
            },
        };
        //std::printf("WAIT\n");
        auto nready = poll(pollfd, 2, -1);
        if( nready == -1 ) {
            continue;
        }
        //std::printf("REVENTS: %x, %x\n", pollfd[0].revents, pollfd[1].revents);
        if( pollfd[1].revents != 0 ) {
            // Shutdown request
            break;
        }
        if( pollfd[0].revents & POLLERR ) {
            break;
        }

        uffd_msg msg;
        auto bytes_read = read(uffd, &msg, sizeof(msg));
        if( bytes_read != sizeof(msg) ) {
            continue;
        }
        if( msg.event != UFFD_EVENT_PAGEFAULT ) {
            continue;
        }

        auto target_address = msg.arg.pagefault.address & page_align_mask;
        auto offset = target_address - frontend_ptr;
        auto tag_index = (offset >> page_shift) & index_mask;
        auto tag = this->tags[tag_index];

        std::printf("PAGEFAULT: %llx, offset=%llx, tag_index=%llx\n", target_address, offset, tag_index);
        if( tag & TAG_USED ) {
            // Flush this cache line.
            auto address = (tag & TAG_MASK) << page_shift;
            std::printf("FLUSH: %p<-%llx\n", backend_ptr + address, frontend_ptr + address);
            std::memcpy(backend_ptr + address, reinterpret_cast<void*>(frontend_ptr + address), page_size);
            std::printf("REMAP: %llx\n", frontend_ptr + address);
            if( munmap(reinterpret_cast<void*>(frontend_ptr + address), page_size) == -1 ) {
                std::printf("munmap error - %d(%s)\n", errno, strerror(errno));
            }
            if( mmap(reinterpret_cast<void*>(frontend_ptr + address), page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED ) {
                std::printf("mmap error - %d(%s)\n", errno, strerror(errno));
            }
            uffdio_register uffdio_register = {
                .range = {
                    .start = frontend_ptr + address,
                    .len = page_size,
                },
                .mode = UFFDIO_REGISTER_MODE_MISSING,
            };
            if( ioctl(this->uffd, UFFDIO_REGISTER, &uffdio_register) == -1 ) {
                std::printf("UFFDIO_REGISTER error - %d(%s)\n", errno, strerror(errno));
            }
        }
        // Fill this cache line.
        std::printf("FILL: %p<-%p\n", target_address, backend_ptr + offset);
        std::memcpy(page_buffer, backend_ptr + offset, page_size);
        this->tags[tag_index] = TAG_USED | (offset >> page_shift);
        uffdio_copy uffdio_copy = {
            .dst = target_address,
            .src = reinterpret_cast<std::uintptr_t>(page_buffer),
            .len = page_size,
            .mode = 0,
            .copy = 0,
        };
        if( ioctl(this->uffd, UFFDIO_COPY, &uffdio_copy) == -1 ) {
            std::printf("UFFDIO_COPY error - %lld(%s)\n", -uffdio_copy.copy, strerror(-uffdio_copy.copy));
        }
    }
}
