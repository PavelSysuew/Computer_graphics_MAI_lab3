#include "gpu_resources.hpp"

#include <cstring>

#include <algorithm>
#include <iostream>
#include <vector>

#include <stb_image.h>

namespace gpu {

namespace {

using graphics::internal::context;

// Отдельный пул команд для загрузок. TRANSIENT — подсказка драйверу,
// что командные буферы из него живут недолго
VkCommandPool vk_upload_command_pool = VK_NULL_HANDLE;
VkCommandBuffer vk_upload_command_buffer = VK_NULL_HANDLE;
// Забор (fence): процессор ждёт на нём, пока видеокарта выполнит копирования
VkFence vk_upload_fence = VK_NULL_HANDLE;

// Staging-буферы текущей пачки: их можно удалить только после того,
// как видеокарта закончит из них копировать
std::vector<Buffer> pending_staging_buffers;
bool upload_recording = false;

// Создаёт staging-буфер в RAM и копирует в него данные
bool createStagingBuffer(const void* data, VkDeviceSize size, Buffer& result) {
	// TRANSFER_SRC — буфер будет источником команды копирования
	if (!createHostBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, result)) {
		return false;
	}
	std::memcpy(result.mapped, data, size);
	return true;
}

// Барьер для изображения: переводит mip-уровни [base_mip, base_mip + mip_count)
// из одной раскладки (layout) в другую и упорядочивает доступ к памяти.
// src_* — что должно завершиться ДО барьера, dst_* — что ждёт ПОСЛЕ него
void transitionImage(VkCommandBuffer cb, VkImage image,
                     uint32_t base_mip, uint32_t mip_count,
                     VkImageLayout old_layout, VkImageLayout new_layout,
                     VkAccessFlags src_access, VkAccessFlags dst_access,
                     VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
	const VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = src_access,
		.dstAccessMask = dst_access,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		// Владелец-очередь не меняется
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = base_mip,
			.levelCount = mip_count,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// Можно ли строить mip-уровни на видеокарте: формат должен поддерживать
// копирование с масштабированием (blit) и линейную фильтрацию
bool supportsLinearBlit(VkFormat format) {
	VkFormatProperties properties = {};
	vkGetPhysicalDeviceFormatProperties(context.physical_device, format, &properties);

	const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
	                                      VK_FORMAT_FEATURE_BLIT_DST_BIT |
	                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	return (properties.optimalTilingFeatures & required) == required;
}

} // namespace

bool initialize() {
	const VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};

	if (vkCreateCommandPool(context.device, &pool_info, nullptr, &vk_upload_command_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create upload command pool\n";
		return false;
	}

	const VkCommandBufferAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vk_upload_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	if (vkAllocateCommandBuffers(context.device, &allocate_info, &vk_upload_command_buffer) != VK_SUCCESS) {
		std::cerr << "Failed to allocate upload command buffer\n";
		return false;
	}

	const VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	if (vkCreateFence(context.device, &fence_info, nullptr, &vk_upload_fence) != VK_SUCCESS) {
		std::cerr << "Failed to create upload fence\n";
		return false;
	}

	return true;
}

void shutdown() {
	for (Buffer& buffer : pending_staging_buffers) {
		destroyBuffer(buffer);
	}
	pending_staging_buffers.clear();

	vkDestroyFence(context.device, vk_upload_fence, nullptr);
	// Командный буфер освобождается вместе с пулом
	vkDestroyCommandPool(context.device, vk_upload_command_pool, nullptr);
	vk_upload_fence = VK_NULL_HANDLE;
	vk_upload_command_pool = VK_NULL_HANDLE;
}

bool createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& result) {
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		// MAPPED — сразу получить адрес для записи с процессора,
		// HOST_ACCESS_SEQUENTIAL_WRITE — процессор пишет в память последовательно
		// (VMA выберет память, видимую процессору)
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
		         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo info = {};
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
	                    &result.buffer, &result.allocation, &info) != VK_SUCCESS) {
		std::cerr << "Failed to create host-visible buffer\n";
		return false;
	}

	result.mapped = info.pMappedData;
	return true;
}

void destroyBuffer(Buffer& buffer) {
	if (buffer.buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.allocator, buffer.buffer, buffer.allocation);
	}
	buffer = {};
}

bool beginUploads() {
	vkResetCommandBuffer(vk_upload_command_buffer, 0);

	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	if (vkBeginCommandBuffer(vk_upload_command_buffer, &begin_info) != VK_SUCCESS) {
		std::cerr << "Failed to begin upload command buffer\n";
		return false;
	}

	upload_recording = true;
	return true;
}

bool endUploads() {
	VkCommandBuffer cb = vk_upload_command_buffer;

	// Общий барьер для буферов: копирования в вершинные и индексные буферы должны
	// завершиться и стать видимыми до того, как их начнёт читать сборщик вершин
	const VkMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
	};
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	                     0, 1, &barrier, 0, nullptr, 0, nullptr);

	upload_recording = false;

	if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
		std::cerr << "Failed to end upload command buffer\n";
		return false;
	}

	const VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cb,
	};

	if (vkQueueSubmit(context.graphics_queue, 1, &submit, vk_upload_fence) != VK_SUCCESS) {
		std::cerr << "Failed to submit uploads\n";
		return false;
	}

	// Ждём, пока видеокарта всё скопирует: только после этого staging-буферы можно удалить
	vkWaitForFences(context.device, 1, &vk_upload_fence, VK_TRUE, UINT64_MAX);
	vkResetFences(context.device, 1, &vk_upload_fence);

	for (Buffer& buffer : pending_staging_buffers) {
		destroyBuffer(buffer);
	}
	pending_staging_buffers.clear();

	return true;
}

bool createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& result) {
	if (!upload_recording) {
		std::cerr << "createDeviceBuffer() must be called between beginUploads() and endUploads()\n";
		return false;
	}

	Buffer staging;
	if (!createStagingBuffer(data, size, staging)) {
		return false;
	}
	pending_staging_buffers.push_back(staging);

	// Буфер-получатель в VRAM: TRANSFER_DST — в него будут копировать
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	// Без флага MAPPED: процессору доступ не нужен. AUTO_PREFER_DEVICE — VMA выберет
	// память с флагом DEVICE_LOCAL, то есть собственную память видеокарты
	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
	                    &result.buffer, &result.allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create device-local buffer\n";
		return false;
	}
	result.mapped = nullptr;

	// Команда копирования staging -> VRAM выполнится видеокартой в endUploads()
	const VkBufferCopy region = {
		.srcOffset = 0,
		.dstOffset = 0,
		.size = size,
	};
	vkCmdCopyBuffer(vk_upload_command_buffer, staging.buffer, result.buffer, 1, &region);

	return true;
}

bool createTexture(const uint8_t* rgba, uint32_t width, uint32_t height, VkFormat format,
                   bool generate_mipmaps, Texture& result) {
	if (!upload_recording) {
		std::cerr << "createTexture() must be called between beginUploads() and endUploads()\n";
		return false;
	}

	VkCommandBuffer cb = vk_upload_command_buffer;

	// Число mip-уровней: делим больший размер пополам, пока не дойдём до 1.
	// Для 512 x 512 это 10 уровней: 512, 256, ..., 2, 1
	uint32_t mip_levels = 1;
	if (generate_mipmaps && supportsLinearBlit(format)) {
		uint32_t size = std::max(width, height);
		while (size > 1) {
			size /= 2;
			++mip_levels;
		}
	}

	// 1. Пиксели — в staging-буфер
	const VkDeviceSize image_size = VkDeviceSize(width) * height * 4;
	Buffer staging;
	if (!createStagingBuffer(rgba, image_size, staging)) {
		return false;
	}
	pending_staging_buffers.push_back(staging);

	// 2. Изображение в VRAM
	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, 1 },
		.mipLevels = mip_levels,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		// OPTIMAL — пиксели лежат в порядке, удобном видеокарте (не построчно)
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		// TRANSFER_DST — в него копируем, TRANSFER_SRC — из уровней копируем при построении mip,
		// SAMPLED — шейдер читает его через сэмплер
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		         VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	if (vmaCreateImage(context.allocator, &image_info, &allocation_info,
	                   &result.image, &result.allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create texture image\n";
		return false;
	}

	result.width = width;
	result.height = height;
	result.mip_levels = mip_levels;

	// 3. Все уровни: UNDEFINED -> TRANSFER_DST_OPTIMAL (раскладка для приёма копирования)
	transitionImage(cb, result.image, 0, mip_levels,
	                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                0, VK_ACCESS_TRANSFER_WRITE_BIT,
	                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	// 4. Копируем пиксели из staging-буфера в нулевой (полноразмерный) уровень
	const VkBufferImageCopy region = {
		.bufferOffset = 0,
		// 0 — строки в буфере идут плотно, без выравнивания
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
		.imageOffset = { 0, 0, 0 },
		.imageExtent = { width, height, 1 },
	};
	vkCmdCopyBufferToImage(cb, staging.buffer, result.image,
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	// 5. Mip-уровни: каждый следующий — уменьшенная вдвое копия предыдущего (vkCmdBlitImage
	// масштабирует с линейной фильтрацией). Mip-уровни убирают мерцание и «муар» на далёких
	// и наклонных поверхностях: сэмплер берёт уровень, где тексель примерно равен пикселю
	int32_t mip_width = int32_t(width);
	int32_t mip_height = int32_t(height);

	for (uint32_t level = 1; level < mip_levels; ++level) {
		// Предыдущий уровень записан — переводим его в раскладку источника копирования
		transitionImage(cb, result.image, level - 1, 1,
		                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
		                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		const int32_t next_width = std::max(mip_width / 2, 1);
		const int32_t next_height = std::max(mip_height / 2, 1);

		const VkImageBlit blit = {
			.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1 },
			.srcOffsets = { { 0, 0, 0 }, { mip_width, mip_height, 1 } },
			.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
			.dstOffsets = { { 0, 0, 0 }, { next_width, next_height, 1 } },
		};

		vkCmdBlitImage(cb, result.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		               result.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		               1, &blit, VK_FILTER_LINEAR);

		// Предыдущий уровень больше не нужен для копирования — готовим его для шейдера
		transitionImage(cb, result.image, level - 1, 1,
		                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
		                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		mip_width = next_width;
		mip_height = next_height;
	}

	// 6. Последний уровень (или единственный, если mip-уровней нет) — тоже для шейдера
	transitionImage(cb, result.image, mip_levels - 1, 1,
	                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
	                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	// 7. Вид на изображение: все mip-уровни, обычная 2D-текстура
	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = result.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = mip_levels,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	if (vkCreateImageView(context.device, &view_info, nullptr, &result.view) != VK_SUCCESS) {
		std::cerr << "Failed to create texture image view\n";
		return false;
	}

	return true;
}

bool loadTexture(const char* path, VkFormat format, Texture& result) {
	int width = 0;
	int height = 0;
	int channels = 0;

	// Последний аргумент — сколько каналов хотим получить. 4 (RGBA) — всегда,
	// даже если в файле их 3 или 1: Vulkan-формат R8G8B8A8 ждёт ровно 4 байта на пиксель.
	// stb_image сама определяет формат файла по его содержимому
	stbi_uc* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
	if (pixels == nullptr) {
		std::cerr << "Failed to load image " << path << ": " << stbi_failure_reason() << '\n';
		return false;
	}

	const bool ok = createTexture(pixels, uint32_t(width), uint32_t(height), format, true, result);

	// Пиксели уже скопированы в staging-буфер, память stb_image больше не нужна
	stbi_image_free(pixels);

	if (ok) {
		std::cout << "Loaded texture " << path << " (" << width << "x" << height
		          << ", " << result.mip_levels << " mip levels)\n";
	}
	return ok;
}

void destroyTexture(Texture& texture) {
	if (texture.view != VK_NULL_HANDLE) {
		vkDestroyImageView(context.device, texture.view, nullptr);
	}
	if (texture.image != VK_NULL_HANDLE) {
		vmaDestroyImage(context.allocator, texture.image, texture.allocation);
	}
	texture = {};
}

} // namespace gpu
