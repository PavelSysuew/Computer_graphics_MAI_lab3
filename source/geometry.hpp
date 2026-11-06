#pragma once

#include <cstdint>
#include <vector>

#include "linear_math.hpp"

// Одна вершина: позиция, нормаль и текстурные координаты
// (interleaved — атрибуты лежат подряд, лекция 3).
// Нормаль — единичный вектор, перпендикулярный поверхности в этой точке, нужна для освещения.
// Текстурные координаты (u, v) говорят, какая точка картинки попадает в эту вершину:
// (0, 0) — левый верхний угол изображения, (1, 1) — правый нижний.
// Значения больше 1 означают повторение текстуры (режим адресации REPEAT у сэмплера)
struct Vertex {
	Vec3 position;
	Vec3 normal;
	Vec2 uv;
};

// Все генераторы ниже строят треугольники так, что векторное произведение
// cross(b - a, c - a) смотрит наружу. В нашем мире (y вверх, камера смотрит вдоль +z)
// такие треугольники видны на экране по часовой стрелке (VK_FRONT_FACE_CLOCKWISE).
//
// Про швы: у замкнутых поверхностей (тор, сфера) первая и последняя строки сетки
// совпадают по положению, но текстурные координаты у них разные (0 и 1).
// Поэтому вершины на шве дублируются: иначе между u = 0.98 и u = 0 интерполяция
// «прокрутила» бы всю текстуру назад внутри одного треугольника

// Генерирует сетку тора.
// major_radius — радиус большой окружности (от центра тора до центра трубки),
// minor_radius — радиус трубки,
// major_segments, minor_segments — на сколько частей делим большую и малую окружности,
// repeat_u, repeat_v — сколько раз текстура повторяется вдоль большой и малой окружности
// (целые числа, чтобы на шве узор совпал)
void generateTorus(float major_radius, float minor_radius,
                   uint32_t major_segments, uint32_t minor_segments,
                   float repeat_u, float repeat_v,
                   std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

// Генерирует UV-сферу радиуса radius: slices делений по долготе, stacks — по широте.
// Текстура натягивается как карта мира (развёртка 2:1): u — долгота, v — широта
void generateSphere(float radius, uint32_t slices, uint32_t stacks,
                    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

// Генерирует прямоугольник width x depth в плоскости XZ с центром в начале координат,
// нормаль смотрит вверх (+y). Текстура повторяется repeat_u раз по x и repeat_v раз по z.
// Используется как пол и (после поворота) как стены
void generatePlane(float width, float depth, float repeat_u, float repeat_v,
                   std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
