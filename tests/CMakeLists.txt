include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(Tests
        FileCollectorTest.cpp
        IntervalSetTest.cpp
)
target_link_libraries(Tests PRIVATE FileCollector GTest::gtest_main)

if(MINGW)
    target_link_options(Tests PRIVATE -static)
endif()

include(GoogleTest)
gtest_discover_tests(Tests)