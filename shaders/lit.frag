#version 450 core

// Позиция, нормаль и текстурные координаты, интерполированные между вершинами треугольника
layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_world_normal;
layout(location = 2) in vec2 in_uv;

// Выход фрагментного шейдера: цвет пикселя в цветовое вложение 0
layout(location = 0) out vec4 out_color;

// Те же блоки, что и в lit.vert (одинаковое описание обязательно)
layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec3 camera_position;
	vec3 directional_light_direction;
	vec3 directional_light_color;
	vec3 ambient_light;
	uint point_light_count;
	uint spot_light_count;
} scene_uniforms;

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix;
	vec3 albedo_factor;
	float shininess;
	vec3 specular_factor;
	float emissive_strength;
} model_uniforms;

// Набор 1 — это набор МАТЕРИАЛА: кроме динамического uniform-буфера в нём три пары
// «изображение + сэмплер» (VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).
// sampler2D — текстура вместе с правилами чтения: фильтрация, mip-уровни, повторение
//
// Альбедо — основной цвет поверхности (K_d и K_a в Блинне–Фонге)
layout(set = 1, binding = 1) uniform sampler2D albedo_texture;
// Specular — где и насколько сильно поверхность блестит (K_s).
// Если у материала её нет, сюда привязана служебная текстура 1x1 с чёрным пикселем:
// K_s = 0, и блика нет нигде
layout(set = 1, binding = 2) uniform sampler2D specular_texture;
// Emissive — собственное свечение. Прибавляется к цвету ПОСЛЕ освещения, поэтому
// светящиеся места видны даже в полной темноте. Нет текстуры — чёрный пиксель, свечения нет
layout(set = 1, binding = 3) uniform sampler2D emissive_texture;

// Флаги нетривиального сэмплирования (доп. задание 2) — биты поля effect_flags
const uint EFFECT_MULTI_SAMPLE  = 1u; // несколько выборок одной текстуры с разными координатами
const uint EFFECT_WAVE          = 2u; // волновое искажение текстурных координат
const uint EFFECT_COLOR_MODULATE = 4u; // модуляция цвета (переливание оттенка)
const uint EFFECT_EMISSIVE_PULSE = 8u; // пульсация свечения бегущей волной

// Push-константы: включение и параметры эффекта для текущего вызова отрисовки.
// Записываются vkCmdPushConstants прямо перед vkCmdDrawIndexed, в uniform-буфер модели не попадают
layout(push_constant) uniform DrawConstants {
	uint effect_flags;
	// Время в секундах — от него зависят все эффекты
	float time;
	// Сила эффекта (0 — нет, 1 — сильно)
	float strength;
	// Скорость анимации эффекта
	float speed;
} draw;

// Точечный источник. Раскладка std430: vec3 выравнивается на 16 байт,
// поэтому структура занимает 32 байта (на C++ после каждого vec3 — float-заглушка)
struct PointLight {
	vec3 position;
	// Цвет, уже умноженный на интенсивность
	vec3 color;
};

// Прожектор. Свободные 4 байта после vec3 занимаем полезными float,
// чтобы не тратить место на заглушки. Размер — 48 байт
struct SpotLight {
	vec3 position;
	// Косинус внутреннего угла: внутри него прожектор светит в полную силу
	float inner_cos;
	// Направление, куда светит прожектор (единичный вектор)
	vec3 direction;
	// Косинус внешнего угла: за ним света уже нет
	float outer_cos;
	// Цвет, уже умноженный на интенсивность
	vec3 color;
};

// Storage-буферы (лекция 4): массивы без фиксированного размера в шейдере.
// readonly — шейдер только читает их
layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight spot_lights[];
};

const float TWO_PI = 6.28318530718;

// Свойства поверхности в текущем пикселе, прочитанные из текстур
struct Surface {
	vec3 albedo;
	vec3 specular;
	vec3 emissive;
};

// Модель Блинна–Фонга для одного источника (лекция 4):
// C = I * (max(n·l, 0) * K_d + K_s * max(n·h, 0)^α)
// n — нормаль, v — направление на камеру, l — направление на источник,
// radiance — пришедший свет I (цвет * интенсивность * затухание).
// K_d и K_s теперь берутся не из uniform-буфера, а из текстур для каждого пикселя
vec3 blinnPhong(Surface surface, vec3 n, vec3 v, vec3 l, vec3 radiance) {
	float n_dot_l = max(dot(n, l), 0.0);

	// Если свет падает с обратной стороны поверхности, бликов тоже быть не должно
	if (n_dot_l <= 0.0) {
		return vec3(0.0);
	}

	// Половинный вектор между направлением на свет и на камеру
	vec3 h = normalize(l + v);
	float n_dot_h = max(dot(n, h), 0.0);

	vec3 diffuse = n_dot_l * surface.albedo;
	vec3 specular = surface.specular * pow(n_dot_h, model_uniforms.shininess);

	return radiance * (diffuse + specular);
}

// Волновое искажение координат. Все частоты — целые числа периодов на единицу uv,
// поэтому смещение одинаково при u = 0 и u = 1 (2, 3, ...) и на швах сеток не появляется разрыв
vec2 waveDistortion(vec2 uv, float t) {
	vec2 offset = vec2(
		sin(TWO_PI * 2.0 * uv.y + 3.0 * t),
		sin(TWO_PI * 2.0 * uv.x + 2.3 * t)
	);
	return uv + 0.04 * draw.strength * offset;
}

// Несколько выборок ОДНОЙ И ТОЙ ЖЕ текстуры с разными текстурными координатами:
// три слоя «плывут» в разные стороны и с разным масштабом, а их веса плавно
// перетекают друг в друга со временем
vec3 sampleAlbedoLayers(vec2 uv, float t) {
	// Слой 1: сдвиг вправо-вверх
	vec2 uv_a = uv + t * vec2(0.10, 0.03);
	// Слой 2: вдвое мельче (масштаб — целое число, чтобы не было шва) и плывёт в другую сторону
	vec2 uv_b = uv * 2.0 + t * vec2(-0.07, 0.11);
	// Слой 3: «водоворот» — смещение по кругу, разное в разных местах текстуры
	vec2 uv_c = uv + 0.15 * vec2(cos(TWO_PI * uv.y + t), sin(TWO_PI * uv.x + 1.3 * t));

	vec3 a = texture(albedo_texture, uv_a).rgb;
	vec3 b = texture(albedo_texture, uv_b).rgb;
	vec3 c = texture(albedo_texture, uv_c).rgb;

	// Веса слоёв сдвинуты по фазе на треть периода; сумма весов нормируется до 1
	vec3 weights = 0.5 + 0.5 * sin(0.8 * t + vec3(0.0, 2.094, 4.189));
	weights = weights * weights + 0.05;
	weights /= weights.x + weights.y + weights.z;

	vec3 layered = a * weights.x + b * weights.y + c * weights.z;
	// draw.strength = 0 — обычная выборка, 1 — только смесь слоёв
	return mix(texture(albedo_texture, uv).rgb, layered, clamp(draw.strength * 1.5, 0.0, 1.0));
}

// Читает все текстуры материала с учётом флагов эффекта
Surface sampleSurface() {
	vec2 uv = in_uv;
	float t = draw.time * draw.speed;

	if ((draw.effect_flags & EFFECT_WAVE) != 0u) {
		uv = waveDistortion(uv, t);
	}

	Surface surface;

	if ((draw.effect_flags & EFFECT_MULTI_SAMPLE) != 0u) {
		surface.albedo = sampleAlbedoLayers(uv, t);
	} else {
		surface.albedo = texture(albedo_texture, uv).rgb;
	}

	if ((draw.effect_flags & EFFECT_COLOR_MODULATE) != 0u) {
		// Модуляция цвета: умножаем на радужный оттенок, который бежит по поверхности.
		// Палитра из косинусов со сдвигом фаз по каналам R, G, B
		vec3 tint = 0.5 + 0.5 * cos(TWO_PI * (0.15 * t + uv.x + uv.y) + vec3(0.0, 2.094, 4.189));
		// Средняя яркость tint около 0.5, поэтому умножаем на 1.8, чтобы объект не темнел
		surface.albedo *= mix(vec3(1.0), 1.8 * tint, draw.strength);
	}

	surface.albedo *= model_uniforms.albedo_factor;

	// Specular-карта хранит «данные», а не цвет, поэтому она в формате UNORM (без sRGB)
	surface.specular = texture(specular_texture, uv).rgb * model_uniforms.specular_factor;

	surface.emissive = texture(emissive_texture, uv).rgb * model_uniforms.emissive_strength;

	if ((draw.effect_flags & EFFECT_EMISSIVE_PULSE) != 0u) {
		// Волна яркости бежит вдоль u; свечение не гаснет полностью
		float pulse = 0.5 + 0.5 * sin(TWO_PI * uv.x - 2.0 * t);
		surface.emissive *= mix(1.0, 0.25 + 1.25 * pulse, draw.strength);
	}

	return surface;
}

void main() {
	Surface surface = sampleSurface();

	// После интерполяции нормаль уже не единичная — нормируем заново
	vec3 n = normalize(in_world_normal);
	vec3 v = normalize(scene_uniforms.camera_position - in_world_position);

	// Рассеянный свет: одинаков для всех точек, без него теневая сторона чёрная.
	// В качестве K_a берём альбедо из текстуры
	vec3 color = scene_uniforms.ambient_light * surface.albedo;

	// 1. Направленный источник: l одинаков для всех точек, затухания нет.
	// Свет летит по directional_light_direction, значит направление НА источник — обратное
	vec3 l_dir = -normalize(scene_uniforms.directional_light_direction);
	color += blinnPhong(surface, n, v, l_dir, scene_uniforms.directional_light_color);

	// 2. Точечные источники
	for (uint i = 0; i < scene_uniforms.point_light_count; ++i) {
		vec3 to_light = point_lights[i].position - in_world_position;
		// Квадрат расстояния D² — это скалярное произведение вектора на себя
		float distance_squared = max(dot(to_light, to_light), 1e-4);
		vec3 l = to_light * inversesqrt(distance_squared);

		// Закон обратных квадратов: интенсивность падает как 1 / D²
		float attenuation = 1.0 / distance_squared;

		color += blinnPhong(surface, n, v, l, point_lights[i].color * attenuation);
	}

	// 3. Прожекторы
	for (uint i = 0; i < scene_uniforms.spot_light_count; ++i) {
		SpotLight light = spot_lights[i];

		vec3 to_light = light.position - in_world_position;
		float distance_squared = max(dot(to_light, to_light), 1e-4);
		vec3 l = to_light * inversesqrt(distance_squared);

		float attenuation = 1.0 / distance_squared;

		// Косинус угла между осью прожектора и лучом от прожектора к точке.
		// Луч от прожектора к точке — это -l
		float cos_theta = dot(-l, normalize(light.direction));

		// Мягкие края: внутри внутреннего угла cone = 1, за внешним — 0,
		// между ними плавно убывает линейно по косинусу.
		// Если углы равны, знаменатель почти 0 и край получается резким
		float epsilon = max(light.inner_cos - light.outer_cos, 1e-4);
		float cone = clamp((cos_theta - light.outer_cos) / epsilon, 0.0, 1.0);

		color += blinnPhong(surface, n, v, l, light.color * attenuation * cone);
	}

	// 4. Свечение: прибавляется к уже посчитанному Блинну–Фонгу и от света не зависит
	color += surface.emissive;

	out_color = vec4(color, 1.0);
}
