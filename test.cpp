#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <thread>

#include "simpleshm.hpp"

using namespace simpleshm;

TEST_CASE("Shared Objects can be created and destroyed") {
    auto shared_bool = std::make_unique<SharedObject<bool>>("test_simpleshm_bool");
    CHECK_THROWS_AS (shared_bool->get(), std::bad_optional_access);
    shared_bool->set(true);
    REQUIRE (shared_bool->get() == true);
    shared_bool->set(false);
    REQUIRE (shared_bool->get() == false);
    errno = 0;
    shared_bool.reset();
    REQUIRE (errno == 0);
}

TEST_CASE("Shared Objects can be accessed from a single thread") {
    auto shared_bool1 = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_single_thread");
    auto shared_bool2 = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_single_thread");
    CHECK_THROWS_AS (shared_bool2->get(), std::bad_optional_access);
    shared_bool1->set(true);
    auto shared_bool3 = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_single_thread");
    REQUIRE (shared_bool1->get() == true);
    REQUIRE (shared_bool2->get() == true);
    REQUIRE (shared_bool3->get() == true);
    shared_bool2->set(false);
    REQUIRE (shared_bool1->get() == false);
    REQUIRE (shared_bool2->get() == false);
    REQUIRE (shared_bool3->get() == false);
    errno = 0;
    shared_bool1.reset();
    REQUIRE (errno == 0);
    errno = 0;
    shared_bool2.reset();
    REQUIRE (errno == 0);
}

TEST_CASE("Shared Objects can be accessed from multiple threads") {
    auto shared_bool1 = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_multi_thread");
    shared_bool1->set(true);
    bool result = false;
    auto t = std::thread{[&](){
        auto shared_bool2 = SharedObject<bool>("test_simpleshm_bool_multi_thread");
        result = shared_bool2.get();
        shared_bool2.set(false);
    }};
    t.join();
    REQUIRE (result == true);
    REQUIRE (errno == 0);
    REQUIRE (shared_bool1->get() == false);
    shared_bool1.reset();
    REQUIRE (errno == 0);
}

TEST_CASE("Shared Objects can be accessed from multiple processes") {
    std::unique_ptr<SharedObject<bool>> shared_bool;

    pid_t pid = fork();
    switch (pid)
    {
    case -1:
        perror("fork");
        exit(EXIT_FAILURE);
    case 0:
        errno = 0;
        shared_bool = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_multi_process");
        shared_bool->set(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE (shared_bool->get() == false);
        errno = 0;
        shared_bool.reset();
        REQUIRE (errno == 0);
        exit(EXIT_SUCCESS);
    default:
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        shared_bool = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_multi_process");
        REQUIRE (shared_bool->get() == true);
        shared_bool->set(false);
        errno = 0;
        shared_bool.reset();
        REQUIRE (errno == 0);
        break;
    }
}

TEST_CASE("Shared Objects are thread safe in a single process") {
    SharedObject<int> shared_int{"test_simpleshm_thread_safety_single_process"};
    shared_int.set(0);
    const int n = 1'000'000;
    auto fun = [&](){
        SharedObject<int> shared_int{"test_simpleshm_thread_safety_single_process"};
        for(int i = 0; i < n; i++) {
            std::lock_guard<std::mutex> lock{shared_int.mutex()};
            shared_int.set(shared_int.get() + 1);
        }
    };
    std::thread t1{fun};
    std::thread t2{fun};
    t1.join();
    t2.join();
    REQUIRE ( shared_int.get() == 2*n);
}