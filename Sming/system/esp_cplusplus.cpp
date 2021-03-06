#include "../include/user_config.h"
#include "../include/esp_cplusplus.h"

////////////////////////////////////////////////////////////////////////

// Just do it! :)
void cpp_core_initialize()
{
    void (**p)(void);
    for (p = &__init_array_start; p != &__init_array_end; ++p)
            (*p)();
}

////////////////////////////////////////////////////////////////////////

void *operator new(size_t size)
{
  //debugf("new: %d", size);
  return os_zalloc(size);
}

void *operator new[](size_t size)
{
  //debugf("new[]: %d", size);
  return os_zalloc(size);
}

void operator delete(void * ptr)
{
	if (ptr != NULL)
		os_free(ptr);
}

void operator delete[](void * ptr)
{
	if (ptr != NULL)
		os_free(ptr);
}

extern "C" void abort()
{
	debugf("ABORT()");
	system_restart();
	while(true);
}

extern "C" void __cxa_pure_virtual(void)
{
	SYSTEM_ERROR("Bad pure_virtual_call");
	abort();
}

extern "C" void __cxa_deleted_virtual(void)
{
	SYSTEM_ERROR("Bad deleted_virtual_call");
	abort();
}


extern "C" void __cxa_guard_acquire(uint64_t* guard_object)
{
}
extern "C" void __cxa_guard_release(uint64_t* guard_object)
{
}
extern "C" void __cxa_guard_abort(uint64_t* guard_object)
{
}
