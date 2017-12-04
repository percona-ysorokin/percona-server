/* Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software Foundation,
  51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#ifndef UNITTEST_GUNIT_GIS_PROJECT_ON_POLE_H_INCLUDED
#define UNITTEST_GUNIT_GIS_PROJECT_ON_POLE_H_INCLUDED

#include <cmath>  // M_PI_2

#include "sql/gis/geometries_cs.h"  // gis::{Cartesian_*,Geographic_*}

namespace {

/**
 * Convert to polar coordinates (-y as polar axis), interpret as sphere north
 * pole azimuthal equidistant coordinates, then unproject to latiude /
 * longtitude. This transformation does not preserve any properties, and in
 * particular, straight lines do not become geodesics unless going through
 * cartesian point (0,0), mapped to the north pole. Intersections should happen
 * at cartesian (0,0) to be guaranteed preseved.
 *
 * A better projection would be something like polar gnomonic but for an
 * ellipsoid surface. Such a projection would ensure straight lines get mapped
 * to geodesics. However, such a projection seems impossible.
 */
gis::Geographic_point gis_project_on_pole(const gis::Cartesian_point& cart) {
  using std::atan2;
  using std::hypot;

  auto x = cart.x();
  auto y = cart.y();
  // Input point should be inside radius pi/2 of origin. Otherwise
  // clockwisedness of rings would not be preserved.
  assert(hypot(x, y) <= M_PI_2);
  // Rotate 45deg and project, so that -y points along the prime meridian and
  // origin is on north pole, convert to polar, and translate origin to
  // equator.
  return {atan2(-y, x), (M_PI_2 - hypot(-y, x))};
}

inline gis::Geographic_linestring gis_project_on_pole(
    const gis::Cartesian_linestring& cart) {
  gis::Geographic_linestring geom;
  for (size_t i = 0; i < cart.size(); i++) {
    geom.push_back(gis_project_on_pole(cart[i]));
  }
  return geom;
}

}  // namespace

#endif  // include guard
