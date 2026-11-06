#include "geometry.hpp"

#include <cmath>

namespace {

constexpr float pi = 3.14159265358979f;

// Индексы для сетки rows x columns вершин (вершина (i, j) лежит в i * columns + j).
// Каждую клетку сетки делим на два треугольника.
// Сетка не замыкается: у замкнутых поверхностей последняя строка — копия первой
// с другими текстурными координатами (см. комментарий про швы в geometry.hpp)
void appendGridIndices(uint32_t rows, uint32_t columns, std::vector<uint32_t>& indices) {
	for (uint32_t i = 0; i + 1 < rows; ++i) {
		for (uint32_t j = 0; j + 1 < columns; ++j) {
			const uint32_t p00 = i * columns + j;
			const uint32_t p10 = (i + 1) * columns + j;
			const uint32_t p01 = i * columns + j + 1;
			const uint32_t p11 = (i + 1) * columns + j + 1;

			// Порядок подобран так, что cross(p01 - p00, p10 - p00) смотрит наружу
			indices.push_back(p00);
			indices.push_back(p01);
			indices.push_back(p10);

			indices.push_back(p10);
			indices.push_back(p01);
			indices.push_back(p11);
		}
	}
}

} // namespace

void generateTorus(float major_radius, float minor_radius,
                   uint32_t major_segments, uint32_t minor_segments,
                   float repeat_u, float repeat_v,
                   std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
	vertices.clear();
	indices.clear();

	// Вершины: сетка по двум углам.
	// u — угол по большой окружности (вокруг оси Y),
	// v — угол по малой окружности (вокруг трубки).
	// i и j доходят до major_segments и minor_segments включительно: это и есть дубли вершин на шве
	for (uint32_t i = 0; i <= major_segments; ++i) {
		const float s = float(i) / float(major_segments);
		const float u = 2.0f * pi * s;

		for (uint32_t j = 0; j <= minor_segments; ++j) {
			const float t = float(j) / float(minor_segments);
			const float v = 2.0f * pi * t;

			// Расстояние от оси Y до точки
			const float ring = major_radius + minor_radius * std::cos(v);

			const Vec3 position = {
				ring * std::cos(u),
				minor_radius * std::sin(v),
				ring * std::sin(u),
			};

			// Процедурная нормаль: направление от центра трубки к точке поверхности.
			// Центр трубки лежит на большой окружности: (R cos u, 0, R sin u).
			// Вычитаем его из позиции и делим на minor_radius — получаем
			// (cos v cos u, sin v, cos v sin u), длина этого вектора уже равна 1
			const Vec3 normal = {
				std::cos(v) * std::cos(u),
				std::sin(v),
				std::cos(v) * std::sin(u),
			};

			// Текстурные координаты — те же параметры поверхности, отнормированные в [0, repeat].
			// На внешней стороне тора угол v растёт вверх, а v текстуры растёт вниз
			// по картинке, поэтому берём 1 - t, чтобы изображение не было перевёрнутым
			const Vec2 uv = { s * repeat_u, (1.0f - t) * repeat_v };

			vertices.push_back({ position, normal, uv });
		}
	}

	appendGridIndices(major_segments + 1, minor_segments + 1, indices);
}

void generateSphere(float radius, uint32_t slices, uint32_t stacks,
                    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
	vertices.clear();
	indices.clear();

	// Сфера — это тор с нулевым большим радиусом, у которого угол v идёт
	// только от южного полюса (-90°) до северного (+90°).
	// u — долгота (вокруг оси Y), v — широта
	for (uint32_t i = 0; i <= slices; ++i) {
		const float s = float(i) / float(slices);
		const float u = 2.0f * pi * s;

		for (uint32_t j = 0; j <= stacks; ++j) {
			const float t = float(j) / float(stacks);
			const float v = -0.5f * pi + pi * t;

			// У сферы с центром в начале координат нормаль — это просто
			// направление из центра в точку
			const Vec3 normal = {
				std::cos(v) * std::cos(u),
				std::sin(v),
				std::cos(v) * std::sin(u),
			};

			// Развёртка «долгота-широта»: u по долготе, v от северного полюса (0) к южному (1)
			const Vec2 uv = { s, 1.0f - t };

			vertices.push_back({ normal * radius, normal, uv });
		}
	}

	appendGridIndices(slices + 1, stacks + 1, indices);
}

void generatePlane(float width, float depth, float repeat_u, float repeat_v,
                   std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
	const float hw = 0.5f * width;
	const float hd = 0.5f * depth;
	const Vec3 up = { 0.0f, 1.0f, 0.0f };

	// Освещение считается для каждого пикселя, а текстура повторяется сэмплером,
	// поэтому плоскость может состоять всего из двух треугольников.
	// u растёт вдоль +x, v — от дальнего края (z = +hd) к ближнему: если смотреть вдоль +z,
	// верх картинки окажется вдали. После поворота плоскости в стену (−90° вокруг X)
	// дальний край становится верхним, и картинка стоит правильно
	vertices = {
		{ { -hw, 0.0f, -hd }, up, { 0.0f,     repeat_v } },
		{ { -hw, 0.0f,  hd }, up, { 0.0f,     0.0f     } },
		{ {  hw, 0.0f, -hd }, up, { repeat_u, repeat_v } },
		{ {  hw, 0.0f,  hd }, up, { repeat_u, 0.0f     } },
	};

	// cross(v1 - v0, v2 - v0) = cross((0, 0, 2hd), (2hw, 0, 0)) = (0, 4·hw·hd, 0) — смотрит вверх
	indices = {
		0, 1, 2,
		2, 1, 3,
	};
}
