project(simpleshm)
cmake_minimum_required(VERSION 3.16)
set (CMAKE_CXX_STANDARD 17)

Include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.4.0 # or a later release
)
FetchContent_MakeAvailable(Catch2)


set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_executable(tests test.cpp)
target_link_libraries(tests PRIVATE rt Catch2::Catch2WithMain Threads::Threads)