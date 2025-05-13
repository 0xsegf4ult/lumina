module;

#include <cassert>
#include <tracy/Tracy.hpp>

export module lumina.renderer:texture_registry;
import :resource_format;

import lumina.vulkan;
import lumina.core;
import lumina.vfs;
import std;

using std::uint32_t;

namespace lumina::render
{

export struct Texture;

export class TextureRegistry
{
public:
	TextureRegistry(vulkan::Device* dev) : device{dev}
	{
		texture_dsl = device->get_descriptor_set_layout(
		{
			// BINDING 0 is SAMPLED_IMAGE FS VARIABLE COUNT
			.sampled_image_bindings = 0b1,
			.fs_bindings = 0b1,
			.variable_bindings = 0b1
		}, /* is_push= */ false);
	
		vk::DescriptorPoolSize texpool
		{
			.type = vk::DescriptorType::eCombinedImageSampler,
			.descriptorCount = max_resources
		};

		texture_pool = device->get_handle().createDescriptorPool
		({
			.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
			.maxSets = 1u,
			.poolSizeCount = 1u,
			.pPoolSizes = &texpool
		});

		uint32_t max_binding = max_resources - 1u;
		vk::StructureChain<vk::DescriptorSetAllocateInfo, vk::DescriptorSetVariableDescriptorCountAllocateInfo> alloc_chain =
		{
			{
				.descriptorPool = texture_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &texture_dsl
			},
			{
				.descriptorSetCount = 1,
				.pDescriptorCounts = &max_binding
			}
		};

		texture_dataset = device->get_handle().allocateDescriptorSets(alloc_chain.get<vk::DescriptorSetAllocateInfo>())[0]; 

		streambuf = device->create_buffer
		({
			.domain = vulkan::BufferDomain::Host,
			.usage = vulkan::BufferUsage::StagingBuffer,
			.size = streambuf_size,
			.debug_name = "texture_registry::streambuffer"
		});

		std::array<std::byte, 16> data;
		data.fill(std::byte{255});
		vulkan::ImageHandle nullimg = device->create_image
		({
			.width = 2,
			.height = 2,
			.format = vk::Format::eR8G8B8A8Unorm,
			.usage = vulkan::ImageUsage::ShaderRead,
			.debug_name = "texture_registry::nulltex",
			.initial_data = data.data()
		});
		transfer_ownership(std::move(nullimg));
	}

	~TextureRegistry()
	{
		device->get_handle().destroyDescriptorPool(texture_pool);
	}

	Handle<Texture> transfer_ownership(vulkan::ImageHandle&& tex)
	{
		ZoneScoped;
		std::scoped_lock<std::mutex> lock{cpu_lock};

		if(textures.size() <= next_slot)
		{
			texture_metadata.insert(texture_metadata.end(), (next_slot + 1) - texture_metadata.size(), {});
			textures.reserve(texture_metadata.capacity());
			textures.resize(texture_metadata.size());
		}

		texture_metadata[next_slot] = tex->get_key().debug_name;
		textures[next_slot] = std::move(tex);

		Handle<Texture> hnd = Handle<Texture>{next_slot++};
		descriptor_update({&hnd, 1});
		return hnd;
	}

	Handle<Texture> enqueue_async_texture_load(const std::filesystem::path& path)
	{
		ZoneScoped;
		std::scoped_lock<std::mutex> lock{cpu_lock};
		if(textures.size() <= next_slot)
		{
			texture_metadata.insert(texture_metadata.end(), (next_slot + 1) - texture_metadata.size(), {});
			textures.reserve(texture_metadata.capacity());
			textures.resize(texture_metadata.size());
		}
		async_queue.push_back({path, next_slot});
		return Handle<Texture>{next_slot++};
	}

	vulkan::Image* get_texture(Handle<Texture> handle)
	{
		return textures[handle].get();
	}

	std::string& get_metadata(Handle<Texture> handle)
	{
		return texture_metadata[handle];
	}

	vk::DescriptorSet get_descriptor() const
	{
		return texture_dataset;
	}

	void async_queue_drain()
	{
		ZoneScoped;
		std::unique_lock<std::mutex> lock{cpu_lock, std::defer_lock};
		if(!lock.try_lock())
			return;

		if(in_transfer)
			return;
		
		if(async_queue.empty())
			return;

		struct texture_load_data
		{
			uint32_t size;
			uint32_t offset;
			Handle<Texture> texture;
			vulkan::ImageHandle image{};
		};

		uint32_t qsize = static_cast<uint32_t>(async_queue.size());
		uint32_t i = 0;
		uint32_t used = 0;
		std::vector<texture_load_data> load_data;
		std::vector<Handle<Texture>> to_update;

		for(i = 0; i < qsize; i++)
		{
			auto& entry = async_queue[i];

			auto file = vfs::open(entry.path, vfs::access_readonly);
			if(!file.has_value())
			{
				log::error("texture_registry: failed to load texture {}: {}", entry.path.string(), vfs::file_open_error(file.error()));
				continue;
			}

			const std::byte* ptr = vfs::map<std::byte>(*file, vfs::access_readonly);

			auto* header = reinterpret_cast<const TextureFileFormat::Header*>(ptr);
			if(header->magic != TextureFileFormat::fmt_magic || header->vmajor != TextureFileFormat::fmt_major_version)
			{
				log::error("texture_registry: failed to load texture {}: invalid file", entry.path.string());
				continue;
			}

			if(header->texformat == TextureFileFormat::TextureFormat::Invalid)
			{
				log::error("texture_registry: texture {} is in an invalid format", entry.path.string());
				continue;
			}

			auto* res_table = reinterpret_cast<const TextureFileFormat::SubresourceDescription*>(ptr + header->subres_desc_offset);
			uint32_t tex_size = 0u;
			uint32_t num_mips = 0;
			uint32_t num_layers = 0;
			for(uint32_t l = 0; l < header->num_subres; l++)
			{
				tex_size += res_table[l].data_size_bytes;
				num_mips = std::max(num_mips, res_table[l].level + 1);
				num_layers = std::max(num_layers, res_table[l].layer + 1);
			}

			if(used + tex_size <= streambuf_size)
			{
				load_data.push_back
				({
					.size = tex_size, .offset = used,
					.texture = Handle<Texture>{entry.promised_handle}
				});
				auto& last = load_data.back();

				last.image = device->create_image
				({
					.width = res_table[0].width,
					.height = res_table[0].height,
					.levels = num_mips,
					.layers = num_layers,
					.format = TextureFileFormat::to_vkformat(header->texformat),
					.usage = vulkan::ImageUsage::ShaderRead,
					.debug_name = entry.path.string()
				});
				to_update.push_back(Handle<Texture>{entry.promised_handle});
				memcpy(streambuf->map<std::byte>() + used, ptr + res_table[0].data_offset, tex_size);
				used += tex_size;
			}
			else
				break;
		}
		if(load_data.empty())
			return;
		
		async_queue.erase(async_queue.begin(), async_queue.begin() + i);
		in_transfer = true;
		lock.unlock();

		log::debug("texture_registry: start async cb");
		auto tcb = device->request_command_buffer(vulkan::Queue::Transfer, "texture_registry_copy");
		{
			for(uint32_t j = 0; j < load_data.size(); j++)
			{
				auto& info = load_data[j];

				tcb.pipeline_barrier
				({{
					.src_stage = vk::PipelineStageFlagBits2::eTopOfPipe,
					.dst_stage = vk::PipelineStageFlagBits2::eTransfer,
					.dst_access = vk::AccessFlagBits2::eTransferWrite,
					.src_layout = vk::ImageLayout::eUndefined,
					.dst_layout = vk::ImageLayout::eTransferDstOptimal,
					.image = info.image.get()
				}});

				const vulkan::ImageKey& key = info.image->get_key();

				for(uint32_t level = 0; level < key.levels; level++)
				{
					vk::BufferImageCopy copy_region
					{
						.bufferOffset = info.offset + info.image->get_subresource(level, 0).byte_offset,
						.imageSubresource = {vk::ImageAspectFlagBits::eColor, level, 0, key.layers},
						.imageExtent = {info.image->get_subresource(level, 0).width, info.image->get_subresource(level, 0).height, 1}
					};

					tcb.vk_object().copyBufferToImage(streambuf->handle, info.image->get_handle(), vk::ImageLayout::eTransferDstOptimal, {copy_region});
				}

				tcb.pipeline_barrier
				({{
					.src_stage = vk::PipelineStageFlagBits2::eTransfer,
					.src_access = vk::AccessFlagBits2::eTransferWrite,
					.dst_stage = vk::PipelineStageFlagBits2::eAllCommands,
					.src_layout = vk::ImageLayout::eTransferDstOptimal,
					.dst_layout = vk::ImageLayout::eTransferDstOptimal,
					.src_queue = vulkan::Queue::Transfer,
					.dst_queue = vulkan::Queue::Graphics,
					.image = info.image.get() 
				}});
			}
		}
		auto ttv = device->submit(tcb, vulkan::submit_signal_timeline);

		log::debug("texture_registry: wait for async cb {} entries", load_data.size());
		auto gcb = device->request_command_buffer(vulkan::Queue::Graphics, "gfx_tex_acb_wait");
		gcb.debug_name("gfx_tex_acb_wait");
		{
			gcb.add_wait_semaphore({vulkan::Queue::Transfer, ttv, vk::PipelineStageFlagBits2::eFragmentShader});
			for(uint32_t j = 0; j < load_data.size(); j++)
			{
				gcb.pipeline_barrier
				({{
					.src_stage = vk::PipelineStageFlagBits2::eFragmentShader,
					.dst_stage = vk::PipelineStageFlagBits2::eFragmentShader,
					.dst_access = vk::AccessFlagBits2::eShaderRead,
					.src_layout = vk::ImageLayout::eTransferDstOptimal,
					.dst_layout = vk::ImageLayout::eShaderReadOnlyOptimal,
					.src_queue = vulkan::Queue::Transfer,
					.dst_queue = vulkan::Queue::Graphics,
					.image = load_data[j].image.get()
				}});
			}
		}
		log::debug("texture_registry: pre gfx queue submit");
		auto gtv = device->submit(gcb, vulkan::submit_signal_timeline);
		log::debug("texture_registry: post gfx queue submit");
		device->wait_timeline(vulkan::Queue::Graphics, gtv);
		log::debug("texture_registry: transfer complete, uploaded {} textures", i);

		lock.lock();
		for(auto& entry : load_data)
		{
			texture_metadata[entry.texture] = entry.image->get_key().debug_name;
			textures[entry.texture] = std::move(entry.image);
		}

		descriptor_update(to_update);

		in_transfer = false;
	}

	void clear()
	{
		std::scoped_lock<std::mutex> lock{cpu_lock};
		textures.erase(textures.begin() + 1, textures.end());
		texture_metadata.erase(texture_metadata.begin() + 1, texture_metadata.end());
		next_slot = 1;
	}
private:
	void descriptor_update(std::span<Handle<Texture>> hnd)
	{
		ZoneScoped;
		std::vector<vk::DescriptorImageInfo> img_info;

		for(auto& tex : hnd)
		{
			auto& texture = textures[tex];

			img_info.push_back
			({
				.sampler = device->get_prefab_sampler(vulkan::SamplerPrefab::TextureAnisotropic),
				.imageView = texture->get_default_view()->get_handle(),
				.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
			});
		}

		vk::WriteDescriptorSet write
		{
			.dstSet = texture_dataset,
			.dstBinding = 0,
			.dstArrayElement = hnd[0],
			.descriptorCount = static_cast<uint32_t>(hnd.size()),
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			.pImageInfo = img_info.data()
		};

		device->get_handle().updateDescriptorSets(1u, &write, 0u, nullptr);
	}

	vulkan::Device* device;

	constexpr static uint32_t max_resources = 65536;
	vk::DescriptorSet texture_dataset;
	vk::DescriptorPool texture_pool;
	vk::DescriptorSetLayout texture_dsl;

	uint32_t streambuf_size = 64 * 1024 * 1024;
	vulkan::BufferHandle streambuf;
	
	struct AsyncLoadRequest
	{
		std::filesystem::path path;
		uint32_t promised_handle;
	};

	bool in_transfer{false};
	std::mutex cpu_lock;
	std::vector<AsyncLoadRequest> async_queue;

	uint32_t next_slot = 0;
	std::vector<std::string> texture_metadata;
	std::vector<vulkan::ImageHandle> textures;
};

}
