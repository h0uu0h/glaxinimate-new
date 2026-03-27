/*
 * SPDX-FileCopyrightText: 2024 Glaxnimate Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "path_boolean.hpp"

#include <clipper2/clipper.h>
#include <cmath>

#include "math/bezier/solver.hpp"

using namespace glaxnimate;
using namespace Clipper2Lib;

namespace {

// Scale factor: Clipper2 works with integers, we use 1000x scale for sub-pixel precision
constexpr double kScale = 1000.0;

// Convert QPointF to Clipper2 integer point
Point64 to_clipper(const QPointF& p)
{
    return Point64(
        static_cast<int64_t>(std::round(p.x() * kScale)),
        static_cast<int64_t>(std::round(p.y() * kScale))
    );
}

// Convert Clipper2 integer point back to QPointF
QPointF from_clipper(const Point64& p)
{
    return QPointF(p.x / kScale, p.y / kScale);
}

// Flatten a single bezier segment (cubic) into polyline points
void flatten_segment(
    const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3,
    Path64& out, int steps)
{
    for ( int i = 1; i <= steps; i++ )
    {
        double t = double(i) / steps;
        double u = 1.0 - t;
        QPointF pt = u*u*u * p0 + 3*u*u*t * p1 + 3*u*t*t * p2 + t*t*t * p3;
        out.push_back(to_clipper(pt));
    }
}

// Adaptive flatness — use fewer samples for near-straight segments
int adaptive_steps(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, int base_steps)
{
    // Measure deviation of control points from the straight line p0→p3
    QPointF chord = p3 - p0;
    double chord_len = std::sqrt(QPointF::dotProduct(chord, chord));
    if ( chord_len < 1e-6 )
        return 2;

    QPointF n(-chord.y() / chord_len, chord.x() / chord_len);
    double d1 = std::abs(QPointF::dotProduct(p1 - p0, n));
    double d2 = std::abs(QPointF::dotProduct(p2 - p0, n));
    double max_dev = std::max(d1, d2);

    if ( max_dev < 0.5 )
        return 2;
    if ( max_dev < 2.0 )
        return base_steps / 4;
    if ( max_dev < 10.0 )
        return base_steps / 2;
    return base_steps;
}

// Convert a Bezier path to a Clipper2 polygon
Path64 bezier_to_polygon(const math::bezier::Bezier& bez, int precision)
{
    Path64 path;
    if ( bez.size() < 2 )
        return path;

    path.push_back(to_clipper(bez[0].pos));

    int seg_count = bez.closed() ? bez.size() : bez.size() - 1;
    for ( int i = 0; i < seg_count; i++ )
    {
        int next = (i + 1) % bez.size();
        QPointF p0 = bez[i].pos;
        QPointF p1 = bez[i].tan_out;
        QPointF p2 = bez[next].tan_in;
        QPointF p3 = bez[next].pos;

        // Check if segment is a straight line
        bool is_line = (p1 == p0 || (p1 - p0).manhattanLength() < 0.01) &&
                       (p2 == p3 || (p2 - p3).manhattanLength() < 0.01);

        if ( is_line )
        {
            path.push_back(to_clipper(p3));
        }
        else
        {
            int steps = adaptive_steps(p0, p1, p2, p3, precision);
            flatten_segment(p0, p1, p2, p3, path, steps);
        }
    }

    return path;
}

// Fit a cubic bezier to a sequence of points using least-squares
// For simplicity, we use a polyline-to-bezier approach:
// each consecutive run of 3+ points becomes a cubic segment
void polygon_to_bezier_simple(const Path64& poly, math::bezier::Bezier& out_bez)
{
    if ( poly.size() < 2 )
        return;

    // Simple approach: treat the polygon as a series of line segments
    // with corner nodes. This preserves shape exactly.
    out_bez.clear();

    QPointF first = from_clipper(poly[0]);
    out_bez.push_back(math::bezier::Point(first));

    for ( size_t i = 1; i < poly.size(); i++ )
    {
        QPointF p = from_clipper(poly[i]);
        // Skip near-duplicate points
        if ( (p - from_clipper(poly[i-1])).manhattanLength() < 0.1 )
            continue;
        out_bez.push_back(math::bezier::Point(p));
    }

    // Simplify: merge collinear points to reduce node count
    // A point is collinear if the angle between prev→point and point→next is ~180°
    if ( out_bez.size() > 3 )
    {
        math::bezier::Bezier simplified;
        simplified.push_back(out_bez[0]);

        for ( int i = 1; i < out_bez.size() - 1; i++ )
        {
            QPointF prev = out_bez[i-1].pos;
            QPointF curr = out_bez[i].pos;
            QPointF next = out_bez[i+1].pos;

            QPointF d1 = curr - prev;
            QPointF d2 = next - curr;
            double cross = d1.x() * d2.y() - d1.y() * d2.x();
            double dot = d1.x() * d2.x() + d1.y() * d2.y();
            double len1 = std::sqrt(QPointF::dotProduct(d1, d1));
            double len2 = std::sqrt(QPointF::dotProduct(d2, d2));

            // Keep point if angle deviation > ~3 degrees
            if ( len1 > 0.01 && len2 > 0.01 )
            {
                double sin_angle = std::abs(cross) / (len1 * len2);
                if ( sin_angle < 0.05 ) // ~3 degrees
                    continue;
            }

            simplified.push_back(out_bez[i]);
        }

        simplified.push_back(out_bez[out_bez.size() - 1]);
        out_bez = simplified;
    }

    out_bez.set_closed(true);
}

ClipType to_clipper_op(gui::tools::BooleanOp op)
{
    switch ( op )
    {
        case gui::tools::BooleanOp::Union:        return ClipType::Union;
        case gui::tools::BooleanOp::Difference:   return ClipType::Difference;
        case gui::tools::BooleanOp::Intersection: return ClipType::Intersection;
        case gui::tools::BooleanOp::Exclusion:    return ClipType::Xor;
    }
    return ClipType::Union;
}

} // anonymous namespace


std::vector<math::bezier::Bezier> gui::tools::path_boolean(
    const std::vector<math::bezier::Bezier>& subject,
    const std::vector<math::bezier::Bezier>& clip,
    BooleanOp op,
    int precision)
{
    Paths64 subject_polys;
    Paths64 clip_polys;

    for ( const auto& bez : subject )
    {
        auto poly = bezier_to_polygon(bez, precision);
        if ( !poly.empty() )
            subject_polys.push_back(std::move(poly));
    }

    for ( const auto& bez : clip )
    {
        auto poly = bezier_to_polygon(bez, precision);
        if ( !poly.empty() )
            clip_polys.push_back(std::move(poly));
    }

    // Execute boolean operation
    Paths64 result = Clipper2Lib::BooleanOp(to_clipper_op(op), FillRule::NonZero, subject_polys, clip_polys);

    // Convert result back to bezier paths
    std::vector<math::bezier::Bezier> output;
    for ( const auto& poly : result )
    {
        math::bezier::Bezier bez;
        polygon_to_bezier_simple(poly, bez);
        if ( bez.size() >= 2 )
            output.push_back(std::move(bez));
    }

    return output;
}
