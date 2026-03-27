/*
 * SPDX-FileCopyrightText: 2024 Glaxnimate Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "math/bezier/bezier.hpp"
#include <vector>

namespace glaxnimate::gui::tools {

enum class BooleanOp
{
    Union,
    Difference,
    Intersection,
    Exclusion
};

/**
 * @brief Perform a boolean operation on two sets of bezier paths
 * @param subject   The subject paths (first selected shape)
 * @param clip      The clip paths (second selected shape)
 * @param op        The boolean operation to perform
 * @param precision Curve flattening precision (samples per segment)
 * @return The resulting bezier paths
 */
std::vector<math::bezier::Bezier> path_boolean(
    const std::vector<math::bezier::Bezier>& subject,
    const std::vector<math::bezier::Bezier>& clip,
    BooleanOp op,
    int precision = 64
);

} // namespace glaxnimate::gui::tools
