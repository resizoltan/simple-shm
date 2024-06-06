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

namespace simpleshm
{

namespace internal
{
template <typename T>
using OptionalSharedObject = std::optional<T>;

}

template <typename T>
class SharedObject
{
    static inline constexpr std::size_t SIZE = sizeof(internal::OptionalSharedObject<T>);
public:
    SharedObject(std::string_view id, bool owner)
    : owner_{owner},
      id_{id},
      shm_file_descriptor_{shm_open(id.data(), O_RDWR | (owner ? O_CREAT | O_EXCL : 0), S_IRUSR | S_IWUSR)},
      semaphore_{sem_open(id.data(), O_RDWR | (owner ? O_CREAT | O_EXCL : 0), S_IRUSR | S_IWUSR, 1)}
    {
        if(shm_file_descriptor_ < 0) {
            throw std::runtime_error(std::string{"Cannot create shared memory file descriptor with id "}
                .append(id).append(": ").append(strerror(errno)));
        }

        if(ftruncate(shm_file_descriptor_, SIZE) < 0) {
            if(owner) {
                shm_unlink(id.data());
            }
            throw std::runtime_error(std::string{"Cannot resize shared memory with id "}
                .append(id).append(": ").append(strerror(errno)));
        }

        shared_object_ = reinterpret_cast<internal::OptionalSharedObject<T>*>(
            mmap(nullptr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_file_descriptor_, 0));
        if(shared_object_ == MAP_FAILED) {
            if(owner) {
                shm_unlink(id.data());
            }
            throw std::runtime_error(std::string{"Cannot map shared memory with id "}
                .append(id).append(": ").append(strerror(errno)));
        }

        if(semaphore_ == SEM_FAILED) {
            if(owner) {
                shm_unlink(id.data());
            }
            throw std::runtime_error(std::string{"Cannot create semaphore with id "}
                .append(id).append(": ").append(strerror(errno)));
        }

        if(owner) {
            new (shared_object_) internal::OptionalSharedObject<T>();
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
        shared_object_->emplace(value);
    }

    T& get() {
        // throws std::bad_optional_access if value has never been set
        return shared_object_->value();
    }

    const T& get() const {
        // throws std::bad_optional_access if value has never been set
        return shared_object_->value();
    }

private:
    const bool owner_;
    const std::string id_;
    int shm_file_descriptor_;
    sem_t* semaphore_;
    internal::OptionalSharedObject<T>* shared_object_;
};


}