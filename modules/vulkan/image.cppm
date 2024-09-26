export module lumina.vulkan:image;

import vulkan_hpp;
import std;

using std::uint32_t, std::size_t, std::uint64_t;

export namespace lumina::vulkan
{

class Device;
class Image;

bool is_depth_format(vk::Format fmt);
vk::ImageType image_type_from_size([[maybe_unused]]uint32_t w, uint32_t h, uint32_t d);
vk::ImageViewType get_imageview_type(vk::ImageType type);
uint32_t get_mip_levels(uint32_t w, uint32_t h);
uint32_t format_blockdim(vk::Format fmt);
uint32_t format_blocksize(vk::Format fmt);
uint32_t size_for_image(uint32_t w, uint32_t h, vk::Format fmt);

struct ImageViewKey
{
	const Image* image = nullptr;
	vk::ImageViewType view_type = vk::ImageViewType::e2D;
	vk::Format format = vk::Format::eUndefined;
	uint32_t level = 0;
	uint32_t layer = 0;
	uint32_t levels = vk::RemainingMipLevels;
	uint32_t layers = vk::RemainingArrayLayers;
	std::string debug_name;
};

class ImageView
{
public:
	ImageView(Device* dev, const ImageViewKey& k, vk::ImageView v) : device{dev}, key{k}, handle{v} {}
	~ImageView();

	ImageView(const ImageView&) = delete;
	ImageView(ImageView&&) = delete;

	ImageView& operator=(const ImageView&) = delete;
	ImageView& operator=(ImageView&&) = delete;

	constexpr vk::ImageView get_handle() const
	{
		return handle;
	}

	const ImageViewKey& get_key() const
	{
		return key;
	}
private:
	Device* device;
	ImageViewKey key;
	vk::ImageView handle;
};

using ImageViewHandle = std::unique_ptr<ImageView>;

enum class ImageUsage
{
	Undefined,
	ShaderRead,
	DepthShaderRead,
	TransferSource,
	TransferDest,
	ColorAttachment,
	DepthAttachment,
	Framebuffer,
	RWGraphics,
	RWCompute,
	RWGeneric,
	PresentSource
};

vk::ImageUsageFlags decode_image_usage(ImageUsage usage);

struct ImageKey
{
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t depth = 1;
	uint32_t levels = 1;
	uint32_t layers = 1;
	vk::Format format = vk::Format::eUndefined;
	ImageUsage usage = ImageUsage::Undefined;
	std::string debug_name;
	void* initial_data = nullptr;
};

class Image
{
public:
	Image(Device* dev, const ImageKey& k, vk::Image img, vk::DeviceMemory m);
	~Image();

	Image(const Image&) = delete;
	Image(Image&&) = delete;

	Image& operator=(const Image&) = delete;
	Image& operator=(Image&&) = delete;

	vk::Image get_handle() const
	{
		return handle;
	}

	ImageView* get_default_view(size_t index = 0) const
	{
		return default_views[index].get();
	}

	const ImageKey& get_key() const
	{
		return key;
	}

	void disown();
	void disown_memory();

	struct MipInfo
	{
		uint32_t width;
		uint32_t height;
		uint32_t byte_offset;
	};

	std::span<const MipInfo> get_mips() const
	{
		return mips;
	}

	const MipInfo& get_mip(uint32_t level) const
	{
		return mips[level];
	}

	vk::Extent2D get_extent2D() const 
	{
		return {key.width, key.height};
	}
private:
	bool owns_image = true;
	bool owns_memory = true;

	std::vector<MipInfo> mips;
	std::vector<ImageViewHandle> default_views;
	Device* device;
	ImageKey key;
	vk::Image handle;
	vk::DeviceMemory memory;
};

using ImageHandle = std::unique_ptr<Image>;

}
