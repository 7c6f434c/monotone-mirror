/*************************************************
* Basic Allocators Source File                   *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/defalloc.h>
#include <botan/util.h>
#include <cstdlib>
#include <cstring>

namespace Botan {

namespace {

/*************************************************
* Perform Memory Allocation                      *
*************************************************/
void* do_malloc(u32bit n, bool do_lock)
   {
   void* ptr = std::malloc(n);

   if(!ptr)
      return 0;

   if(do_lock)
      lock_mem(ptr, n);

   std::memset(ptr, 0, n);
   return ptr;
   }

/*************************************************
* Perform Memory Deallocation                    *
*************************************************/
void do_free(void* ptr, u32bit n, bool do_lock)
   {
   if(!ptr)
      return;

   std::memset(ptr, 0, n);
   if(do_lock)
      unlock_mem(ptr, n);

   std::free(ptr);
   }

}

/*************************************************
* Malloc_Allocator's Allocation                  *
*************************************************/
void* Malloc_Allocator::alloc_block(u32bit n) const
   {
   return do_malloc(n, false);
   }

/*************************************************
* Malloc_Allocator's Deallocation                *
*************************************************/
void Malloc_Allocator::dealloc_block(void* ptr, u32bit n) const
   {
   do_free(ptr, n, false);
   }

/*************************************************
* Locking_Allocator's Allocation                 *
*************************************************/
void* Locking_Allocator::alloc_block(u32bit n) const
   {
   return do_malloc(n, true);
   }

/*************************************************
* Locking_Allocator's Deallocation               *
*************************************************/
void Locking_Allocator::dealloc_block(void* ptr, u32bit n) const
   {
   do_free(ptr, n, true);
   }

}
