#pragma once

#include <cstdint>

#include "graphics_internal.hpp"

// Ресурсы видеокарты: буферы и текстуры, а также их загрузка в видеопамять (VRAM)
// через промежуточные staging-буферы (лабораторная №3)
namespace gpu {

// Буфер вместе с выделенной под него памятью
struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;
	// Адрес, по которому процессор может писать в буфер.
	// У буферов в VRAM его нет (nullptr): процессор их напрямую не видит
	void* mapped = nullptr;
};

// Текстура: изображение в VRAM и «вид» на него (image view), через который его читает шейдер
struct Texture {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t width = 0;
	uint32_t height = 0;
	// Число mip-уровней: уменьшенных вдвое копий изображения, вплоть до 1 x 1
	uint32_t mip_levels = 0;
};

// Создаёт пул команд и забор (fence) для загрузок. Вызывается один раз при запуске
bool initialize();
void shutdown();

// Буфер в памяти, видимой процессору (RAM или общая память).
// Для данных, которые меняются каждый кадр: uniform-, storage-буферы, линии конусов
bool createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& result);
void destroyBuffer(Buffer& buffer);

// --- Пакетная загрузка в VRAM ---
// Все функции ниже только записывают команды копирования в общий командный буфер.
// Сами копирования выполняются в endUploads(): одна отправка в очередь на всю пачку,
// после неё staging-буферы удаляются.
//
//   gpu::beginUploads();
//   gpu::createDeviceBuffer(...);  gpu::loadTexture(...);  ...
//   gpu::endUploads();
bool beginUploads();
bool endUploads();

// Буфер в VRAM с начальными данными data (вершины, индексы).
// Данные сначала копируются в staging-буфер в RAM, затем видеокарта сама копирует их в VRAM
bool createDeviceBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, Buffer& result);

// Текстура из пикселей RGBA (по 4 байта на пиксель, строки сверху вниз).
// format — VK_FORMAT_R8G8B8A8_SRGB для цвета (альбедо, свечение) или
// VK_FORMAT_R8G8B8A8_UNORM для данных (карта блеска).
// generate_mipmaps — построить mip-уровни на видеокарте командой vkCmdBlitImage
bool createTexture(const uint8_t* rgba, uint32_t width, uint32_t height, VkFormat format,
                   bool generate_mipmaps, Texture& result);

// Загружает изображение с диска (PNG, JPEG, BMP, TGA, GIF) через stb_image и создаёт текстуру
bool loadTexture(const char* path, VkFormat format, Texture& result);

void destroyTexture(Texture& texture);

} // namespace gpu
