#include "hzpch.h"
#include "VulkanTexture.h"

#include "VulkanContext.h"
#include "stb_image.h"

#include "VulkanImage.h"

namespace Hazel {

	namespace Utils {
		
		static void InsertImageMemoryBarrier(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkAccessFlags srcAccessMask,
			VkAccessFlags dstAccessMask,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			VkImageSubresourceRange subresourceRange)
		{
			VkImageMemoryBarrier imageMemoryBarrier{};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			imageMemoryBarrier.srcAccessMask = srcAccessMask;
			imageMemoryBarrier.dstAccessMask = dstAccessMask;
			imageMemoryBarrier.oldLayout = oldImageLayout;
			imageMemoryBarrier.newLayout = newImageLayout;
			imageMemoryBarrier.image = image;
			imageMemoryBarrier.subresourceRange = subresourceRange;

			vkCmdPipelineBarrier(
				cmdbuffer,
				srcStageMask,
				dstStageMask,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier);
		}

		static VkSamplerAddressMode VulkanSamplerWrap(TextureWrap wrap)
		{
			switch (wrap)
			{
				case TextureWrap::Clamp:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				case TextureWrap::Repeat:  return VK_SAMPLER_ADDRESS_MODE_REPEAT;
			}
			HZ_CORE_ASSERT(false, "Unknown wrap mode");
			return (VkSamplerAddressMode)0;
		}

		static VkFilter VulkanSamplerFilter(TextureFilter filter)
		{
			switch (filter)
			{
				case TextureFilter::Linear:   return VK_FILTER_LINEAR;
				case TextureFilter::Nearest:  return VK_FILTER_NEAREST;
			}
			HZ_CORE_ASSERT(false, "Unknown filter");
			return (VkFilter)0;
		}

	}

	//////////////////////////////////////////////////////////////////////////////////
	// Texture2D
	//////////////////////////////////////////////////////////////////////////////////

	VulkanTexture2D::VulkanTexture2D(const std::string& path, TextureProperties properties)
		: m_Path(path), m_Properties(properties)
	{
		int width, height, channels;
		if (stbi_is_hdr(path.c_str()))
		{
			m_ImageData.Data = (byte*)stbi_loadf(path.c_str(), &width, &height, &channels, 4);
			m_ImageData.Size = width * height * 4 * sizeof(float);
			m_Format = ImageFormat::RGBA32F;
		}
		else
		{
			//stbi_set_flip_vertically_on_load(1);
			m_ImageData.Data = stbi_load(path.c_str(), &width, &height, &channels, 4);
			m_ImageData.Size = width * height * 4;
			m_Format = ImageFormat::RGBA;
		}

		//HZ_CORE_ASSERT(m_ImageData.Data, "Failed to load image!");
		HZ_CORE_WARN("Failed to load image!");
		m_Width = width;
		m_Height = height;

		HZ_CORE_ASSERT(m_Format != ImageFormat::None);

		Ref<VulkanTexture2D> instance = this;
		Renderer::Submit([instance]() mutable
		{
			instance->Invalidate();
		});
	}

	VulkanTexture2D::VulkanTexture2D(ImageFormat format, uint32_t width, uint32_t height, const void* data, TextureProperties properties)
		: m_Format(format), m_Properties(properties)
	{
		m_Width = width;
		m_Height = height;

		HZ_CORE_ASSERT(format == ImageFormat::RGBA);
		uint32_t size = width * height * 4;

		m_ImageData = Buffer::Copy(data, size);
		memcpy(m_ImageData.Data, data, m_ImageData.Size);
		
		Ref<VulkanTexture2D> instance = this;
		Renderer::Submit([instance]() mutable
		{
			instance->Invalidate();
		});
	}

	VulkanTexture2D::~VulkanTexture2D()
	{
		Ref<Image2D> image = m_Image;
		Renderer::Submit([image]() mutable
		{
			image->Release();
		});
	}

	void VulkanTexture2D::Invalidate()
	{
		auto device = VulkanContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();

		if (m_Image)
			m_Image->Release();

		m_Image = Image2D::Create(ImageFormat::RGBA, m_Width, m_Height);
		Ref<VulkanImage2D> image = m_Image.As<VulkanImage2D>();
		auto& info = image->GetImageInfo();

		VkDeviceSize size = m_ImageData.Size;

		VkFormat format = Utils::VulkanImageFormat(m_Format);
		uint32_t mipCount = GetMipLevelCount();

		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

		VulkanAllocator allocator("Texture2D");

		// Create staging buffer
		VkBufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = size;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VkBuffer stagingBuffer;
		VmaAllocation stagingBufferAllocation = allocator.AllocateBuffer(bufferCreateInfo, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer);
		
		// Copy data to staging buffer
		uint8_t* destData =	allocator.MapMemory<uint8_t>(stagingBufferAllocation);
		memcpy(destData, m_ImageData.Data, size);
		allocator.UnmapMemory(stagingBufferAllocation);

		VkImageCreateInfo imageCreateInfo{};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipCount;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { m_Width, m_Height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		info.MemoryAlloc = allocator.AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY, info.Image);

		VkCommandBuffer copyCmd = device->GetCommandBuffer(true);

		// Image memory barriers for the texture image

		// The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
		VkImageSubresourceRange subresourceRange = {};
		// Image only contains color data
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		// Start at first mip level
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;

		// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
		VkImageMemoryBarrier imageMemoryBarrier{};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.image = info.Image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		imageMemoryBarrier.srcAccessMask = 0;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
		// Source pipeline stage is host write/read exection (VK_PIPELINE_STAGE_HOST_BIT)
		// Destination pipeline stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
		vkCmdPipelineBarrier(
			copyCmd,
			VK_PIPELINE_STAGE_HOST_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &imageMemoryBarrier);

		VkBufferImageCopy bufferCopyRegion = {};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = m_Width;
		bufferCopyRegion.imageExtent.height = m_Height;
		bufferCopyRegion.imageExtent.depth = 1;
		bufferCopyRegion.bufferOffset = 0;

		// Copy mip levels from staging buffer
		vkCmdCopyBufferToImage(
			copyCmd,
			stagingBuffer,
			info.Image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&bufferCopyRegion);

#if 0
		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
		// Source pipeline stage stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
		// Destination pipeline stage fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
		vkCmdPipelineBarrier(
			copyCmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &imageMemoryBarrier);

#endif

		Utils::InsertImageMemoryBarrier(copyCmd, info.Image,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			subresourceRange);

		device->FlushCommandBuffer(copyCmd);

		// Clean up staging resources
		allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// CREATE TEXTURE SAMPLER
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Create a texture sampler
		VkSamplerCreateInfo sampler{};
		sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler.maxAnisotropy = 1.0f;
		sampler.magFilter = Utils::VulkanSamplerFilter(m_Properties.SamplerFilter);
		sampler.minFilter = Utils::VulkanSamplerFilter(m_Properties.SamplerFilter);
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = Utils::VulkanSamplerWrap(m_Properties.SamplerWrap);
		sampler.addressModeV =  Utils::VulkanSamplerWrap(m_Properties.SamplerWrap);
		sampler.addressModeW =  Utils::VulkanSamplerWrap(m_Properties.SamplerWrap);
		sampler.mipLodBias = 0.0f;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;
		sampler.maxLod = (float)mipCount;
		// Enable anisotropic filtering
		// This feature is optional, so we must check if it's supported on the device

		// TODO:
		/*if (vulkanDevice->features.samplerAnisotropy) {
			// Use max. level of anisotropy for this example
			sampler.maxAnisotropy = 1.0f;// vulkanDevice->properties.limits.maxSamplerAnisotropy;
			sampler.anisotropyEnable = VK_TRUE;
		}
		else {
			// The device does not support anisotropic filtering
			sampler.maxAnisotropy = 1.0;
			sampler.anisotropyEnable = VK_FALSE;
		}*/
		sampler.maxAnisotropy = 1.0;
		sampler.anisotropyEnable = VK_FALSE;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(vulkanDevice, &sampler, nullptr, &info.Sampler));

		// Create image view
		// Textures are not directly accessed by the shaders and
		// are abstracted by image views containing additional
		// information and sub resource ranges
		VkImageViewCreateInfo view{};
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = format;
		view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
		// It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;
		view.subresourceRange.levelCount = mipCount;
		view.image = info.Image;
		VK_CHECK_RESULT(vkCreateImageView(vulkanDevice, &view, nullptr, &info.ImageView));

		GenerateMips();
		image->UpdateDescriptor();
	}

	void VulkanTexture2D::Bind(uint32_t slot) const
	{
	}

	void VulkanTexture2D::Lock()
	{
	}

	void VulkanTexture2D::Unlock()
	{
	}

	Buffer VulkanTexture2D::GetWriteableBuffer()
	{
		return m_ImageData;
	}

	bool VulkanTexture2D::Loaded() const
	{
		return true;
	}

	const std::string& VulkanTexture2D::GetPath() const
	{
		return m_Path;
	}

	uint32_t VulkanTexture2D::GetMipLevelCount() const
	{
		return Utils::CalculateMipCount(m_Width, m_Height);
	}

	void VulkanTexture2D::GenerateMips()
	{
		auto device = VulkanContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();

		Ref<VulkanImage2D> image = m_Image.As<VulkanImage2D>();
		auto& info = image->GetImageInfo();

		VkCommandBuffer blitCmd = VulkanContext::GetCurrentDevice()->GetCommandBuffer(true);

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = info.Image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		auto mipLevels = GetMipLevelCount();
		for (int32_t i = 1; i < mipLevels; i++)
		{
			VkImageBlit imageBlit{};

			// Source
			imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.srcSubresource.layerCount = 1;
			imageBlit.srcSubresource.mipLevel = i - 1;
			imageBlit.srcOffsets[1].x = int32_t(m_Width >> (i - 1));
			imageBlit.srcOffsets[1].y = int32_t(m_Height >> (i - 1));
			imageBlit.srcOffsets[1].z = 1;

			// Destination
			imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBlit.dstSubresource.layerCount = 1;
			imageBlit.dstSubresource.mipLevel = i;
			imageBlit.dstOffsets[1].x = int32_t(m_Width >> i);
			imageBlit.dstOffsets[1].y = int32_t(m_Height >> i);
			imageBlit.dstOffsets[1].z = 1;

			VkImageSubresourceRange mipSubRange = {};
			mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mipSubRange.baseMipLevel = i;
			mipSubRange.levelCount = 1;
			mipSubRange.layerCount = 1;

			// Prepare current mip level as image blit destination
			Utils::InsertImageMemoryBarrier(blitCmd, info.Image,
				0, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				mipSubRange);

			// Blit from previous level
			vkCmdBlitImage(
				blitCmd,
				info.Image,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				info.Image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&imageBlit,
				VK_FILTER_LINEAR);

			// Prepare current mip level as image blit source for next level
			Utils::InsertImageMemoryBarrier(blitCmd, info.Image,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				mipSubRange);
		}

		// After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.layerCount = 1;
		subresourceRange.levelCount = mipLevels;

		Utils::InsertImageMemoryBarrier(blitCmd, info.Image,
			VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			subresourceRange);

		VulkanContext::GetCurrentDevice()->FlushCommandBuffer(blitCmd);

#if 0
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = m_Image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		barrier.subresourceRange = subresourceRange;

		int32_t mipWidth = m_Width;
		int32_t mipHeight = m_Height;

		VkCommandBuffer commandBuffer = VulkanContext::GetCurrentDevice()->GetCommandBuffer(true);

		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		auto mipLevels = GetMipLevelCount();
		for (uint32_t i = 1; i < mipLevels; i++)
		{
			VkImageBlit blit = {};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;

			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mipWidth / 2, mipHeight / 2, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;

			vkCmdBlitImage(commandBuffer,
				m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.subresourceRange.baseMipLevel = i;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			if (mipWidth > 1)
				mipWidth /= 2;
			if (mipHeight > 1)
				mipHeight /= 2;
		}

		// Transition all mips from transfer to shader read
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		VulkanContext::GetCurrentDevice()->FlushCommandBuffer(commandBuffer);
#endif
	}

	//////////////////////////////////////////////////////////////////////////////////
	// TextureCube
	//////////////////////////////////////////////////////////////////////////////////

	VulkanTextureCube::VulkanTextureCube(ImageFormat format, uint32_t width, uint32_t height, const void* data, TextureProperties properties)
		: m_Format(format), m_Width(width), m_Height(height), m_Properties(properties)
	{
		if (data)
		{
			uint32_t size = width * height * 4 * 6; // six layers
			m_LocalStorage = Buffer::Copy(data, size);
		}
		
		Ref<VulkanTextureCube> instance = this;
		Renderer::Submit([instance]() mutable
		{
			instance->Invalidate();
		});
}

	VulkanTextureCube::VulkanTextureCube(const std::string& path, TextureProperties properties)
		: m_Properties(properties)
	{
		HZ_CORE_ASSERT(false, "Not implemented");
	}

	VulkanTextureCube::~VulkanTextureCube()
	{
		VkImageView imageView = m_DescriptorImageInfo.imageView;
		VkSampler sampler = m_DescriptorImageInfo.sampler;
		VkImage image = m_Image;
		VmaAllocation allocation = m_MemoryAlloc;
		Renderer::Submit([imageView, sampler, image, allocation]()
		{
			HZ_CORE_TRACE("Destroying VulkanTextureCube");
			auto vulkanDevice = VulkanContext::GetCurrentDevice()->GetVulkanDevice();
			vkDestroyImageView(vulkanDevice, imageView, nullptr);
			vkDestroySampler(vulkanDevice, sampler, nullptr);

			VulkanAllocator allocator("TextureCube");
			allocator.DestroyImage(image, allocation);
		});
	}

	static void SetImageLayout(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkImageSubresourceRange subresourceRange,
		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
	{
		// Create an image barrier object
		VkImageMemoryBarrier imageMemoryBarrier{};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.oldLayout = oldImageLayout;
		imageMemoryBarrier.newLayout = newImageLayout;
		imageMemoryBarrier.image = image;
		imageMemoryBarrier.subresourceRange = subresourceRange;

		// Source layouts (old)
		// Source access mask controls actions that have to be finished on the old layout
		// before it will be transitioned to the new layout
		switch (oldImageLayout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			// Image layout is undefined (or does not matter)
			// Only valid as initial layout
			// No flags required, listed only for completeness
			imageMemoryBarrier.srcAccessMask = 0;
			break;

		case VK_IMAGE_LAYOUT_PREINITIALIZED:
			// Image is preinitialized
			// Only valid as initial layout for linear images, preserves memory contents
			// Make sure host writes have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			// Image is a color attachment
			// Make sure any writes to the color buffer have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			// Image is a depth/stencil attachment
			// Make sure any writes to the depth/stencil buffer have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			// Image is a transfer source
			// Make sure any reads from the image have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			// Image is a transfer destination
			// Make sure any writes to the image have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			// Image is read by a shader
			// Make sure any shader reads from the image have been finished
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		default:
			// Other source layouts aren't handled (yet)
			break;
		}

		// Target layouts (new)
		// Destination access mask controls the dependency for the new image layout
		switch (newImageLayout)
		{
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			// Image will be used as a transfer destination
			// Make sure any writes to the image have been finished
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			// Image will be used as a transfer source
			// Make sure any reads from the image have been finished
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			// Image will be used as a color attachment
			// Make sure any writes to the color buffer have been finished
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			// Image layout will be used as a depth/stencil attachment
			// Make sure any writes to depth/stencil buffer have been finished
			imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			// Image will be read in a shader (sampler, input attachment)
			// Make sure any writes to the image have been finished
			if (imageMemoryBarrier.srcAccessMask == 0)
			{
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		default:
			// Other source layouts aren't handled (yet)
			break;
		}

		// Put barrier inside setup command buffer
		vkCmdPipelineBarrier(
			cmdbuffer,
			srcStageMask,
			dstStageMask,
			0,
			0, nullptr,
			0, nullptr,
			1, &imageMemoryBarrier);
	}

	static void SetImageLayout(
		VkCommandBuffer cmdbuffer,
		VkImage image,
		VkImageAspectFlags aspectMask,
		VkImageLayout oldImageLayout,
		VkImageLayout newImageLayout,
		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
	{
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = aspectMask;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		SetImageLayout(cmdbuffer, image, oldImageLayout, newImageLayout, subresourceRange, srcStageMask, dstStageMask);
	}

	void VulkanTextureCube::Invalidate()
	{
		auto device = VulkanContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();

		VkFormat format = Utils::VulkanImageFormat(m_Format);
		uint32_t mipCount = GetMipLevelCount();

		VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

		VulkanAllocator allocator("TextureCube");

		// Create optimal tiled target image on the device
		VkImageCreateInfo imageCreateInfo{};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipCount;
		imageCreateInfo.arrayLayers = 6;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.extent = { m_Width, m_Height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
		imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		m_MemoryAlloc = allocator.AllocateImage(imageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY, m_Image);

		m_DescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		// Copy data if present
		if (m_LocalStorage)
		{
			// Create staging buffer
			VkBufferCreateInfo bufferCreateInfo{};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.size = m_LocalStorage.Size;
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VkBuffer stagingBuffer;
			VmaAllocation stagingBufferAllocation = allocator.AllocateBuffer(bufferCreateInfo, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer);

			// Copy data to staging buffer
			uint8_t* destData = allocator.MapMemory<uint8_t>(stagingBufferAllocation);
			memcpy(destData, m_LocalStorage.Data, m_LocalStorage.Size);
			allocator.UnmapMemory(stagingBufferAllocation);

			VkCommandBuffer copyCmd = device->GetCommandBuffer(true);

			// Image memory barriers for the texture image

			// The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
			VkImageSubresourceRange subresourceRange = {};
			// Image only contains color data
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			// Start at first mip level
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 6;

			// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
			VkImageMemoryBarrier imageMemoryBarrier{};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.image = m_Image;
			imageMemoryBarrier.subresourceRange = subresourceRange;
			imageMemoryBarrier.srcAccessMask = 0;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

			// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition 
			// Source pipeline stage is host write/read exection (VK_PIPELINE_STAGE_HOST_BIT)
			// Destination pipeline stage is copy command exection (VK_PIPELINE_STAGE_TRANSFER_BIT)
			vkCmdPipelineBarrier(
				copyCmd,
				VK_PIPELINE_STAGE_HOST_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &imageMemoryBarrier);

			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 6;
			bufferCopyRegion.imageExtent.width = m_Width;
			bufferCopyRegion.imageExtent.height = m_Height;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = 0;

			// Copy mip levels from staging buffer
			vkCmdCopyBufferToImage(
				copyCmd,
				stagingBuffer,
				m_Image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&bufferCopyRegion);

			Utils::InsertImageMemoryBarrier(copyCmd, m_Image,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				subresourceRange);

			device->FlushCommandBuffer(copyCmd);

			allocator.DestroyBuffer(stagingBuffer, stagingBufferAllocation);
		}

		VkCommandBuffer layoutCmd = device->GetCommandBuffer(true);

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipCount;
		subresourceRange.layerCount = 6;

		SetImageLayout(
			layoutCmd, m_Image,
			VK_IMAGE_LAYOUT_UNDEFINED,
			m_DescriptorImageInfo.imageLayout,
			subresourceRange);

		device->FlushCommandBuffer(layoutCmd);

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// CREATE TEXTURE SAMPLER
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Create a texture sampler
		VkSamplerCreateInfo sampler{};
		sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sampler.maxAnisotropy = 1.0f;
		sampler.magFilter = Utils::VulkanSamplerFilter(m_Properties.SamplerFilter);
		sampler.minFilter = Utils::VulkanSamplerFilter(m_Properties.SamplerFilter);
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU =  Utils::VulkanSamplerWrap(m_Properties.SamplerWrap);
		sampler.addressModeV =  Utils::VulkanSamplerWrap(m_Properties.SamplerWrap);
		sampler.addressModeW =  Utils::VulkanSamplerWrap(m_Properties.SamplerWrap);
		sampler.mipLodBias = 0.0f;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;
		// Set max level-of-detail to mip level count of the texture
		sampler.maxLod = (float)mipCount;
		// Enable anisotropic filtering
		// This feature is optional, so we must check if it's supported on the device

		sampler.maxAnisotropy = 1.0;
		sampler.anisotropyEnable = VK_FALSE;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(vulkanDevice, &sampler, nullptr, &m_DescriptorImageInfo.sampler));

		// Create image view
		// Textures are not directly accessed by the shaders and
		// are abstracted by image views containing additional
		// information and sub resource ranges
		VkImageViewCreateInfo view{};
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		view.format = format;
		view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
		// It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 6;
		view.subresourceRange.levelCount = mipCount;
		view.image = m_Image;
		VK_CHECK_RESULT(vkCreateImageView(vulkanDevice, &view, nullptr, &m_DescriptorImageInfo.imageView));
	}

	uint32_t VulkanTextureCube::GetMipLevelCount() const
	{
		return Utils::CalculateMipCount(m_Width, m_Height);
	}

	VkImageView VulkanTextureCube::CreateImageViewSingleMip(uint32_t mip)
	{
		// TODO: assert to check mip count

		auto device = VulkanContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();

		VkFormat format = Utils::VulkanImageFormat(m_Format);

		VkImageViewCreateInfo view{};
		view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		view.format = format;
		view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = mip;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 6;
		view.subresourceRange.levelCount = 1;
		view.image = m_Image;

		VkImageView result;
		VK_CHECK_RESULT(vkCreateImageView(vulkanDevice, &view, nullptr, &result));
		return result;
	}

	void VulkanTextureCube::GenerateMips(bool readonly)
	{
		auto device = VulkanContext::GetCurrentDevice();
		auto vulkanDevice = device->GetVulkanDevice();

		VkCommandBuffer blitCmd = VulkanContext::GetCurrentDevice()->GetCommandBuffer(true);

		uint32_t mipLevels = GetMipLevelCount();
		for (uint32_t face = 0; face < 6; face++)
		{
			VkImageSubresourceRange mipSubRange = {};
			mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			mipSubRange.baseMipLevel = 0;
			mipSubRange.baseArrayLayer = face;
			mipSubRange.levelCount = 1;
			mipSubRange.layerCount = 1;

			// Prepare current mip level as image blit destination
			Utils::InsertImageMemoryBarrier(blitCmd, m_Image,
				0, VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				mipSubRange);
		}
		
		for (int32_t i = 1; i < mipLevels; i++)
		{
			for (uint32_t face = 0; face < 6; face++)
			{
				VkImageBlit imageBlit{};

				// Source
				imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.srcSubresource.layerCount = 1;
				imageBlit.srcSubresource.mipLevel = i - 1;
				imageBlit.srcSubresource.baseArrayLayer = face;
				imageBlit.srcOffsets[1].x = int32_t(m_Width >> (i - 1));
				imageBlit.srcOffsets[1].y = int32_t(m_Height >> (i - 1));
				imageBlit.srcOffsets[1].z = 1;

				// Destination
				imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.dstSubresource.layerCount = 1;
				imageBlit.dstSubresource.mipLevel = i;
				imageBlit.dstSubresource.baseArrayLayer = face;
				imageBlit.dstOffsets[1].x = int32_t(m_Width >> i);
				imageBlit.dstOffsets[1].y = int32_t(m_Height >> i);
				imageBlit.dstOffsets[1].z = 1;

				VkImageSubresourceRange mipSubRange = {};
				mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				mipSubRange.baseMipLevel = i;
				mipSubRange.baseArrayLayer = face;
				mipSubRange.levelCount = 1;
				mipSubRange.layerCount = 1;

				// Prepare current mip level as image blit destination
				Utils::InsertImageMemoryBarrier(blitCmd, m_Image,
					0, VK_ACCESS_TRANSFER_WRITE_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					mipSubRange);

				// Blit from previous level
				vkCmdBlitImage(
					blitCmd,
					m_Image,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					m_Image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1,
					&imageBlit,
					VK_FILTER_LINEAR);

				// Prepare current mip level as image blit source for next level
				Utils::InsertImageMemoryBarrier(blitCmd, m_Image,
					VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					mipSubRange);
			}
		}

		// After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.layerCount = 6;
		subresourceRange.levelCount = mipLevels;

		Utils::InsertImageMemoryBarrier(blitCmd, m_Image,
			VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readonly ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			subresourceRange);

		VulkanContext::GetCurrentDevice()->FlushCommandBuffer(blitCmd);

		m_MipsGenerated = true;

		m_DescriptorImageInfo.imageLayout = readonly ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;

	}

}