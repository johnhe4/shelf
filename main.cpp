#include <catch2/catch_all.hpp>

int main( int argc, char* argv[] )
{
   Catch::Session session;
   auto ret = session.applyCommandLine( argc, argv );
   if ( ret )
   {
      return ret;
   }

   int result = session.run();
#if defined(WIN32) && defined(_DEBUG)
   std::cout << "Press Enter to exit..." << std::endl;
   std::cin.get();
#endif
   return result;
}
