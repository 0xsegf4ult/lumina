module;

#if defined LUMINA_PLATFORM_POSIX
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#elif defined LUMINA_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

export module lumina.vfs;
import lumina.core;
import std;

namespace lumina::vfs
{

export using std::filesystem::path;

export struct access_readonly_t{};
export constexpr access_readonly_t access_readonly;

export struct access_rw_t{};
export constexpr access_rw_t access_rw;

export enum class FileOpenError
{
	Unknown,
	NoEntry,
	IsDirectory
};

export std::string_view file_open_error(FileOpenError e)
{
	switch(e)
	{
	using enum FileOpenError;
	case Unknown:
	return "unknown error";
	case NoEntry:
	return "file does not exist";
	case IsDirectory:
	return "file is directory";
	}
}

export struct File
{
	#if defined LUMINA_PLATFORM_POSIX
	int fd;
	#elif defined LUMINA_PLATFORM_WIN32
	HANDLE fd;
	HANDLE map;
	#endif
	std::size_t size;
	void* mapped;
	bool rw;
};

struct vfs_context_t
{
	std::unordered_map<Handle<File>, File> open_files;
	std::shared_mutex lock;
};
vfs_context_t* vfs_context = nullptr; 

}

export namespace lumina::vfs
{


void init()
{
	vfs_context = new vfs_context_t();

	#if defined LUMINA_PLATFORM_POSIX
	struct rlimit lim;
	getrlimit(RLIMIT_NOFILE, &lim);
	lim.rlim_cur = 65536;
	setrlimit(RLIMIT_NOFILE, &lim);
	#endif
}

void shutdown()
{
	delete vfs_context;
}

using open_return_type = std::expected<Handle<File>, FileOpenError>;
open_return_type open_unscoped(const path& p, access_readonly_t)
{
	Handle<File> fh{fnv::hash(p.c_str())};
	{
	std::scoped_lock<std::shared_mutex> r_lock{vfs_context->lock};

	if(vfs_context->open_files.contains(fh))
		return fh;

	}

	if(!std::filesystem::exists(p))
		return std::unexpected(FileOpenError::NoEntry);

	if(std::filesystem::is_directory(p))
		return std::unexpected(FileOpenError::IsDirectory);

	File f{};
	#if defined LUMINA_PLATFORM_POSIX
	f.fd = ::open(p.c_str(), O_RDONLY);
	if(f.fd < 0)
	{
		std::perror("failed to open file");
		return std::unexpected(FileOpenError::Unknown);
	}
	struct stat file_info;
	if(fstat(f.fd, &file_info) < 0)
	{
		std::perror("failed to stat file");
		return std::unexpected(FileOpenError::Unknown);
	}
	f.size = static_cast<std::size_t>(file_info.st_size);

	f.mapped = mmap(nullptr, f.size, PROT_READ, MAP_PRIVATE, f.fd, 0);
	if(f.mapped == MAP_FAILED)
	{
		std::perror("failed to mmap file");
		return std::unexpected(FileOpenError::Unknown);
	}
	#elif defined LUMINA_PLATFORM_WIN32
	f.fd = CreateFileW
	(
		p.c_str(),
		GENERIC_READ,
		0,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		0
	);

	if(f.fd == INVALID_HANDLE_VALUE)
	{
		log::error("CreateFileW failed with error {}", GetLastError());
		return std::unexpected(FileOpenError::Unknown);
	}

	LARGE_INTEGER fsize;
	if(!GetFileSizeEx(f.fd, &fsize))
	{
		log::error("GetFileSizeEx failed with error {}", GetLastError());
		CloseHandle(f.fd);
		return std::unexpected(FileOpenError::Unknown);
	}

	f.size = static_cast<size_t>(fsize.QuadPart);

	f.map = CreateFileMapping
	(
		f.fd,
		nullptr,
		PAGE_READONLY,
		0, 
		0,
		nullptr
	);

	if(f.map == 0)
	{
		log::error("CreateFileMapping failed with error {}", GetLastError());
		CloseHandle(f.fd);
		return std::unexpected(FileOpenError::Unknown);
	}

	f.mapped = MapViewOfFile
	(
		f.map,
		FILE_MAP_READ,
		0, 0, 0
	);

	if(f.mapped == nullptr)
	{
		log::error("MapViewOfFile failed with error {}", GetLastError());
		CloseHandle(f.map);
		CloseHandle(f.fd);
		return std::unexpected(FileOpenError::Unknown);
	}
	#else
	static_assert(false, "not implemented for current platform");
	#endif
	
	f.rw = false;
	
	{
	std::unique_lock<std::shared_mutex> w_lock{vfs_context->lock};
	vfs_context->open_files[fh] = f;
	}

	return fh;
}

open_return_type open_unscoped(const path& p, access_rw_t)
{
	Handle<File> fh{fnv::hash(p.c_str())};
	
	{
	std::scoped_lock<std::shared_mutex> r_lock{vfs_context->lock};
	
	if(vfs_context->open_files.contains(fh))
	{
		if(!vfs_context->open_files[fh].rw) [[unlikely]]
		{
			log::critical("Tried to reopen readonly file as rw");
			std::unreachable();
		}
		return fh;
	}

	}

	if(std::filesystem::exists(p))
		return std::unexpected(FileOpenError::NoEntry);

	if(std::filesystem::is_directory(p))
		return std::unexpected(FileOpenError::IsDirectory);

	File f{};
	#if defined LUMINA_PLATFORM_POSIX
	f.fd = ::open(p.c_str(), O_RDWR);
	if(f.fd < 0)
		return std::unexpected(FileOpenError::Unknown);

	struct stat file_info;
	if(fstat(f.fd, &file_info) < 0)
		return std::unexpected(FileOpenError::Unknown);
	f.size = file_info.st_size;

	f.mapped = mmap(nullptr, f.size, PROT_READ | PROT_WRITE, MAP_PRIVATE, f.fd, 0);
	if(f.mapped == MAP_FAILED)
		return std::unexpected(FileOpenError::Unknown);
	#elif defined LUMINA_PLATFORM_WIN32
	log::critical("vfs_win32: open_rw STUBBED");
	#else
	static_assert(false, "not implemented for current platform");
	#endif

	f.rw = true;
	
	{
	std::unique_lock<std::shared_mutex> w_lock{vfs_context->lock};
	vfs_context->open_files[fh] = f;
	}

	return fh;
}

void close(Handle<File> h)
{
	{
	std::scoped_lock<std::shared_mutex> r_lock{vfs_context->lock};
	const File& f = vfs_context->open_files[h];
	#if defined LUMINA_PLATFORM_POSIX
	munmap(f.mapped, f.size);
	::close(f.fd);
	#elif defined LUMINA_PLATFORM_WIN32
	UnmapViewOfFile(f.mapped);
	CloseHandle(f.map);
	CloseHandle(f.fd);
	#else
	static_assert(false, "not implemented for current platform");
	#endif
	}

	std::unique_lock<std::shared_mutex> w_lock{vfs_context->lock};
	vfs_context->open_files.erase(h);
}

struct ScopedFileHandle : public open_return_type
{
	~ScopedFileHandle()
	{
		if(has_value())
			vfs::close(this->operator*());
	}
};

ScopedFileHandle open(const path& p, access_readonly_t ro_tag)
{
	return {open_unscoped(p, ro_tag)};
}

ScopedFileHandle open(const path& p, access_rw_t rw_tag)
{
	return {open_unscoped(p, rw_tag)};
}

template <typename T>
const T* map(Handle<File> h, access_readonly_t)
{
	std::scoped_lock<std::shared_mutex> r_lock{vfs_context->lock};
	return std::bit_cast<const T*>(vfs_context->open_files[h].mapped);
}

template <typename T>
T* map(Handle<File> h, access_rw_t)
{
	std::scoped_lock<std::shared_mutex> r_lock{vfs_context->lock};

	if(!vfs_context->open_files[h].rw)
	{
		log::critical("Tried to map readonly file as rw");
		std::unreachable();
	}

	return std::bit_cast<T*>(vfs_context->open_files[h].mapped);
}


}
