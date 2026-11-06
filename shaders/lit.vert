#version 450 core

// Входы: атрибуты вершины из вершинного буфера (номера совпадают с location в VkVertexInputAttributeDescription)
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
// Текстурные координаты (u, v) вершины
layout(location = 2) in vec2 in_uv;

// Набор 0 — общие данные сцены (один на всю сцену, лекция 4).
// Раскладка std140: vec3 выравнивается на 16 байт, поэтому после каждого vec3
// стоит float-заглушка (или полезный float), как и в структуре SceneUniforms на C++
layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	// Произведение матриц проекции и вида: P * V
	mat4 view_projection;
	// Позиция камеры в мире (нужна для вектора v «на глаз»)
	vec3 camera_position;
	// Направление, КУДА светит направленный источник (как лучи Солнца)
	vec3 directional_light_direction;
	// Цвет направленного света, уже умноженный на интенсивность
	vec3 directional_light_color;
	// Рассеянный свет: цвет, умноженный на интенсивность
	vec3 ambient_light;
	// Сколько элементов массивов из storage-буферов реально используется
	uint point_light_count;
	uint spot_light_count;
} scene_uniforms;

// Набор 1, binding 0 — ДИНАМИЧЕСКИЙ uniform-буфер (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC).
// На C++ это один буфер с массивом структур — по одной на каждый объект.
// Шейдер видит только одну структуру: ту, что лежит по смещению, переданному
// в vkCmdBindDescriptorSets (pDynamicOffsets). Описание блока — как у обычного uniform-буфера
layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	// Матрица модели M: ставит объект в мир
	mat4 model;
	// Матрица для нормалей (M^-1)^T, используется только блок 3x3
	mat4 normal_matrix;
	// Множитель цвета текстуры альбедо (оттенок)
	vec3 albedo_factor;
	// Коэффициент блеска α
	float shininess;
	// Множитель для specular-текстуры (цвет и сила блика)
	vec3 specular_factor;
	// Множитель для emissive-текстуры (яркость свечения)
	float emissive_strength;
} model_uniforms;

// Позиция и нормаль в мировых координатах — освещение считаем в мире
layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_world_normal;
// Текстурные координаты просто передаём дальше: растеризатор интерполирует их
// между вершинами треугольника (с учётом перспективы)
layout(location = 2) out vec2 out_uv;

void main() {
	// Точка в мировых координатах: M * p
	vec4 world_position = model_uniforms.model * vec4(in_position, 1.0);

	// p' = P * V * M * p (лекция 4, MVP)
	gl_Position = scene_uniforms.view_projection * world_position;

	out_world_position = world_position.xyz;

	// Нормаль — направление, переноса у неё нет, поэтому берём только mat3.
	// Нормировать будем во фрагментном шейдере: после интерполяции длина всё равно станет меньше 1
	out_world_normal = mat3(model_uniforms.normal_matrix) * in_normal;

	out_uv = in_uv;
}
