module;

#if defined LUMINA_PLATFORM_POSIX
#include <sys/mman.h>
#include <cerrno>
#include <cstring>
#elif defined LUMINA_PLATFORM_WIN32
#include <memoryapi.h>
#endif

export module lumina.core:object_pool;
export import :handle;
import lumina.core.log;

import std;

using std::size_t, std::uint32_t, std::uint64_t;

export namespace lumina
{

template <typename T>
class ObjectPool
{
public:
	using handle_type = Handle<T>;
	constexpr static uint32_t invalid_object = 0xFFFFFFFF;
	static_assert(std::atomic<uint32_t>::is_always_lock_free, "atomic u32 must be lock free");
	static_assert(std::atomic<uint32_t>::is_always_lock_free, "atomic u64 must be lock free");

	struct InternalObject
	{
		T data;
		std::atomic<uint32_t> next{invalid_object};
	};
	static_assert(alignof(InternalObject) == alignof(T));

	ObjectPool(std::string_view unique_name) : name{unique_name} {}
	~ObjectPool()
	{
		#if defined LUMINA_PLATFORM_POSIX
		munmap(objects, map_length);
		#elif defined LUMINA_PLATFORM_WIN32
		VirtualFree(objects, 0ul, MEM_RELEASE);
		#endif
	}

	void init(uint32_t max_obj, uint32_t prefault = 1)
	{
		max_objects = max_obj;
		map_length = max_objects * sizeof(InternalObject);
		const uint32_t pf_size = ((prefault * sizeof(InternalObject)) + 4096u) / 4096u;
		[[maybe_unused]] const uint32_t pf_objects = std::min<uint32_t>(pf_size * 4096u / sizeof(InternalObject), max_objects);

		#if defined LUMINA_PLATFORM_POSIX
		objects = reinterpret_cast<InternalObject*>(mmap(nullptr, map_length, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0));
		if(objects == MAP_FAILED)
			log::critical("object_pool[{}]: mmap {} bytes failed with error {}", name, map_length, std::strerror(errno));
		#elif defined LUMINA_PLATFORM_WIN32
		objects = reinterpret_cast<InternalObject*>(VirtualAlloc(nullptr, map_length, MEM_RESERVE, PAGE_NOACCESS));
		VirtualAlloc(objects, prefault * sizeof(InternalObject), MEM_COMMIT, PAGE_READWRITE);
		num_allocations = pf_objects;
		#else
		static_assert(false, "no mmap equivalent for current platform");
		#endif

		monotonic_ctr = 0u;
		xchg_aba_tag = 0u;
		freelist_head = invalid_object;
	}

	template <typename... Args>
	handle_type allocate(Args&&... args)
	{
		for(;;)
		{
			uint64_t tag_fh = freelist_head.load(std::memory_order_acquire);
			uint32_t fhead = tag_fh & 0xFFFFFFFF;
			if(fhead == invalid_object)
			{
				fhead = monotonic_ctr.fetch_add(1, std::memory_order_relaxed);
				if(fhead >= max_objects)
				{
					log::critical("object_pool[{}]: out of handles", name);
					return handle_type{invalid_object};
				}

				return internal_allocate(fhead, std::forward<Args>(args)...);
			}

			uint32_t next = get_internal(fhead).next.load(std::memory_order_acquire);
			uint64_t new_freelist_head = (static_cast<uint64_t>(xchg_aba_tag.fetch_add(1, std::memory_order_relaxed)) << 32u) | next;

			if(freelist_head.compare_exchange_weak(tag_fh, new_freelist_head, std::memory_order_release))
				return internal_allocate(fhead, std::forward<Args>(args)...);
		}
	}

	void deallocate(handle_type handle) noexcept
	{
		InternalObject& obj = get_internal(handle);
		obj.data.~T();

		for(;;)
		{
			uint64_t tag_fh = freelist_head.load(std::memory_order_acquire);
			uint32_t fhead = tag_fh & 0xFFFFFFFF;
			
			obj.next.store(fhead, std::memory_order_release);
			
			uint64_t new_freelist_head = (static_cast<uint64_t>(xchg_aba_tag.fetch_add(1u, std::memory_order_relaxed)) << 32u) | handle;
			if(freelist_head.compare_exchange_weak(tag_fh, new_freelist_head, std::memory_order_release))
			{
				return;
			}
		}
	}

	constexpr T& get(handle_type handle) noexcept
	{
		return objects[handle].data;
	}

	constexpr const T& get(handle_type handle) const noexcept
	{
		return objects[handle].data;
	}
private:
	constexpr InternalObject& get_internal(uint32_t handle) noexcept
	{
		return objects[handle];
	}

	template <typename... Args>
	handle_type internal_allocate(uint32_t index, Args&&... args)
	{
		//FIXME: test on win32
		#if defined LUMINA_PLATFORM_WIN32
		num_allocations = std::max(num_allocations, index + 1);
		VirtualAlloc(objects, sizeof(InternalObject) * num_allocations, MEM_COMMIT, PAGE_READWRITE);
		#endif

		InternalObject& obj = get_internal(index);
		::new (&obj.data) T(std::forward<Args>(args)...);
		obj.next.store(index, std::memory_order_release);
		return handle_type{index};
	}

	std::string_view name;
	
	InternalObject* objects;
	uint32_t max_objects;
	size_t map_length;

	#if defined LUMINA_PLATFORM_WIN32
	std::atomic<uint32_t> num_allocations;
	#endif

	std::atomic<uint32_t> monotonic_ctr;
	std::atomic<uint32_t> xchg_aba_tag;
	std::atomic<uint64_t> freelist_head;
};

}

