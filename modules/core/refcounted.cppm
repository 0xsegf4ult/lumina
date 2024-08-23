export module lumina.core:refcounted;

import std;

export namespace lumina
{

template <typename T, typename Deleter = std::default_delete<T>>
class RefCountEnabled
{
public:
	RefCountEnabled() = default;
	RefCountEnabled(const RefCountEnabled&) = delete;
	RefCountEnabled& operator=(const RefCountEnabled&) = delete;

	void add_ref() noexcept
	{
		refcount.fetch_add(1, std::memory_order_relaxed);
	}

	void release() noexcept
	{
		if(refcount.fetch_sub(1, std::memory_order_release) == 1u)
		{
			atomic_thread_fence(std::memory_order_acquire);
			Deleter()(static_cast<T*>(this));
		}
	}
private:
	std::atomic<std::uint32_t> refcount{0u};
};

template <typename T>
class RefCounted
{
public:
	explicit RefCounted(T* ptr) : data{ptr}
	{
		if(data)
			data->add_ref();
	}

	RefCounted() : data{nullptr} {}
	~RefCounted()
	{
		if(data)
			data->release();
	}

	RefCounted(const RefCounted& other) noexcept : data{other.data}
	{
		if(data)
			data->add_ref();
	}

	constexpr RefCounted(RefCounted&& other) noexcept : data{other.data}
	{
		other.data = nullptr;
	}

	RefCounted& operator=(const RefCounted& other) noexcept
	{
		if(data != other.data)
		{
			if(data)
				data->release();
			data = other.data;
			if(data)
				data->add_ref();
		}
		return *this;
	}

	RefCounted& operator=(RefCounted&& other) noexcept
	{
		if(data != other.data)
		{
			if(data)
				data->release();
			data = other.data;
			other.data = nullptr;
		}
		return *this;
	}

	constexpr T& operator*() noexcept
	{
		return *data;
	}

	constexpr const T& operator*() const noexcept
	{
		return *data;
	}

	constexpr T* operator->() noexcept
	{
		return data;
	}

	constexpr const T* operator->() const noexcept
	{
		return data;
	}

	explicit operator bool() const noexcept
	{
		return data != nullptr;
	}

	constexpr bool operator==(const RefCounted& rhs) const noexcept
	{
		return data == rhs.data;
	}

	constexpr bool operator!=(const RefCounted& rhs) const noexcept
	{
		return data != rhs.data;
	}
private:
	T* data;
};

}
