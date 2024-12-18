cmake_minimum_required(VERSION 3.20)
project( shelf )
set( CMAKE_CXX_STANDARD 17 )
find_package(Catch2)
add_executable( shelfTest
   main.cpp
   concurrentQueue.hpp
   concurrentQueueTest.cpp
)
target_link_libraries( shelfTest PRIVATE
   Catch2::Catch2
)
