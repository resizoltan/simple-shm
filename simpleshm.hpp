#pragma once

#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>

#include <string_view>
#include <stdexcept>
#include <type_traits>
#include <optional>
#include <mutex>

namespace simpleshm
{

namespace internal
{
template <typename T>
struct SharedObject
{
    T data;
    std::recursive_mutex mutex;
};

}

template <typename T>
class SharedObject
{
    static inline constexpr std::size_t SIZE = sizeof(internal::SharedObject<T>);
public:
    template <typename ... Arg>
    SharedObject(std::string_view id, Arg&&... arg)
    : id_{id}
    {
        static_assert(sizeof...(Arg) == 0 || std::is_constructible_v<T, Arg&&...>);
        owner_ = openAsOwner(std::forward<Arg&&>(arg)...);
        if(!owner_) {
            openAsNonOwner();
        } 
    }

    ~SharedObject() {
        munmap(shared_object_, SIZE);
        close(shm_file_descriptor_);
        if(owner_) {
            // we acquire the semaphore so we know that noone is currently initializing
            sem_wait(semaphore_);
            shm_unlink(id_.data());
            sem_unlink(id_.data());
        }
    }

    void set(const T& value) {
        auto lock = std::lock_guard{shared_object_->mutex};
        shared_object_->data = value;
    }

    T get() const {
        auto lock = std::lock_guard{shared_object_->mutex};
        return shared_object_->data;
    }

    std::recursive_mutex& mutex() {
        return shared_object_->mutex;
    }

private:
    bool owner_;
    std::string id_;
    int shm_file_descriptor_;
    sem_t* semaphore_;
    internal::SharedObject<T>* shared_object_;

    template <typename ... Arg>
    bool openAsOwner(Arg&&... arg) {
        if(!std::is_constructible_v<T, Arg&&...>) {
            return false;
        }

        semaphore_ = sem_open(id_.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
        if(semaphore_ == SEM_FAILED) {
            return false;
        }

         // try creating shared memory segment
        shm_file_descriptor_ = shm_open(id_.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if(shm_file_descriptor_ < 0) { // we could not create the memory segment
            sem_unlink(id_.c_str());
            sem_post(semaphore_);
            throwError("Cannot create shared memory");
        }

        if(ftruncate(shm_file_descriptor_, SIZE) < 0) {
            shm_unlink(id_.c_str());
            sem_unlink(id_.c_str());
            sem_post(semaphore_);
            throwError("Cannot resize shared memory");
        }

        shared_object_ = reinterpret_cast<internal::SharedObject<T>*>(
            mmap(nullptr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_file_descriptor_, 0));
        if(shared_object_ == MAP_FAILED) {
            shm_unlink(id_.c_str());
            sem_unlink(id_.c_str());
            sem_post(semaphore_);
            throwError("Cannot map shared memory");
        }

        new (shared_object_) internal::SharedObject<T>({{std::forward<Arg&&>(arg)...},{}});

        // this is potentially dangerous, but it seems to work:
        // std::mutex cannot be used between processes by default
        // so we re-init the underlying posix mutex with the right attributes
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_destroy(shared_object_->mutex.native_handle());
        pthread_mutex_init(shared_object_->mutex.native_handle(), &attr);

        sem_post(semaphore_);
        return true;
    }

    void openAsNonOwner() {
        semaphore_ = sem_open(id_.c_str(), O_RDWR);
        if(semaphore_ == SEM_FAILED) {
            throwError("Cannot create semaphore");
        }

        // we wait max 10 ms for the owner to finish initialization
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 10'000'000;
        if(sem_timedwait(semaphore_, &ts)) {
            throwError("Could not acquire semaphore");
        }
        // we block the semaphore so the owner cannot destruct while we are opening
        shm_file_descriptor_ = shm_open(id_.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
        sem_post(semaphore_);

        if(shm_file_descriptor_ < 0) { // we could not open the memory segment
            throwError("Cannot open shared memory");
        }

        shared_object_ = reinterpret_cast<internal::SharedObject<T>*>(
            mmap(nullptr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_file_descriptor_, 0));
        if(shared_object_ == MAP_FAILED) {
            throwError("Cannot map shared memory");
        }
    }

    void throwError(std::string_view error) {
        throw std::runtime_error(std::string{error}.append(" with id {")
            .append(id_).append("}: ").append(strerror(errno)));
    }
};


}