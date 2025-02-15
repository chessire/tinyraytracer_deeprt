#define _USE_MATH_DEFINES

#include <cmath>
#include <limits>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include "geometry.h"

#define MAX_DISTANCE 9999.f
#define EPSILON 1e-3f
#define MAX_MARCHING_STEPS 128

struct Light {
    Light(const Vec3f &p, const float i) : position(p), intensity(i) {}
    Vec3f position;
    float intensity;
};

struct Material {
    Material(const float r, const Vec4f &a, const Vec3f &color, const float spec) : refractive_index(r), albedo(a), diffuse_color(color), specular_exponent(spec) {}
    Material() : refractive_index(1), albedo(1,0,0,0), diffuse_color(), specular_exponent() {}
    float refractive_index;
    Vec4f albedo;
    Vec3f diffuse_color;
    float specular_exponent;
};

class sdf_model
{
	Material material;
public:
	sdf_model(const Material& m) : material(m) { }

	virtual float sdf(Vec3f point) const = 0;
	virtual bool try_get_normal(Vec3f point, Vec3f& n) const = 0;
	const Material& get_material() const { return material; }
};

struct Sphere : public sdf_model {
	Vec3f center;
	float radius;

	Sphere(const Vec3f &c, const float r, const Material &m) : sdf_model(m), center(c), radius(r) {}

	float sdf(Vec3f point) const final
	{
		return (point - center).norm() - radius;
	}

	bool try_get_normal(Vec3f point, Vec3f& n) const final
	{
		Vec3f point_to_center = point - center;
		if (abs(point_to_center.norm()) < EPSILON)
			return false;

		n = point_to_center.normalize();

		return true;
	}

    bool ray_intersect(const Vec3f &orig, const Vec3f &dir, float &t0) const {
        Vec3f L = center - orig;
		float tca = L * dir;
		if (tca < 0) return false;
        float d2 = L*L - tca*tca;
        if (d2 > radius*radius) return false;
        float thc = sqrtf(radius*radius - d2);
        t0       = tca - thc;
		float t1 = tca + thc;
		if (t0 < 0) t0 = t1;
		if (t0 < 0) return false;
        return true;
    }
};

void fresnel(const Vec3f &I, const Vec3f &N, const float &ior, float &kr)
{
	float cosi = std::clamp(I * N, -1.f, 1.f);
	float etai = 1, etat = ior;
	if (cosi > 0) { std::swap(etai, etat); }
	// Compute sini using Snell's law
	float sint = etai / etat * sqrtf(std::max(0.f, 1 - cosi * cosi));
	// Total internal reflection
	if (sint >= 1) {
		kr = 1;
	}
	else {
		float cost = sqrtf(std::max(0.f, 1 - sint * sint));
		cosi = fabsf(cosi);
		float Rs = ((etat * cosi) - (etai * cost)) / ((etat * cosi) + (etai * cost));
		float Rp = ((etai * cosi) - (etat * cost)) / ((etai * cosi) + (etat * cost));
		kr = (Rs * Rs + Rp * Rp) / 2;
	}
	// As a consequence of the conservation of energy, transmittance is given by:
	// kt = 1 - kr;
}

Vec3f reflect(const Vec3f &I, const Vec3f &N) {
    return I - N*2.f*(I*N);
}

Vec3f refract(const Vec3f &I, const Vec3f &N, const float eta_t, const float eta_i=1.f) { // Snell's law
    float cosi = - std::max(-1.f, std::min(1.f, I*N));
    if (cosi<0) return refract(I, -N, eta_i, eta_t); // if the ray comes from the inside the object, swap the air and the media
    float eta = eta_i / eta_t;
    float k = 1 - eta*eta*(1 - cosi*cosi);
    return k<0 ? Vec3f(1,0,0) : I*eta + N*(eta*cosi - sqrtf(k)); // k<0 = total reflection, no ray to refract. I refract it anyways, this has no physical meaning
}

float scene_sdf(const Vec3f& point, const std::vector<const sdf_model*> &models, const sdf_model*& hit_model)
{
	float min_dist = MAX_DISTANCE;
	hit_model = nullptr;
	for (const auto& model : models)
	{
		float dist = model->sdf(point);
		if (dist < 0.f)
			continue;

		if (min_dist > dist)
		{
			min_dist = dist;
			hit_model = model;
		}
	}
	return min_dist;
}

bool ray_marching(const Vec3f &orig, const Vec3f &dir, const std::vector<const sdf_model*> &models, Vec3f &hit, Vec3f &N, Material &material)
{
	float depth = EPSILON;
	for (int i = 0; i < MAX_MARCHING_STEPS; ++i)
	{
		const sdf_model* hit_model = nullptr;
		float dist = scene_sdf(orig + dir * depth, models, hit_model);

		if (hit_model == nullptr)
			break;

		depth += dist;
		if (dist < EPSILON)
		{
			hit = orig + dir * depth;
			if (hit_model->try_get_normal(hit, N) == false)
				std::cout << "normal bug!";
			material = hit_model->get_material();
			return true;
		}

		// for checkboard(temp comment)
		//if (depth >= MAX_DISTANCE)
		//	return false;
	}

	float checkerboard_dist = std::numeric_limits<float>::max();
	if (fabs(dir.y) > EPSILON) {
		float d = -(orig.y + 4) / dir.y; // the checkerboard plane has equation y = -4
		Vec3f pt = orig + dir * d;
		if (d > 0 && fabs(pt.x) < 10 && pt.z<-10 && pt.z>-30) {
			checkerboard_dist = d;
			hit = pt;
			N = Vec3f(0, 1, 0);
			material.diffuse_color = (int(.5*hit.x + 1000) + int(.5*hit.z)) & 1 ? Vec3f(.3, .3, .3) : Vec3f(.3, .2, .1);
		}
	}

	return std::min(MAX_DISTANCE, checkerboard_dist)<1000;
}

//bool scene_intersect(const Vec3f &orig, const Vec3f &dir, const std::vector<Sphere> &spheres, Vec3f &hit, Vec3f &N, Material &material) {
//    float spheres_dist = std::numeric_limits<float>::max();
//    for (size_t i=0; i < spheres.size(); i++) {
//        float dist_i;
//        if (spheres[i].ray_intersect(orig, dir, dist_i) && dist_i < spheres_dist) {
//            spheres_dist = dist_i;
//            hit = orig + dir*dist_i;
//            N = (hit - spheres[i].center).normalize();
//            material = spheres[i].get_material();
//        }
//    }
//
//    float checkerboard_dist = std::numeric_limits<float>::max();
//    if (fabs(dir.y)>1e-3)  {
//        float d = -(orig.y+4)/dir.y; // the checkerboard plane has equation y = -4
//        Vec3f pt = orig + dir*d;
//        if (d>0 && fabs(pt.x)<10 && pt.z<-10 && pt.z>-30 && d<spheres_dist) {
//            checkerboard_dist = d;
//            hit = pt;
//            N = Vec3f(0,1,0);
//            material.diffuse_color = (int(.5*hit.x+1000) + int(.5*hit.z)) & 1 ? Vec3f(.3, .3, .3) : Vec3f(.3, .2, .1);
//        }
//    }
//    return std::min(spheres_dist, checkerboard_dist)<1000;
//}

Vec3f cast_ray(const Vec3f &orig, const Vec3f &dir, const std::vector<const sdf_model*> &models, const std::vector<Light> &lights, size_t depth=0) {
    Vec3f point, N;
    Material material;

    if (depth>4 || !ray_marching(orig, dir, models, point, N, material)) {
        return Vec3f(0.2, 0.7, 0.8); // background color
	}

	Vec3f refract_color(0.f, 0.f, 0.f);
	// compute fresnelt
	float kr;
	fresnel(dir, N, material.refractive_index, kr);
	// compute refraction if it is not a case of total internal reflection
	if (kr < 1) {
		Vec3f refract_dir = refract(dir, N, material.refractive_index).normalize();
		Vec3f refract_orig = refract_dir * N < 0 ? point - N * EPSILON : point + N * EPSILON;
		refract_color = cast_ray(refract_orig, refract_dir, models, lights, depth + 1);
	}

	Vec3f reflect_dir = reflect(dir, N).normalize();
	Vec3f reflect_orig = reflect_dir * N < 0 ? point - N * 1e-3 : point + N * 1e-3; // offset the original point to avoid occlusion by the object itself
	Vec3f reflect_color = cast_ray(reflect_orig, reflect_dir, models, lights, depth + 1);

    float diffuse_light_intensity = 0, specular_light_intensity = 0;
    for (size_t i=0; i<lights.size(); i++) {
        Vec3f light_dir      = (lights[i].position - point).normalize();
        float light_distance = (lights[i].position - point).norm();

        Vec3f shadow_orig = light_dir*N < 0 ? point - N*1e-3 : point + N*1e-3; // checking if the point lies in the shadow of the lights[i]
        Vec3f shadow_pt, shadow_N;
        Material tmpmaterial;
        if (ray_marching(shadow_orig, light_dir, models, shadow_pt, shadow_N, tmpmaterial) && (shadow_pt-shadow_orig).norm() < light_distance)
            continue;

        diffuse_light_intensity  += lights[i].intensity * std::max(0.f, light_dir*N);
        specular_light_intensity += powf(std::max(0.f, -reflect(-light_dir, N)*dir), material.specular_exponent)*lights[i].intensity;
    }

    return material.diffuse_color * diffuse_light_intensity * material.albedo[0] + Vec3f(1., 1., 1.)*specular_light_intensity * material.albedo[1] + reflect_color * material.albedo[2]/* * kr*/ + refract_color * material.albedo[3]/* * (1 - kr)*/;
}

void render(const std::vector<const sdf_model*> &models, const std::vector<Light> &lights) {
    const int   width    = 1024;
    const int   height   = 768;
    const float fov      = M_PI/3.;
    std::vector<Vec3f> framebuffer(width*height);

    #pragma omp parallel for
    for (size_t j = 0; j<height; j++) { // actual rendering loop
        for (size_t i = 0; i<width; i++) {
            float dir_x =  (i + 0.5) -  width/2.;
            float dir_y = -(j + 0.5) + height/2.;    // this flips the image at the same time
            float dir_z = -height/(2.*tan(fov/2.));
            framebuffer[i+j*width] = cast_ray(Vec3f(0,0,0), Vec3f(dir_x, dir_y, dir_z).normalize(), models, lights);
        }
    }

    std::ofstream ofs; // save the framebuffer to file
    ofs.open("./out.ppm",std::ios::binary);
    ofs << "P6\n" << width << " " << height << "\n255\n";
    for (size_t i = 0; i < height*width; ++i) {
        Vec3f &c = framebuffer[i];
		float max = std::max(c[0], std::max(c[1], c[2]));
		if (max > 1) c = c * (1. / max);
        for (size_t j = 0; j<3; j++) {
            ofs << (char)(255 * std::max(0.f, std::min(1.f, framebuffer[i][j])));
        }
    }
    ofs.close();
}

int main() {
    Material      ivory(0.0, Vec4f(0.6,  0.3, 0.1, 0.0), Vec3f(0.4, 0.4, 0.3),   50.);
    Material      glass(1.5, Vec4f(0.0,  0.5, 0.1, 0.8), Vec3f(0.6, 0.7, 0.8),  125.);
    Material red_rubber(0.0, Vec4f(0.9,  0.1, 0.0, 0.0), Vec3f(0.3, 0.1, 0.1),   10.);
    Material     mirror(0.0, Vec4f(0.0, 10.0, 0.8, 0.0), Vec3f(1.0, 1.0, 1.0), 1425.);

    std::vector<const sdf_model*> models;
    models.push_back(new Sphere(Vec3f(-3,    0,   -16), 2,      ivory));
	models.push_back(new Sphere(Vec3f(-1.0, -1.5, -12), 2,      glass));
	models.push_back(new Sphere(Vec3f( 1.5, -0.5, -18), 3, red_rubber));
	models.push_back(new Sphere(Vec3f( 7,    5,   -18), 4,     mirror));

    std::vector<Light>  lights;
    lights.push_back(Light(Vec3f(-20, 20,  20), 1.5));
    lights.push_back(Light(Vec3f( 30, 50, -25), 1.8));
	lights.push_back(Light(Vec3f(30, 20, 30), 1.7));

	render(models, lights);

	for (auto model : models)
	{
		delete model;
	}
	models.clear();

    return 0;
}

