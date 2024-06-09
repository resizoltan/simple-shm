#pragma once

#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <semaphore.h>

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
struct OptionalSharedObject
{
    std::optional<T> data;
    std::mutex mutex;
};


class SemGuard {
public:
    inline SemGuard(sem_t* semaphore)
    : semaphore_{semaphore}
    {
        sem_wait(semaphore_);
    };

    inline ~SemGuard() {
        sem_post(semaphore_);
    }
private:
    sem_t* semaphore_;
};

}

template <typename T>
class SharedObject
{
    static inline constexpr std::size_t SIZE = sizeof(internal::OptionalSharedObject<T>);
public:
    SharedObject(std::string_view id)
    : id_{id}
    {
        owner_ = openAsOwner();
        if(!owner_) {
            openAsNonOwner();
        } 
    }

    ~SharedObject() {
        munmap(shared_object_, SIZE);
        close(shm_file_descriptor_);
        if(owner_) {
            shm_unlink(id_.data());
            sem_unlink(id_.data());
        }
    }

    void set(const T& value) {
        shared_object_->data.emplace(value);
    }

    T get() const {
        // throws std::bad_optional_access if value has never been set
        return shared_object_->data.value();
    }

    std::mutex& mutex() {
        return shared_object_->mutex;
    }

private:
    bool owner_;
    std::string id_;
    int shm_file_descriptor_;
    sem_t* semaphore_;
    internal::OptionalSharedObject<T>* shared_object_;

    bool openAsOwner() {
        semaphore_ = sem_open(id_.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
        if(semaphore_ == SEM_FAILED) {
            return false;
        }

         // try creating shared memory segment
        shm_file_descriptor_ = shm_open(id_.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if(shm_file_descriptor_ < 0) { // we could not create the memory segment
            sem_unlink(id_.c_str());
            throwError("Cannot create shared memory");
        }

        if(ftruncate(shm_file_descriptor_, SIZE) < 0) {
            shm_unlink(id_.c_str());
            sem_unlink(id_.c_str());
            throwError("Cannot resize shared memory");
        }

        shared_object_ = reinterpret_cast<internal::OptionalSharedObject<T>*>(
            mmap(nullptr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_file_descriptor_, 0));
        if(shared_object_ == MAP_FAILED) {
            shm_unlink(id_.c_str());
            sem_unlink(id_.c_str());
            throwError("Cannot map shared memory");
        }

        new (shared_object_) internal::OptionalSharedObject<T>();

        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
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
        {
            internal::SemGuard{semaphore_};
            shm_file_descriptor_ = shm_open(id_.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
        }

        if(shm_file_descriptor_ < 0) { // we could not open the memory segment
            throwError("Cannot open shared memory");
        }

        shared_object_ = reinterpret_cast<internal::OptionalSharedObject<T>*>(
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