#include <catch2/catch_all.hpp>

#include <thread>
#include "concurrentQueue.hpp"

using namespace std::chrono_literals;

// This test code was loosely referenced from:
//   https://github.com/bhhbazinga/LockFreeLinkedList/blob/master/test.cc
const int kMaxThreads = std::thread::hardware_concurrency();

constexpr int NUM_ELEMENTS_PER_THREAD = 1000;
std::atomic<bool> start = false;

TEST_CASE( "concurrentQueue" )
{
   SECTION( "sharedNode")
   {
      auto n = concurrentQueue<std::string>::node::allocAligned( "hi there" );
      CHECK( n != nullptr );

      // Verify alignment
      auto value = reinterpret_cast<uintptr_t>(n);
      auto alignedValue = (value >> 3) << 3;
      CHECK( value == alignedValue );

      // Verify ref counting
#if SYNC_RECL_METHOD == 1 // Pulled from concurrentQueue.hpp
      CHECK( n->load().refCount == 1 );
      {
         concurrentQueue<std::string>::sharedNode sn1( n );
         CHECK( n->load().refCount == 2 );
         {
            auto sn2 = sn1;
            CHECK( n->load().refCount == 3 );
         }
         CHECK( n->load().refCount == 2 );
         auto sn2( sn1 );
         CHECK( n->load().refCount == 3 );
         concurrentQueue<std::string>::sharedNode sn3( n );
         CHECK( n->load().refCount == 4 );
      }
      CHECK( n->load().refCount == 1 );
#endif
      
      // Test concurrency
      int numIterations = 10000;
      int divide = kMaxThreads / 2;
      std::vector<std::thread> threads;
      auto concurrentTest = [] ( auto sharedNode, auto numIterations ) {
         while (!start)
         {
            std::this_thread::yield();
         }

         for ( int i = 0; i < numIterations; ++i )
         {
            auto sn( sharedNode );
            concurrentQueue<std::string>::sharedNode sn2 = sharedNode;
            sn2 = sn;
            std::swap( sn2, sn );
         }
      };
      for (int i = 0; i < divide; ++i)
      {
         threads.push_back( std::thread(concurrentTest, concurrentQueue<std::string>::sharedNode(n), numIterations) );
      }
      start = true;
      for (int i = 0; i < divide; ++i)
      {
         threads[i].join();
      }
      start = false;
#if SYNC_RECL_METHOD == 1
      CHECK( n->load().refCount == 1 );
#endif
   }
   SECTION( "single_thread" )
   {
      concurrentQueue<int> list;
      for ( auto i = 0; i < NUM_ELEMENTS_PER_THREAD; ++i )
         list.push_back(int(i));
      CHECK( list.size() == NUM_ELEMENTS_PER_THREAD );

      for ( auto i = 0; i < NUM_ELEMENTS_PER_THREAD; ++i )
      {
         CHECK( list.front()->get() == i );
         list.pop_front();
      }
      CHECK( list.size() == 0 );
   }
   // TODO: Disabled until concurrentQueue is fixed
//   SECTION( "SPSC" )
//   {
//      static std::atomic<int> cnt = 0;
//      concurrentQueue<int> list;
//      
//      auto onInsert = []( concurrentQueue<int> * list ) {
//         while (!start) { std::this_thread::yield(); }
//
//         for ( int i = 0; i < NUM_ELEMENTS_PER_THREAD; ++i )
//         {
//            list->push_back( int(i) );
//            ++cnt;
//         }
//      };
//
//      auto onDelete = []( concurrentQueue<int> * list ) {
//         while (!start) { std::this_thread::yield(); }
//
//         for (int i = 0; i < NUM_ELEMENTS_PER_THREAD; ++i)
//         {
//            if ( list->pop_front() )
//               --cnt;
//         }
//      };
//
//      auto producer = std::thread( onInsert, &list );
//      auto consumer = std::thread( onDelete, &list );
//
//      cnt = 0;
//      start = true;
//
//      producer.join();
//      consumer.join();
//
//      REQUIRE( cnt == (int)list.size() );
//      for ( auto i = 0; i < cnt; ++i )
//      {
//         INFO( cnt );
//         CHECK( list.pop_front() );
//         CHECK( list.size() == cnt - i - 1 );
//      }
//      CHECK( !list.pop_front() );
//      CHECK( list.size() == 0 );
//   }
//   SECTION( "wait_for" )
//   {
//      concurrentQueue<int> list;
//
//      auto onInsert = []( concurrentQueue<int> * list ) {
//         while (!start) { std::this_thread::yield(); }
//
//         int n = NUM_ELEMENTS_PER_THREAD;
//         for (int i = 0; i < n; ++i)
//         {
//            list->push_back( int(i) );
//         }
//      };
//
//      auto producer = std::thread( onInsert, &list );
//      start = true;
//      
//      int count = 0;
//      while ( list.wait_for(100ms) != std::cv_status::timeout )
//      {
//         while ( list.pop_front() )
//            ++count;
//      }
//      producer.join();
//
//      CHECK( count == NUM_ELEMENTS_PER_THREAD );
//   }
//   SECTION( "MPMC" )
//   {
//      static std::atomic<int> cnt = 0;
//      concurrentQueue<std::string> list;
//      int divide = kMaxThreads / 2;
//      
//      auto onInsert = []( concurrentQueue<std::string> * list, int divide, int id ) {
//         while (!start) { std::this_thread::yield(); }
//
//         int n = NUM_ELEMENTS_PER_THREAD / divide;
//         std::stringstream ss;
//         for (int i = 0; i < n; ++i)
//         {
//            ss << "t" << id << "_" <<i;
//            list->push_back( ss.str() );
//            ss.str("");
//            ++cnt;
//         }
//      };
//
//      auto onDelete = []( concurrentQueue<std::string> * list, int divide, int id ) {
//         while (!start) { std::this_thread::yield(); }
//
//         int n = NUM_ELEMENTS_PER_THREAD / divide;
//         for (int i = 0; i < n; ++i)
//         {
//      // TODO: TESTING
//      //std::cout << list.front().data() << std::endl;
//            if ( list->pop_front() )
//               --cnt;
//         }
//      };
//
//      std::vector<std::thread> insert_threads;
//      for (int i = 0; i < divide; ++i)
//      {
//         insert_threads.push_back(std::thread(onInsert, &list, divide, i));
//      }
//
//      std::vector<std::thread> delete_threads;
//      for (int i = 0; i < divide; ++i)
//      {
//         delete_threads.push_back(std::thread(onDelete, &list, divide, i));
//      }
//
//      cnt = 0;
//      start = true;
//      for (int i = 0; i < divide; ++i)
//      {
//         insert_threads[i].join();
//      }
//
//      for ( auto && thread : delete_threads )
//      {
//         thread.join();
//      }
//
//      REQUIRE( cnt == (int)list.size() );
//      for ( auto i = 0; i < cnt; ++i )
//      {
//         INFO( cnt );
//         CHECK( list.pop_front() );
//         CHECK( list.size() == cnt - i - 1 );
//      }
//      CHECK( !list.pop_front() );
//      CHECK( list.size() == 0 );
//   }
}
