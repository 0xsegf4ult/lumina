module;

#if defined LUMINA_PLATFORM_POSIX
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
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
	int fd;
	std::size_t size;
	void* mapped;
	bool rw;
};

struct vfs_context_t
{
	std::unordered_map<Handle<File>, File> open_files;
};
static vfs_context_t* vfs_context = nullptr; 

}

export namespace lumina::vfs
{


void init()
{
	vfs_context = new vfs_context_t();
}

void shutdown()
{
	delete vfs_context;
}

using open_return_type = std::expected<Handle<File>, FileOpenError>;
open_return_type open_unscoped(const path& p, access_readonly_t)
{
	Handle<File> fh{fnv::hash(p.c_str())};
	if(vfs_context->open_files.contains(fh))
		return fh;

	if(!std::filesystem::exists(p))
		return std::unexpected(FileOpenError::NoEntry);

	if(std::filesystem::is_directory(p))
		return std::unexpected(FileOpenError::IsDirectory);

	File f{};
	#if defined LUMINA_PLATFORM_POSIX
	f.fd = ::open(p.c_str(), O_RDONLY);
	if(f.fd < 0)
		return std::unexpected(FileOpenError::Unknown);

	struct stat file_info;
	if(fstat(f.fd, &file_info) < 0)
		return std::unexpected(FileOpenError::Unknown);
	f.size = file_info.st_size;

	f.mapped = mmap(nullptr, f.size, PROT_READ, MAP_PRIVATE, f.fd, 0);
	if(f.mapped == MAP_FAILED)
		return std::unexpected(FileOpenError::Unknown);

	#else
	static_assert(false, "not implemented for current platform");
	#endif
	
	f.rw = false;
	vfs_context->open_files[fh] = f;
	return fh;
}

open_return_type open_unscoped(const path& p, access_rw_t)
{
	Handle<File> fh{fnv::hash(p.c_str())};
	if(vfs_context->open_files.contains(fh))
	{
		if(!vfs_context->open_files[fh].rw) [[unlikely]]
		{
			log::critical("Tried to reopen readonly file as rw");
			std::unreachable();
		}
		return fh;
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
	#else
	static_assert(false, "not implemented for current platform");
	#endif

	f.rw = true;
	vfs_context->open_files[fh] = f;
	return fh;
}

void close(Handle<File> h)
{
	const File& f = vfs_context->open_files[h];
	#if defined LUMINA_PLATFORM_POSIX
	munmap(f.mapped, f.size);
	::close(f.fd);
	#else
	static_assert(false, "not implemented for current platform");
	#endif
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
	return std::bit_cast<const T*>(vfs_context->open_files[h].mapped);
}

template <typename T>
T* map(Handle<File> h, access_rw_t)
{
	if(!vfs_context->open_files[h].rw)
	{
		log::critical("Tried to map readonly file as rw");
		std::unreachable();
	}

	return std::bit_cast<T*>(vfs_context->open_files[h].mapped);
}


}
