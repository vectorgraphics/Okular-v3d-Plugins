/* bound.h
 * Bézier bounding-box computation extracted from path3.cc.
 * Provides tight bounds for Bezier patches and triangles via recursive
 * de Casteljau subdivision, matching drawsurface.cc usage.
 */

#pragma once

#include <cstddef>

namespace camp {
class triple;

// Recursive subdivision bound functions (from path3.cc).
double bound(double *P, double (*m)(double, double), double b, double fuzz, int depth);
double boundtri(double *P, double (*m)(double, double), double b, double fuzz, int depth);

// Cubic Bézier curve bound (from path3.cc) — operates on triple control points.
double bound(triple z0, triple c0, triple c1, triple z1,
             double (*m)(double, double),
             double (*f)(const triple&), double b, double fuzz, int depth);

// Fuzz constants (from path.cc).
extern const double Fuzz;
extern const unsigned maxdepth;

namespace run {
    // L-infinity norm of an array (from runarray.in → runarray.cc).
    double norm(double *a, size_t n);
}
}

