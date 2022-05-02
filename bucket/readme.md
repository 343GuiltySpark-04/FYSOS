This is an attempt to make a simple and easy to use heap allocator.

After searching for an easy to use, mostly drop-in code, heap allocator, most of them ranged from very easy to quite difficult to understand what was actually being done under the hood.  One was quite simple, but allocated a minimum of a 4096-byte page for every memory request.  Another was quite efficient for small allocation amounts, but was extremely difficult to understand exactly what was happening under the hood.

Therefore, I created this one.  It is very simple, and more importantly, has documentation that isn't just source code comments.  The included PDF is part of section 20 from my OS Specification.  It explains exactly what is going on under the hood.

The two source files needed are malloc.cpp and malloc.h.  The bucket.cpp file contains the skeleton of the four "out-side" functions that are needed.

As long as you have a mmap() (mostly)equivilant call already implemented, and a spinlock style locking mechanism, this code is (nearly) drop-in for a simple but useful malloc() replacement for your beginning kernel building.

The mmap() call should allocate contiguous pages of memory from the system.  This memory can be, and usually is virtual memory.  However, if your code doesn't support virtual memory, simply return a string of contiguous physical memory.  It works the same.

The mmap_free() function simply calls the system to free this memory.

To use, call the 

   kernel_heap = malloc_init( some size in bytes );

function to initialize the Heap Allocator and start with one Bucket with one free Pebble.

No code is ever just drop-in so you may have to modify it a little to get it to work for you.

This code is just an example.  It *does not* include the alignment, physical contiguous, or HARWARE** flag functionality.  This is left up to the reader.

If you have any questions or comments, please let me know.  fys [at] fysnet [dot] net.

Thanks,
Ben