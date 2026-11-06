// Единственное место, где компилируется реализация stb_image.
// Во всех остальных файлах заголовок подключается без этого макроса и даёт только объявления
#define STB_IMAGE_IMPLEMENTATION
// Нам нужны только распространённые форматы, остальные декодеры не компилируем
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#include <stb_image.h>
