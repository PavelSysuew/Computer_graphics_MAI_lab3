#version 450 core

// Общий фрагментный шейдер для маркеров и линий: просто выводит цвет без освещения
layout(location = 0) in vec3 in_color;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = vec4(in_color, 1.0);
}
