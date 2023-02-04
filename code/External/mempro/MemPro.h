/*
Copyright 2019 PureDev Software Limited

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/


//------------------------------------------------------------------------
//
// MemPro.h
//
/*
	MemProLib is the library that allows the MemPro application to communicate
	with your application.

	===========================================================
                             SETUP
	===========================================================

	* include MemPro.cpp and MemPro.h in your project.

	* Link with Dbghelp.lib and Ws2_32.lib - these are needed for the callstack trace and the network connection

	* Connect to your app with the MemPro
*/
//------------------------------------------------------------------------
/*
	MemPro
	Version:	1.6.8.0
*/
//------------------------------------------------------------------------
#ifndef MEMPRO_MEMPRO_H_INCLUDED
#define MEMPRO_MEMPRO_H_INCLUDED

//------------------------------------------------------------------------
#ifndef MEMPRO_ENABLED
	#define MEMPRO_ENABLED 1				// **** enable/disable MemPro here! ****
#endif

//------------------------------------------------------------------------
#if defined(__UNREAL__) && MEMPRO_ENABLED && !defined(WITH_ENGINE)
	#undef MEMPRO_ENABLED
	#define MEMPRO_ENABLED 0
#endif

//------------------------------------------------------------------------
#if defined(__UNREAL__)
	#include "CoreTypes.h"
	#include "HAL/PlatformMisc.h"
#endif

//------------------------------------------------------------------------
// **** The Target Platform ****

// define ONE of these
#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__WIN32__) || defined(__WINDOWS__)
	#if defined(_XBOX_ONE) || (defined(__UNREAL__) && PLATFORM_XBOXONE)
		#define MEMPRO_PLATFORM_XBOXONE
	#elif defined(_XBOX)
		#define MEMPRO_PLATFORM_XBOX360
	#else
		#define MEMPRO_PLATFORM_WIN
	#endif
#elif defined(__APPLE__)
	#define MEMPRO_PLATFORM_APPLE
#elif defined(PS4) || (defined(__UNREAL__) && PLATFORM_PS4)
	#define MEMPRO_PLATFORM_PS4
#elif defined(PS5) || (defined(__UNREAL__) && PLATFORM_PS5)
	#define MEMPRO_PLATFORM_PS5
#else
	#define MEMPRO_PLATFORM_UNIX
#endif

//------------------------------------------------------------------------
// macros for tracking allocs that define to nothing if disabled
#if MEMPRO_ENABLED
	#ifndef WAIT_FOR_CONNECT
		#define WAIT_FOR_CONNECT false
	#endif
	#define MEMPRO_TRACK_ALLOC(p, size) MemPro::TrackAlloc(p, size, WAIT_FOR_CONNECT)
	#define MEMPRO_TRACK_FREE(p) MemPro::TrackFree(p, WAIT_FOR_CONNECT)
#else
	#define MEMPRO_TRACK_ALLOC(p, size) ((void)0)
	#define MEMPRO_TRACK_FREE(p) ((void)0)
#endif

//------------------------------------------------------------------------
#if MEMPRO_ENABLED

//------------------------------------------------------------------------
// Some platforms have problems initialising winsock from global constructors,
// to help get around this problem MemPro waits this amount of time before
// initialising. Allocs and freed that happen during this time are stored in
// a temporary buffer.
#define MEMPRO_INIT_DELAY 100

//------------------------------------------------------------------------
// MemPro waits this long before giving up on a connection after initialisation
#define MEMPRO_CONNECT_TIMEOUT 500

//------------------------------------------------------------------------
#include <stdlib.h>

//------------------------------------------------------------------------
//#define MEMPRO_WRITE_DUMP "d:\\temp\\allocs.mempro_dump"

//------------------------------------------------------------------------
// always default to use dump files for Unreal
#if defined(__UNREAL__) && !defined(MEMPRO_WRITE_DUMP)
	#define MEMPRO_WRITE_DUMP
#endif

//------------------------------------------------------------------------
#if defined(MEMPRO_WRITE_DUMP) && defined(DISALLOW_WRITE_DUMP) 
	#error
#endif

//------------------------------------------------------------------------
#define MEMPRO_ASSERT(b) if(!(b)) Platform::DebugBreak()

//------------------------------------------------------------------------
namespace MemPro
{
	//------------------------------------------------------------------------
	typedef long long int64;
	typedef unsigned long long uint64;

	//------------------------------------------------------------------------
	enum PageState
	{
		Invalid = -1,
		Free,
		Reserved,
		Committed
	};

	//------------------------------------------------------------------------
	enum PageType
	{
		page_Unknown = -1,
		page_Image,
		page_Mapped,
		page_Private
	};

	//------------------------------------------------------------------------
	enum EPlatform
	{
		Platform_Windows,
		Platform_Unix,
		Platform_PS4,
	};

	//------------------------------------------------------------------------
	typedef int(*ThreadMain)(void*);

	//------------------------------------------------------------------------
	typedef void(*SendPageStateFunction)(void*, size_t, PageState, PageType, unsigned int, bool, int, void*);

	//------------------------------------------------------------------------
	typedef void(*EnumerateLoadedModulesCallbackFunction)(int64, const char*, void*);

	//------------------------------------------------------------------------
	// You don't need to call this directly, it is automatically called on the first allocation.
	// Only call this function if you want to be able to connect to your app before it has allocated any memory.
	// If wait_for_connect is true this function will block until the external MemPro app has connected,
	// this is useful to make sure that every single allocation is being tracked.
	void Initialise(bool wait_for_connect=false);

	void Disconnect();		// kick all current connections, but can accept more

	void Shutdown();		// free all resources, no more connections allowed

	void TrackAlloc(void* p, size_t size, bool wait_for_connect=false);

	void TrackFree(void* p, bool wait_for_connect=false);

	bool IsPaused();

	void SetPaused(bool paused);

	void TakeSnapshot(bool send_memory=false);

	void FlushDumpFile();

	// ignore these, for internal use only
	void IncRef();
	void DecRef();
}

//------------------------------------------------------------------------
#ifndef MEMPRO_WRITE_DUMP
namespace
{
	// if we are using sockets we need to flush the sockets on global teardown
	// This class is a trick to attempt to get mempro to shutdown after all other
	// global objects.
	class MemProGLobalScope
	{
	public:
		MemProGLobalScope() { MemPro::IncRef(); }
		~MemProGLobalScope() { MemPro::DecRef(); }
	};
	static MemProGLobalScope g_MemProGLobalScope;
}
#endif

//------------------------------------------------------------------------
#ifdef OVERRIDE_NEW_DELETE

	#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__WIN32__) || defined(__WINDOWS__)
		#include <malloc.h>

		void* operator new(size_t size)
		{
			void* p = malloc(size);
			MEMPRO_TRACK_ALLOC(p, size);
			return p;
		}

		void operator delete(void* p)
		{
			MEMPRO_TRACK_FREE(p);
			free(p);
		}

		void* operator new[](size_t size)
		{
			void* p = malloc(size);
			MEMPRO_TRACK_ALLOC(p, size);
			return p;
		}

		void operator delete[](void* p)
		{
			MEMPRO_TRACK_FREE(p);
			free(p);
		}
	#endif

#endif

//------------------------------------------------------------------------
#ifdef OVERRIDE_MALLOC_FREE
	
	#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__WIN32__) || defined(__WINDOWS__)
		
		// NOTE: for this to work, you will need to make sure you are linking STATICALLY to the crt. eg: /MTd

		__declspec(restrict) __declspec(noalias) void* malloc(size_t size)
		{
			void* p = HeapAlloc(GetProcessHeap(), 0, size);
			MEMPRO_TRACK_ALLOC(p, size);
			return p;
		}

		__declspec(restrict) __declspec(noalias) void* realloc(void *p, size_t new_size)
		{
			MEMPRO_TRACK_FREE(p);
			void* p_new = HeapReAlloc(GetProcessHeap(), 0, p, new_size);
			MEMPRO_TRACK_ALLOC(p_new, new_size);
			return p_new;
		}

		__declspec(noalias) void free(void *p)
		{
			HeapFree(GetProcessHeap(), 0, p);
			MEMPRO_TRACK_FREE(p);
		}
	#else
		void *malloc(int size)
		{
			void* (*ptr)(int);
			void* handle = (void*)-1;
			ptr = (void*)dlsym(handle, "malloc");
			if(!ptr) abort();
			void *p = (*ptr)(size);
			MEMPRO_TRACK_ALLOC(p, size);
			return p;
		}

		void *realloc(void *p, int size)
		{
			MEMPRO_TRACK_FREE(p);
			void * (*ptr)(void *, int);
			void * handle = (void*) -1;
			ptr = (void*)dlsym(handle, "realloc");
			if (!ptr) abort();
			void* p_new = (*ptr)(p, size);
			MEMPRO_TRACK_ALLOC(p_new, size);
			return p_new;
		}

		void free(void *p)
		{
			MEMPRO_TRACK_FREE(p);
			void* (*ptr)(void*);
			void* handle = (void*)-1;
			ptr = (void*)dlsym(handle, "free");
			if (!ptr == NULL) abort();
			(*ptr)(alloc);
		}
	#endif
#endif

//------------------------------------------------------------------------
#endif		// #if MEMPRO_ENABLED

//------------------------------------------------------------------------
#endif		// #ifndef MEMPRO_MEMPRO_H_INCLUDED

