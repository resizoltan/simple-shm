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
        // try creating shared memory segment
        shm_file_descriptor_ = shm_open(id.data(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if(shm_file_descriptor_ < 0) { // we could not create the memory segment
            if( errno == 17) { // File exists: we try opening the memory segment again, without creating
                errno = 0;
                // if the segment is deleted in the meantime, bad luck, but probably unintended behavior
                shm_file_descriptor_ = shm_open(id.data(), O_RDWR, S_IRUSR | S_IWUSR);
                if(shm_file_descriptor_ < 0) {
                    throwError("Cannot open shared memory");
                }
                owner_ = false; // we do not own this shared object
            }
            else {
                throwError("Cannot open shared memory");
            }
        }
        else {
            owner_ = true; // we created the segment, we are the owners of this shared object
        }

        if(ftruncate(shm_file_descriptor_, SIZE) < 0) {
            if(owner_) {
                shm_unlink(id.data());
            }
            throwError("Cannot resize shared memory");
        }

        shared_object_ = reinterpret_cast<internal::OptionalSharedObject<T>*>(
            mmap(nullptr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_file_descriptor_, 0));
        if(shared_object_ == MAP_FAILED) {
            if(owner_) {
                shm_unlink(id.data());
            }
            throwError("Cannot map shared memory");
        }

        semaphore_ = sem_open(id.data(), O_RDWR | (owner_ ? O_CREAT | O_EXCL : 0), S_IRUSR | S_IWUSR, 1);
        if(semaphore_ == SEM_FAILED) {
            if(owner_) {
                shm_unlink(id.data());
            }
            throwError("Cannot create semaphore");
        }

        if(owner_) {
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
        internal::SemGuard{semaphore_};
        shared_object_->emplace(value);
    }

    T get() const {
        internal::SemGuard{semaphore_};
        // throws std::bad_optional_access if value has never been set
        return shared_object_->value();
    }

private:
    bool owner_;
    std::string id_;
    int shm_file_descriptor_;
    sem_t* semaphore_;
    internal::OptionalSharedObject<T>* shared_object_;

    void throwError(std::string_view error) {
        throw std::runtime_error(std::string{error}.append(" with id {")
            .append(id_).append("}: ").append(strerror(errno)));
    }
};


}