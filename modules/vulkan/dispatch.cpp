import vulkan_hpp;

// relies on https://github.com/KhronosGroup/Vulkan-Hpp/pull/2146/commits/64e731a07ab2a45b7aa1709483703ae1e5f1401b
// wait for new vulkan-headers release or build them locally

namespace vk::detail
{
	DispatchLoaderDynamic defaultDispatchLoaderDynamic;
}
