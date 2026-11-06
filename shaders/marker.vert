#version 450 core

// Вершинный шейдер для маркеров источников света (доп. задание 4).
// Используем ту же сетку сферы, что и обычные объекты, но нормаль не нужна:
// маркеры не освещаются, у них постоянный цвет
layout(location = 0) in vec3 in_position;

// Из набора 0 нужна только матрица P * V. Остальные поля блока не объявляем:
// шейдер может описать только начало блока, раскладка от этого не меняется
layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
} scene_uniforms;

// Push-константы — маленький кусочек данных, который кладётся прямо в командный буфер
// командой vkCmdPushConstants. Удобно для данных, которые меняются на каждый вызов отрисовки:
// не нужно заводить отдельный буфер и набор дескрипторов на каждый источник света
layout(push_constant) uniform MarkerConstants {
	// Матрица модели маркера: перенос в позицию источника и масштаб
	mat4 model;
	// Цвет источника
	vec4 color;
} marker;

layout(location = 0) out vec3 out_color;

void main() {
	gl_Position = scene_uniforms.view_projection * marker.model * vec4(in_position, 1.0);
	out_color = marker.color.rgb;
}
