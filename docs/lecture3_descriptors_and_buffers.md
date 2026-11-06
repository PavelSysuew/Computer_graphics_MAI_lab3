# Лекция 3. Descriptors and buffers

## Вершинный и индексный буферы
- **Вершинный буфер** — массив данных вершин модели.
- **Индексный буфер** — целые индексы вершин, чтобы не дублировать общие вершины; количество кратно 3 для треугольников. Рисование: `vkCmdDrawIndexed`.
- **Вершинные атрибуты**: позиция, цвет, текстурные координаты, нормаль/касательная/бинормаль, индексы и веса костей и т.д.
- Размещение: отдельный буфер на атрибут или **interleaved** (массив структур `Vertex`) — в курсе используется второй.

## Интерполяция и барицентрические координаты
- Выходы вершинного шейдера интерполируются по треугольнику для каждого фрагмента.
- P = λ1·A + λ2·B + λ3·C, λ1+λ2+λ3 = 1; λi = площадь подтреугольника напротив вершины / площадь всего треугольника. Для отрезка — обычная линейная интерполяция.

## Описание вершин в конвейере (vertex input state)
```cpp
VkVertexInputBindingDescription bindings[] = {{ .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }};
VkVertexInputAttributeDescription attrs[] = {
  { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position) },
  { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) },
};
```
- Binding — из какого буфера и с каким шагом (stride) читать; attribute — тип, буфер, `location` в шейдере, смещение.
- Шейдеры: `layout(location = N) in/out`; выходы VS должны совпадать по типу и location со входами FS.
```glsl
// vertex
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;
layout(location = 0) out vec3 out_color;
layout(std140, set = 0, binding = 0) uniform GlobalUniforms { mat4 matrix; } global_uniforms;
void main() { gl_Position = global_uniforms.matrix * vec4(in_position, 1); out_color = in_color; }
// fragment
layout(location = 0) in vec3 in_color;
layout(location = 0) out vec4 out_color;
void main() { out_color = vec4(in_color, 1); }
```

## Загрузка данных RAM → VRAM
Шаги: создать `VkBuffer` → выделить память (`VkDeviceMemory`) → отобразить (map) → `memcpy`.
**VMA (Vulkan Memory Allocator)** берёт на себя выбор типа памяти (GPU-only, host-visible, ReBAR), выделение, привязку, фрагментацию и утечки.
```cpp
VkBufferCreateInfo bi{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(vertices),
                       .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
VmaAllocationCreateInfo ai{ .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                            .usage = VMA_MEMORY_USAGE_AUTO };
vmaCreateBuffer(context.allocator, &bi, &ai, &vk_vertex_buffer, &vk_vertex_buffer_allocation, nullptr);
vmaMapMemory(context.allocator, vk_vertex_buffer_allocation, (void**)&ptr);
memcpy(ptr, vertices, sizeof(vertices));
```
Индексный буфер — аналогично, `usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT`, индексы `uint32_t`.

## Uniform-буферы
- Данные, постоянные в течение кадра: матрицы, время, параметры источников света.
- Размер выравнивается до 16 байт: `(sizeof(GlobalUniforms) + 0xf) & ~0xf`; `usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT`; раскладка `std140`.
- Обновляются каждый кадр в функции `update` стартового кода (пересчёт матрицы → запись в отображённую память).

## Дескрипторы (цепочка объектов)
1. **Дескриптор** — описание внешнего ресурса: тип, binding, количество, шейдерные стадии.
2. `VkDescriptorSetLayoutBinding` { binding 0, `UNIFORM_BUFFER`, count 1, stage VERTEX|FRAGMENT } → `VkDescriptorSetLayout` (`vkCreateDescriptorSetLayout`).
3. `VkPipelineLayout` теперь содержит `setLayoutCount = 1, pSetLayouts = &layout`.
4. `VkDescriptorPool` — аллокатор наборов: `maxSets` и `VkDescriptorPoolSize { type, descriptorCount }`; на каждый тип нужно N·S дескрипторов (N на набор × S наборов).
5. `vkAllocateDescriptorSets` — выделить `VkDescriptorSet` из пула по layout.
6. Связать буфер с набором: `VkDescriptorBufferInfo { buffer, offset 0, range = sizeof(GlobalUniforms) }` → `VkWriteDescriptorSet { dstSet, dstBinding 0, descriptorCount 1, type UNIFORM_BUFFER, pBufferInfo }` → `vkUpdateDescriptorSets`.
7. В командах кадра:
```cpp
VkDeviceSize off = 0;
vkCmdBindVertexBuffers(cb, 0, 1, &vk_vertex_buffer, &off);
vkCmdBindIndexBuffer(cb, vk_index_buffer, 0, VK_INDEX_TYPE_UINT32);
vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout, 0, 1, &vk_descriptor_set, 0, nullptr);
vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);
vkCmdDrawIndexed(cb, index_count, 1, 0, 0, 0);
```
Соответствие: `set = 0, binding = 0` в шейдере ↔ индекс набора в `vkCmdBindDescriptorSets` и `dstBinding`/`binding` в layout.
