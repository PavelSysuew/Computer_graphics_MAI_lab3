#version 450 core

layout(location = 0) in vec3 in_color;

// Выход фрагментного фейдера
layout(location = 0) out vec4 out_color;

// Выполняется для каждого пикселя внутри треугольника
void main() {
	out_color = vec4(in_color, 1.0);
}
