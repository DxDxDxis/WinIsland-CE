#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace wi {
inline constexpr double TopArcBaseline = 4.0;
struct OutlinePoint { double x, y; };
struct OutlineCorner { OutlinePoint start, c1, c2, end; };
// Positive top: detached convex corner. Negative top: outward screen-edge
// shoulder. At zero both collapse to a square, allowing continuous morphing.
inline std::array<OutlineCorner, 4> islandOutline(double x, double w, double h,
                                                 double radius, double top) {
    constexpr double k = .5522847498307936;
    double r = std::clamp(radius, 0., std::min(w, h) / 2);
    double t = std::clamp(top, -std::min(w, h) / 2, std::min(w, h) / 2);
    double a = std::abs(t), right = x + w;
    // A slightly taller blend softens the larger shoulder without a bulb.
    // Bound the vertical extent so it cannot run into the bottom corner.
    double depth = t < 0 ? std::min(a * 1.1, h-r) : a;
    OutlineCorner tr = t < 0
        ? OutlineCorner{{right+a,0},{right+(1-k)*a,0},{right,(1-k)*depth},{right,depth}}
        : OutlineCorner{{right-a,0},{right-(1-k)*a,0},{right,(1-k)*a},{right,a}};
    OutlineCorner tl{{2*x+w-tr.end.x,tr.end.y}, {2*x+w-tr.c2.x,tr.c2.y},
                     {2*x+w-tr.c1.x,tr.c1.y}, {2*x+w-tr.start.x,tr.start.y}};
    return {{tr, {{right,h-r},{right,h-(1-k)*r},{right-(1-k)*r,h},{right-r,h}},
             {{x+r,h},{x+(1-k)*r,h},{x,h-(1-k)*r},{x,h-r}}, tl}};
}
inline OutlinePoint outlineAt(const OutlineCorner &c, double t) {
    double u=1-t;
    return {u*u*u*c.start.x+3*u*u*t*c.c1.x+3*u*t*t*c.c2.x+t*t*t*c.end.x,
            u*u*u*c.start.y+3*u*u*t*c.c1.y+3*u*t*t*c.c2.y+t*t*t*c.end.y};
}
} // namespace wi
