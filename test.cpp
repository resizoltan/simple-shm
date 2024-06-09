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

TEST_CASE ("Access of shared objects is thread-safe") {
    SharedObject<int> shared_int{"simpleshm_pthread_process_shared"};
    shared_int.set(0);
    const int n = 1'000'000;
    auto fun = [](){
        SharedObject<int> shared_int2{"simpleshm_pthread_process_shared"};
        for(int i = 0; i < n; i++) {
            auto lock = std::lock_guard{shared_int2.mutex()};
            shared_int2.set(shared_int2.get() + 1);
        }
    };
    std::thread t1{fun};
    std::thread t2{fun};
    t1.join();
    t2.join();
    REQUIRE ( shared_int.get() == 2*n);
}
