# Лекция 2. Graphics pipeline (Vulkan)

## Основные объекты Vulkan
- `VkInstance` — точка входа в API.
- `VkPhysicalDevice` — конкретный GPU и его фичи.
- `VkDevice` — логическое устройство, менеджер ресурсов.
- `VkCommandBuffer` — список команд для GPU («нарисуй 3 вершины…»).

## Соглашения API
- Функции `vkCreate*`: 1-й аргумент `VkDevice`; затем (опц.) количество; указатель на `Vk*CreateInfo`; `VkAllocationCallbacks*` (у нас `nullptr`); последний — адрес результата. Возвращают `VkResult` → проверять `== VK_SUCCESS`.
- Почти все `*Info` требуют поле `.sType` (тег типа) и имеют `.pNext` — цепочка структур-расширений (односвязный список).

## Стадии графического конвейера (`VkGraphicsPipelineCreateInfo` → `VkPipeline`)
1. **Shader stages** — `VkPipelineShaderStageCreateInfo` для вершинного (`VK_SHADER_STAGE_VERTEX_BIT`) и фрагментного (`VK_SHADER_STAGE_FRAGMENT_BIT`) шейдеров, `.module = VkShaderModule`, `.pName = "main"`.
   Шейдеры пишутся на GLSL и компилируются в **SPIR-V** (glslc/glslangValidator), затем грузятся в `VkShaderModule`.
2. **Vertex input** (`VkPipelineVertexInputStateCreateInfo`) — откуда брать вершинные данные. Обязательное поле; в этой лекции пустое (вершины захардкожены в шейдере).
3. **Input assembly** (`VkPipelineInputAssemblyStateCreateInfo`) — топология: `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` (также point/line list/strip, triangle strip/fan).
4. **Viewport/scissor** (`VkPipelineViewportStateCreateInfo`) — count = 1; сами значения задаются динамически.
   Viewport — куда рисовать (отображение NDC → пиксели), scissor — что оставить (остальное отрезается).
5. **Rasterization** — `polygonMode = FILL`, `cullMode = BACK_BIT`, `frontFace = CLOCKWISE`, `lineWidth = 1.0f`.
6. **Multisample** — `rasterizationSamples = VK_SAMPLE_COUNT_1_BIT` (MSAA: несколько выборок на пиксель для сглаживания краёв).
7. **Depth/stencil** — `depthTestEnable = true`, `depthWriteEnable = true`, `depthCompareOp = LESS`.
8. **Color blend** — на каждое цветовое вложение `VkPipelineColorBlendAttachmentState` с `colorWriteMask = R|G|B|A`; смешивание (прозрачность) пока выключено.
9. **Dynamic state** — `VK_DYNAMIC_STATE_VIEWPORT`, `VK_DYNAMIC_STATE_SCISSOR`: задаются при записи команд (`vkCmdSetViewport/Scissor`).
10. **Pipeline layout** (`VkPipelineLayout`) — какие внешние ресурсы (дескрипторы, push constants) видят шейдеры; пока пустой.

## Ключевые понятия
- **Вершинный шейдер** — выполняется на каждую вершину, пишет `gl_Position` (однородные координаты clip space). Пример: массив из 3 позиций, индексация `gl_VertexIndex`.
- **Фрагментный шейдер** — на каждый фрагмент (пиксель), `layout(location = 0) out vec4 out_color;` (RGBA).
- **Порядок обхода (winding)** — по знаку векторного произведения рёбер треугольника определяется, лицевая ли грань. **Back-face culling** отбрасывает отвёрнутые грани — нужно договориться о порядке (CW/CCW) в движке.
- **Z-buffer / depth test** — буфер глубины хранит глубину каждого пикселя; новый фрагмент рисуется, если его глубина меньше текущей, и глубина перезаписывается. Делает результат независимым от порядка рисования.
- **Swapchain (цепочка кадров)** — набор изображений, которые по очереди показываются на экране (пока одно показывается, в другое рисуем).
- **Render pass** (`VkRenderPass`) — описывает вложения (цвет, глубина), операции load/store и зависимости между подпроходами для синхронизации. **Framebuffer** — конкретные изображения для render pass.

## Запись команд кадра
```cpp
vkResetCommandBuffer(cb, 0);
vkBeginCommandBuffer(cb, &begin /* ONE_TIME_SUBMIT */);
VkClearValue clear[] = { {.color={{0.1f,0.1f,0.1f,1.0f}}}, {.depthStencil={1.0f, 0}} }; // глубина 1 = дальше всего
vkCmdBeginRenderPass(cb, &rp_begin /* renderPass, framebuffer, renderArea = swapchain_extent, clear */, VK_SUBPASS_CONTENTS_INLINE);
vkCmdSetViewport(cb, 0, 1, &viewport);   // {0,0,w,h, minDepth 0, maxDepth 1}
vkCmdSetScissor(cb, 0, 1, &scissor);     // {.extent = swapchain_extent}
vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
vkCmdDraw(cb, 3, 1, 0, 0);               // 3 вершины, 1 экземпляр
vkCmdEndRenderPass(cb);
vkEndCommandBuffer(cb);
```
