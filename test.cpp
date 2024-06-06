#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <thread>

#include "simpleshm.hpp"

using namespace simpleshm;

struct NonDefaultConstructibleObject
{
    NonDefaultConstructibleObject(int) {};
};
//SharedObject<NonDefaultConstructibleObject> non{"test_simpleshm_non", true};

TEST_CASE("Shared Objects can be created and destroyed") {
    auto shared_bool = std::make_unique<SharedObject<bool>>("test_simpleshm_bool", true);
    CHECK_THROWS_AS (shared_bool->get(), std::bad_optional_access);
    shared_bool->set(true);
    REQUIRE (shared_bool->get() == true);
    shared_bool->set(false);
    REQUIRE (shared_bool->get() == false);
    CHECK_THROWS (std::make_unique<SharedObject<bool>>("test_simpleshm_bool", true));
    errno = 0;
    shared_bool.reset();
    REQUIRE (errno == 0);
    CHECK_THROWS (std::make_unique<SharedObject<bool>>("test_simpleshm_bool_no_owner", false));
}

TEST_CASE("Shared Objects can be accessed from a single thread") {
    auto shared_bool1 = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_single_thread", true);
    auto shared_bool2 = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_single_thread", false);
    CHECK_THROWS_AS (shared_bool2->get(), std::bad_optional_access);
    shared_bool1->set(true);
    auto shared_bool3 = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_single_thread", false);
    REQUIRE (shared_bool2->get() == true);
    shared_bool2->set(false);
    REQUIRE (shared_bool1->get() == false);
    errno = 0;
    shared_bool1.reset();
    REQUIRE (errno == 0);
    errno = 0;
    shared_bool2.reset();
    REQUIRE (errno == 0);
}

TEST_CASE("Shared Objects can be accessed from multiple threads") {
    auto shared_bool1 = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_multi_thread", true);
    shared_bool1->set(true);
    bool result = false;
    auto t = std::thread{[&](){
        auto shared_bool2 = SharedObject<bool>("test_simpleshm_bool_multi_thread", false);
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
    pid_t pid = fork();
    switch (pid)
    {
    case -1:
        perror("fork");
        exit(EXIT_FAILURE);
        break;
    case 0:
        
    default:
        break;
    }
    auto shared_bool1 = std::make_unique<SharedObject<bool>>("test_simpleshm_bool_multi_process", true);
    shared_bool1->set(true);
    bool result = false;
    auto t = std::thread{[&](){
        auto shared_bool2 = SharedObject<bool>("test_simpleshm_bool_multi_process", false);
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