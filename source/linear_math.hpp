#pragma once

#include <cmath>

// Вектор из двух чисел: текстурные координаты (u, v)
struct Vec2 {
	float x, y;
};

// Вектор из трёх чисел (x, y, z)
struct Vec3 {
	float x, y, z;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 operator-(Vec3 a, Vec3 b) {
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator-(Vec3 a) {
	return { -a.x, -a.y, -a.z };
}

// Умножение вектора на число
inline Vec3 operator*(Vec3 a, float k) {
	return { a.x * k, a.y * k, a.z * k };
}

inline Vec3 operator*(float k, Vec3 a) {
	return a * k;
}

// Скалярное произведение: |a| * |b| * cos(угол между ними)
inline float dot(Vec3 a, Vec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Векторное произведение: вектор, перпендикулярный a и b
inline Vec3 cross(Vec3 a, Vec3 b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x,
	};
}

inline float length(Vec3 a) {
	return std::sqrt(dot(a, a));
}

// Вектор того же направления, но длины 1. Нулевой вектор заменяем на fallback,
// чтобы не делить на ноль (например, если в интерфейсе ввели направление (0, 0, 0))
inline Vec3 normalize(Vec3 a, Vec3 fallback = { 0.0f, -1.0f, 0.0f }) {
	const float len = length(a);
	if (len < 1e-6f) {
		return fallback;
	}
	return a * (1.0f / len);
}

// Матрица 4x4. Хранится по столбцам (column-major), как mat4 в GLSL,
// поэтому её можно копировать в uniform-буфер без перестановки
struct Mat4 {
	// Элемент в строке row и столбце col лежит в m[col * 4 + row]
	float m[16];

	float& at(int row, int col) { return m[col * 4 + row]; }
	float at(int row, int col) const { return m[col * 4 + row]; }
};

// Перевод градусов в радианы (тригонометрия в C++ работает с радианами)
inline float radians(float degrees) {
	return degrees * 3.14159265358979f / 180.0f;
}

// Единичная матрица: ничего не меняет
inline Mat4 identity() {
	Mat4 r = {};
	r.at(0, 0) = 1.0f;
	r.at(1, 1) = 1.0f;
	r.at(2, 2) = 1.0f;
	r.at(3, 3) = 1.0f;
	return r;
}

// Произведение матриц. В (a * b) * p сначала применяется b, потом a
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
	Mat4 r = {};
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			float sum = 0.0f;
			for (int k = 0; k < 4; ++k) {
				sum += a.at(row, k) * b.at(k, col);
			}
			r.at(row, col) = sum;
		}
	}
	return r;
}

// Матрица переноса: единичная со сдвигом (tx, ty, tz) в последнем столбце (лекция 1)
inline Mat4 translation(Vec3 t) {
	Mat4 r = identity();
	r.at(0, 3) = t.x;
	r.at(1, 3) = t.y;
	r.at(2, 3) = t.z;
	return r;
}

// Матрица растяжения: коэффициенты по диагонали
inline Mat4 scaling(Vec3 s) {
	Mat4 r = identity();
	r.at(0, 0) = s.x;
	r.at(1, 1) = s.y;
	r.at(2, 2) = s.z;
	return r;
}

// Поворот вокруг оси X на угол angle (в радианах)
inline Mat4 rotationX(float angle) {
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	Mat4 r = identity();
	r.at(1, 1) = c;
	r.at(1, 2) = -s;
	r.at(2, 1) = s;
	r.at(2, 2) = c;
	return r;
}

// Поворот вокруг оси Y на угол angle (в радианах)
inline Mat4 rotationY(float angle) {
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	Mat4 r = identity();
	r.at(0, 0) = c;
	r.at(0, 2) = s;
	r.at(2, 0) = -s;
	r.at(2, 2) = c;
	return r;
}

// Поворот вокруг оси Z на угол angle (в радианах)
inline Mat4 rotationZ(float angle) {
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	Mat4 r = identity();
	r.at(0, 0) = c;
	r.at(0, 1) = -s;
	r.at(1, 0) = s;
	r.at(1, 1) = c;
	return r;
}

// Перспективная проекция через вертикальный угол обзора (лекция 1).
// Камера смотрит вдоль +z, глубина после деления на w попадает в [0, 1].
// В лабе 2 мир «нормальный»: ось y смотрит вверх. А в каноническом объёме Vulkan
// y направлена вниз, поэтому меняем знак у y (элемент [1][1] отрицательный)
inline Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane) {
	const float t = std::tan(fov_y / 2.0f);
	Mat4 r = {};
	r.at(0, 0) = 1.0f / (aspect * t);
	r.at(1, 1) = -1.0f / t;
	r.at(2, 2) = far_plane / (far_plane - near_plane);
	r.at(2, 3) = -far_plane * near_plane / (far_plane - near_plane);
	// w := z, потом видеокарта делит x, y, z на w (перспективное деление)
	r.at(3, 2) = 1.0f;
	return r;
}

// Ортографическая проекция (лекция 1). Объём вида — параллелепипед:
// по y от -half_height до half_height, по x с учётом соотношения сторон окна.
// Верх (t) — это +half_height, потому что ось y в мире направлена вверх,
// а в Vulkan верх экрана — это y = -1 (см. комментарий к perspective)
inline Mat4 orthographic(float half_height, float aspect, float near_plane, float far_plane) {
	const float t = half_height;
	const float b = -half_height;
	const float l = -half_height * aspect;
	const float r_ = half_height * aspect;

	Mat4 r = identity();
	r.at(0, 0) = 2.0f / (r_ - l);
	r.at(0, 3) = -(r_ + l) / (r_ - l);
	r.at(1, 1) = 2.0f / (b - t);
	r.at(1, 3) = -(b + t) / (b - t);
	r.at(2, 2) = 1.0f / (far_plane - near_plane);
	r.at(2, 3) = -near_plane / (far_plane - near_plane);
	return r;
}

// Применяет матрицу к точке (w = 1): учитываются и поворот, и перенос
inline Vec3 transformPoint(const Mat4& m, Vec3 p) {
	return {
		m.at(0, 0) * p.x + m.at(0, 1) * p.y + m.at(0, 2) * p.z + m.at(0, 3),
		m.at(1, 0) * p.x + m.at(1, 1) * p.y + m.at(1, 2) * p.z + m.at(1, 3),
		m.at(2, 0) * p.x + m.at(2, 1) * p.y + m.at(2, 2) * p.z + m.at(2, 3),
	};
}

// Матрица для нормалей: обратная транспонированная к верхнему блоку 3x3 матрицы модели.
// Если модель растянута неравномерно (например, scale = (2, 1, 1)), то нормаль,
// умноженная на саму M, перестаёт быть перпендикулярной поверхности. (M^-1)^T это исправляет.
// Считаем через векторные произведения: если a0, a1, a2 — столбцы A, то столбцы (A^-1)^T
// равны cross(a1, a2) / det, cross(a2, a0) / det, cross(a0, a1) / det, где det = dot(a0, cross(a1, a2))
inline Mat4 normalMatrix(const Mat4& m) {
	const Vec3 a0 = { m.at(0, 0), m.at(1, 0), m.at(2, 0) };
	const Vec3 a1 = { m.at(0, 1), m.at(1, 1), m.at(2, 1) };
	const Vec3 a2 = { m.at(0, 2), m.at(1, 2), m.at(2, 2) };

	const Vec3 c0 = cross(a1, a2);
	const Vec3 c1 = cross(a2, a0);
	const Vec3 c2 = cross(a0, a1);

	float det = dot(a0, c0);
	if (std::fabs(det) < 1e-12f) {
		det = 1.0f;
	}
	const float inv = 1.0f / det;

	// Переноса у нормалей нет (это направления, а не точки), поэтому последний столбец — (0, 0, 0, 1)
	Mat4 r = identity();
	r.at(0, 0) = c0.x * inv; r.at(1, 0) = c0.y * inv; r.at(2, 0) = c0.z * inv;
	r.at(0, 1) = c1.x * inv; r.at(1, 1) = c1.y * inv; r.at(2, 1) = c1.z * inv;
	r.at(0, 2) = c2.x * inv; r.at(1, 2) = c2.y * inv; r.at(2, 2) = c2.z * inv;
	return r;
}
