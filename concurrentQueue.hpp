#ifndef _CONCURRENT_QUEUE_
#define _CONCURRENT_QUEUE_

// TODO: THIS DOESN'T WORK, I GAVE UP. MAYBE IT IS POSSIBLE TO FIX...
/// I present 3 flavors of a thread-safe, dynamic, non-linearizable, multiple-producer-multiple-consumer (MPMC) FIFO linked-list.
/// These differ from other implemantations in that memory is unbounded AND some flavors provide memory reclamation.
/// This has the advantage of usability and unbound memory, at the cost of variable heap usage and performance.
///
/// Set this value to determine the method of synchronization and memory reclamation.
/// 0: synchronize using a mutex, delete as soon as a node is removed. Good for SPSC. Has memory reclamation. This is the KISS approach.
/// 1: synchronize lock-free using Harris' algorithm + reference counting. Good for SPMC, MPSC, and MPMC. Has memory reclamation which incurs performance cost.
/// 2: synchronize lock-free using Harris' algorithm. Good for SPMC, MPSC, and MPMC. Does not have memory reclamation.
#define SYNC_RECL_METHOD 2

#include <atomic>
#include <exception>
#include <utility>
#include <memory>
#include <cassert>
#include <condition_variable>

#if SYNC_RECL_METHOD == 0
   /// This is the naive approach, but it may be the best when little contention is expected.
   #define optionalSync() std::unique_lock<std::mutex> scopedLock( ((concurrentQueue *)this)->_mutex )
   #define defaultControlBlock() { {0} }
   constexpr int numRefCountBits = 0;
#elif SYNC_RECL_METHOD == 1
   /// My ref-counted variation on a linked-list proposedTim Harris (see the default case for more details regarding his non-refcounted approach).
   /// I utilized a single 64-bit atomic value that can be easily CAS'd for thread-safety, where:
   /// - bit 0 is the "marker" for logically-deleted nodes
   /// - bits 1-47 are the memory address. Allocations are aligned (so lower bits are not used) and assumed to use the suprisingly-typical 48-bit virtual space
   /// - bits 48-63 are the reference count. This allows for a maximum reference count of 32,768 (16-bits)
   /// A future implementation may consider the simpler std::atomic<std::shared_ptr>> type; however, as of late 2024 clang still does not support this,
   /// and GCC and MSVC use locks internally for their implementation.
   #define optionalSync()
   #define node_pack_next(ptr_as_uint) (ptr_as_uint >> 1)
   #define node_unpack_next(ptr_as_uint) (ptr_as_uint << 1)
   #define CAS(n, old, new) n.compare_exchange_weak( old, new )
   #define defaultControlBlock() { {.marker = 0, .nextPtr = 0, .refCount = 1} }
   constexpr int numRefCountBits = 16;
#else
   /// This is the vanilla implementation of Tim Harris' proposal: https://timharris.uk/papers/2001-disc.pdf
   /// This has no memory reclamation and as such may not be the most practical approach.
   /// It should be faster than the refererence counted version.
   #define optionalSync()
   #define node_pack_next(ptr_as_uint) (ptr_as_uint >> 1)
   #define node_unpack_next(ptr_as_uint) (ptr_as_uint << 1)
   #define CAS(n, old, new) n.compare_exchange_weak( old, new )
   #define defaultControlBlock() { {0} }
   constexpr int numRefCountBits = 0;
#endif
#define node_pack_next(ptr_as_uint) (ptr_as_uint >> 1) /// Ensure the next pointer consumes one-less bit, leaving room for the marker
#define node_unpack_next(ptr_as_uint) (ptr_as_uint << 1)
constexpr int numMarkerBits = 1; /// Keep the marker for all implementations, though some may not use it. No harm, no foul.
constexpr int numPtrBits = (sizeof(uintptr_t) * 8) - numRefCountBits - numMarkerBits;

template< typename ELEMENT_T >
class concurrentQueue
{
public:
   class node
   {
      friend class concurrentQueue;

      /// Control block for a node, represented as an unsigned long value holding bitfield values for:
      ///  - marker
      ///  - pointer
      ///  - refCounter
      /// This is packed into a single unsigned long so that we can perform a single compare-and-swap (CAS) operation on everything at once.
      /// Unlike Harris' implementation, I have a clear separation between the next pointer and the marker bit, allowing me to
      /// check the marker on the node of interest instead of decomposing the "next" pointer. Keep an eye out for this subtle difference in the implementation.
      union controlBlock
      {
         uintptr_t everything;
         struct
         {
            unsigned int marker: 1;
            uintptr_t nextPtr: numPtrBits;
#if SYNC_RECL_METHOD == 1
            int refCount: numRefCountBits;
#endif
         };
      };

   public:
      ELEMENT_T value;
      node() {}
      inline node(ELEMENT_T && v ) noexcept : value( v ) {}
      inline controlBlock load() const { return _controlBlock.load(); }
      static inline node * allocAligned( ELEMENT_T && v ) { return new( std::align_val_t(8)) node( std::forward<ELEMENT_T>(v) ); }

   private:
      std::atomic<controlBlock> _controlBlock = defaultControlBlock();
      static_assert( sizeof(_controlBlock) == sizeof(uintptr_t), "controlBLock is not sizeof(uintptr_t)" );

      node * next() const { return reinterpret_cast<node *>( node_unpack_next(_controlBlock.load().nextPtr) ); }
      void next( node * next )
      {
         auto ctrlBlock = _controlBlock.load();
         ctrlBlock.nextPtr = node_pack_next( reinterpret_cast<uintptr_t>(next) );
         _controlBlock.store(ctrlBlock);
      }
      bool logicallyRemoved() const { return _controlBlock.load().marker == 1; }
      
#if SYNC_RECL_METHOD == 1
      static inline void addRef( node * n ) noexcept
      {
         auto oldBlock = n->_controlBlock.load();
         do
         {
            auto newBlock = oldBlock;
            newBlock.refCount += 1;
            if ( CAS(n->_controlBlock, oldBlock, newBlock) )
               break;
         } while ( true );
      }
      static inline void decRef( node * n, bool test_canDelete = true ) noexcept
      {
         int rc = 0;
         auto oldBlock = n->_controlBlock.load();
         do
         {
            auto newBlock = oldBlock;
            newBlock.refCount -= 1;
            rc = newBlock.refCount;
            assert( rc >= 0 );
            if ( CAS(n->_controlBlock, oldBlock, newBlock) ) // TODO: move refCount to the lower bits and use a fetch-and-add instead, should be faster
               break;
         } while ( true );

         if ( rc == 0 ) // Notice not less than zero; exactly zero in case we get racy or overzealous
         {
            //delete n; // TODO: Do not enable this until _size is managed correctly
         }
      }
#endif
   };

#if SYNC_RECL_METHOD == 1
   #define node_raw(n) n._n
   struct sharedNode
   {
      node * _n;
      inline sharedNode() noexcept : _n(0) {}
      inline explicit sharedNode( node * n ) noexcept : _n(n) { if (_n) node::addRef(_n); }
      inline sharedNode( const sharedNode & rhs ) noexcept : _n(rhs._n) { if (_n) node::addRef(_n); }
      inline ~sharedNode() { if (_n) node::decRef(_n); }
      inline sharedNode & operator=( const sharedNode & rhs ) noexcept
      {
         sharedNode(rhs).swap(*this);
         return *this;
      }
      inline sharedNode & operator=( node * rhs ) noexcept
      {
         sharedNode(rhs).swap(*this);
         return *this;
      }
      inline bool operator==( const sharedNode & rhs ) const noexcept { return _n == rhs._n; }
      inline bool operator==( const node * rhs ) const noexcept { return _n == rhs; }
      inline node * operator->() noexcept { return _n; }
      inline void swap( sharedNode & rhs ) noexcept
      {
         std::swap( _n, rhs._n );
      }
   };
#else
   using sharedNode = node *;
   #define node_raw(n) n
#endif

   struct searchResult
   {
      sharedNode left;
      sharedNode right;
   };
   
   concurrentQueue()
   {
      _head = node::allocAligned( {} );
      _tail = nullptr;
      _head->next( _tail );
   }
   ~concurrentQueue()
   {
      clear();
      delete _head;
   }

   /// NOT THREAD SAFE!
   /// This function is only for use when no concurrent access exists, otherwise it cannot accurately succeed.
   inline size_t size() const noexcept
   {
      //return size_t(_size.load());
      
      // TODO: TESTING. This is where I left off - trying to track removed nodes.
      // TODO: Once addressed, this block can be removed and the above uncommented.
      size_t returnValue = 0;
      sharedNode n = _head->next();
      while ( n != nullptr )
      {
         assert( !n->logicallyRemoved() );
         ++returnValue;
         n = n->next();
      }
      assert( returnValue == _size );
      return returnValue;
   }

   /// NOT THREAD SAFE!
   /// This function is only for use when no concurrent access exists, otherwise it cannot accurately succeed.
   inline bool empty() const noexcept
   {
      return _head->next() == nullptr;
   }

   /// NOT THREAD SAFE!
   /// This function is only for use when no concurrent access exists, otherwise it cannot accurately succeed.
   /// This is a full iteration of the list: O(n).
   inline void clear() noexcept
   {
      auto n = _head->next();
      while ( n != nullptr )
      {
         auto next = n->next();
         onNodeRemoved(n);
         n = next;
      }
      _head->next( nullptr );
      _size = 0;
      _cv.notify_one();
   }
   
   inline std::optional<std::reference_wrapper<ELEMENT_T>> front() const noexcept
   {
      optionalSync();
      auto leftAndRight = const_cast<concurrentQueue *>(this)->search( _head, true );
      return leftAndRight.right != nullptr ? std::make_optional(std::ref(leftAndRight.right->value)) : std::nullopt;
   }
   void push_back( ELEMENT_T && element )
   {
      // This allocation guarantees that the LSB will always be zero
      auto newNode = node::allocAligned( std::forward<ELEMENT_T>(element) );
      assert( !newNode->logicallyRemoved() );

#if SYNC_RECL_METHOD == 0
      optionalSync();
      auto current = _head;
      auto next = current->next();
      while ( next != nullptr )
      {
         current = next;
         next = current->next();
      }
      current->next( newNode );
#else
      ++_concurrencyCount;
      do {
         auto leftAndRight = search( _head, false ); // TODO: we can improve this, starting somewhere other than head

         //T1 doesn't apply to this specialization
         newNode->next( node_raw(leftAndRight.right) );

         auto oldControlBlock = leftAndRight.left->_controlBlock.load();
         oldControlBlock.nextPtr = node_pack_next( reinterpret_cast<uintptr_t>(node_raw(leftAndRight.right)) );
         auto newControlBlock = oldControlBlock;
         newControlBlock.nextPtr = node_pack_next( reinterpret_cast<uintptr_t>(newNode) );
         if ( CAS(leftAndRight.left->_controlBlock, oldControlBlock, newControlBlock) ) //C2
            break;
      } while (true); //B3
      --_concurrencyCount;
#endif
      _size.fetch_add( 1, std::memory_order_relaxed );
      _cv.notify_one();
   }

   bool pop_front() noexcept
   {
#if SYNC_RECL_METHOD == 0
      optionalSync();
      searchResult leftAndRight{ _head, _head->next() };
      if ( leftAndRight.right == nullptr )
         return false;
      leftAndRight.left->next( leftAndRight.right->next() );
      onNodeRemoved( node_raw(leftAndRight.right) );
#else
      searchResult leftAndRight;
      sharedNode rightNext;
      ++_concurrencyCount;
      do
      {
         leftAndRight = search( _head, true );
         if ( leftAndRight.right == nullptr ) //T1
         {
            --_concurrencyCount; // TODO: TESTING. Remove these
            return false;
         }

         // Ensure the right node is marked.
         rightNext = leftAndRight.right->next();
         if ( !leftAndRight.right->logicallyRemoved() )
         {
            auto oldControlBlock = leftAndRight.right->_controlBlock.load();
            auto newControlBlock = oldControlBlock;
            newControlBlock.marker = 1;
            if ( CAS(leftAndRight.right->_controlBlock, oldControlBlock, newControlBlock) ) //C3
               break;
         }
      } while ( true ); //B4

      auto oldControlBlock = leftAndRight.left->_controlBlock.load();
      oldControlBlock.nextPtr = node_pack_next( reinterpret_cast<uintptr_t>(node_raw(leftAndRight.right)) );
      auto newControlBlock = oldControlBlock;
      newControlBlock.nextPtr = node_pack_next( reinterpret_cast<uintptr_t>(node_raw(rightNext)) );
      bool removalSucceeded = CAS(leftAndRight.left->_controlBlock, oldControlBlock, newControlBlock); //C4
      if ( !removalSucceeded )
         leftAndRight = search( leftAndRight.left, false ); // Concurrency collision, let search() try and remove marked nodes
      else
         onNodeRemoved( node_raw(leftAndRight.right) );
      --_concurrencyCount;
#endif
      return leftAndRight.right != nullptr;
   }
   
   /// Blocks until at least one element has been added.
   /// Returns `true` if a new element has been added, or `false` if this returns for any other reason.
   /// This does require mutex synchronization with push\_back(), which technically breaks the lock-free nature; however:
   /// - the mutex will have minimal contention, much less than if it was used for all operations
   /// - the mutex only blocks push\_back()
   /// - if you truly need lock free then simply don't call this function. A mutex that never sees contention should have time complexity O(1)
   template< typename Rep, typename Period >
   std::cv_status wait_for( const std::chrono::duration<Rep, Period>& rel_time )
   {
      if ( _head->next() != nullptr )
         return std::cv_status::no_timeout;
      std::unique_lock<std::mutex> autoLock( _cvMutex );
      return _cv.wait_for( autoLock, rel_time );
   }

private:
#if SYNC_RECL_METHOD == 0
   std::mutex _mutex;
#else
   std::atomic_int _concurrencyCount = 0;
#endif
   std::atomic_size_t _size = 0;
   node * _head;
   node * _tail;
   std::condition_variable _cv;
   std::mutex _cvMutex;

   // Accepts a boolean indicating which end we care about.
   searchResult search( node * startNode, bool findFront ) noexcept
   {
#if SYNC_RECL_METHOD == 0
      node * current = startNode;
      node * next = current->next();
      while ( !findFront )
      {
         if ( next == nullptr )
            break;
         current = next;
         next = current->next();
      }
      return { current, next };
#else
      sharedNode left;
      sharedNode right;
      sharedNode leftNext;

   search_again:
      do
      {
         sharedNode t( _head );
         sharedNode t_next( _head->next() );

         // 1: Find left and right nodes
         do
         {
            if ( !t->logicallyRemoved() )
            {
               left = t;
               leftNext = t_next;
            }
            t = t_next;
            if ( t == nullptr )
               break;
            t_next = t->next();
         } while ( t->logicallyRemoved() || !findFront ); //B1
         right = t;

         // 2: Check nodes are adjacent
         if ( leftNext == right )
         {
            if ( (right != nullptr) && right->logicallyRemoved() )
               goto search_again; //G1
            else
               return { left, right }; //R1
         }

         // 3: Remove one or more marked nodes. Getting here means that left and right are NOT adjacent - we have a gap.
         auto oldControlBlock = left->_controlBlock.load();
         oldControlBlock.nextPtr = node_pack_next( reinterpret_cast<uintptr_t>(node_raw(leftNext)) );
         auto newControlBlock = oldControlBlock;
         newControlBlock.nextPtr = node_pack_next( reinterpret_cast<uintptr_t>(node_raw(right)) );
         if ( CAS(left->_controlBlock, oldControlBlock, newControlBlock) ) //C1
         {
            // Handle removal of now-orphaned nodes in this gap, but only if logically removed
            auto n = leftNext;
            do
            {
               auto nextNodeToRemove = n->next();
               if ( n->logicallyRemoved() )
                  onNodeRemoved(n);
               n = nextNodeToRemove;
            } while ( n != right );

            if ( right != nullptr && right->logicallyRemoved() )
               goto search_again; //G2
            else
               return { left, right };
         }
      } while( true ); //B2
#endif
   }
   inline void onNodeRemoved( node * n ) noexcept
   {
#if SYNC_RECL_METHOD == 0
         delete n;
#elif SYNC_RECL_METHOD == 1
         node::decRef( n );
#endif
         _size.fetch_sub( 1, std::memory_order_relaxed );
   }
};
#endif
