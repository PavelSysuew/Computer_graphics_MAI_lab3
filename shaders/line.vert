#version 450 core

// Вершинный шейдер для линий конуса прожектора (доп. задание 4).
// Вершины линий уже посчитаны на процессоре сразу в мировых координатах,
// поэтому матрица модели не нужна
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
} scene_uniforms;

layout(location = 0) out vec3 out_color;

void main() {
	gl_Position = scene_uniforms.view_projection * vec4(in_position, 1.0);
	out_color = in_color;
}
