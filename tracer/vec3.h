#ifndef VEC3_H
#define VEC3_H

#include <cmath>
#include <iostream>

struct vec3
{
    public:
        double e[3];

	vec3() : e{0,0,0} {}
	vec3(double x, double y, double z) : e{x,y,z} {}

	double x() const { return e[0]; }
	double y() const { return e[1]; }
	double z() const { return e[2]; }

	vec3 operator-() const { return {-e[0], -e[1], -e[2]}; }
	double  operator[](int i) const { return e[i]; }
	double& operator[](int i)       { return e[i]; }

	vec3& operator +=(const vec3 other)
	{
	    e[0] += other[0];
	    e[1] += other[1];
	    e[2] += other[2];

	    return *this;
	}

	vec3& operator *=(double t)
	{
	    e[0] *= t;
	    e[1] *= t;
	    e[2] *= t;

	    return *this;
	}

	vec3& operator /=(double t)
	{
	    e[0] /= t;
	    e[1] /= t;
	    e[2] /= t;

	    return *this;
	}

	double length() const
	{
	    return std::sqrt(length_sqr());
	}

	double length_sqr() const
	{
	    return e[0]*e[0] + e[1]*e[1] + e[2]*e[2];
	}	
};


using point3 = vec3;

inline std::ostream& operator<<(std::ostream& out, const vec3& v)
{
    return out << v.e[0] << ' ' << v.e[1] << ' ' << v.e[2];
}

inline vec3 operator +(const vec3& a, const vec3& b)
{
    return vec3(a[0] + b[0], a[1] + b[1], a[2] + b[2]);
}

inline vec3 operator -(const vec3& a, const vec3& b)
{
    return vec3(a[0] - b[0], a[1] - b[1], a[2] - b[2]); 
}

inline vec3 operator *(const vec3& a, const vec3& b)
{
    return vec3(a[0] * b[0], a[1] * b[1], a[2] * b[2]);    
}

inline vec3 operator *(const vec3& v, double t)
{
    return vec3(v[0] * t, v[1] * t, v[2] * t);
}

inline vec3 operator *(double t, const vec3& v)
{
    return v * t;
}

inline vec3 operator /(const vec3& v, double t)
{
    return v * (1/t);
}

inline double dot(const vec3& a, const vec3& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline vec3 cross(const vec3& a, const vec3& b)
{
    return vec3(
        a.e[1] * b.e[2] - a.e[2] * b.e[1],
        a.e[2] * b.e[0] - a.e[0] * b.e[2],
        a.e[0] * b.e[1] - a.e[1] * b.e[0]
    );
}

inline vec3 unit_vector(const vec3& v)
{
    return v / v.length();
}


#endif
