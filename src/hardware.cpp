// Licensed under a 3-clause BSD style license - see LICENSE.rst

#include <hardware.h>

#include <cmath>
#include <iostream>
#include <algorithm>
#include <sstream>

#ifdef _OPENMP
#  include <omp.h>
#endif

namespace fba = fiberassign;

namespace fbg = fiberassign::geom;


fba::Hardware::Hardware(std::string const & timestr,
                        std::vector <int32_t> const & location,
                        std::vector <int32_t> const & petal,
                        std::vector <int32_t> const & device,
                        std::vector <int32_t> const & slitblock,
                        std::vector <int32_t> const & blockfiber,
                        std::vector <int32_t> const & fiber,
                        std::vector <std::string> const & device_type,
                        std::vector <double> const & x_mm,
                        std::vector <double> const & y_mm,
                        std::vector <int32_t> const & status,
                        std::vector <double> const & theta_offset,
                        std::vector <double> const & theta_min,
                        std::vector <double> const & theta_max,
                        std::vector <double> const & theta_arm,
                        std::vector <double> const & phi_offset,
                        std::vector <double> const & phi_min,
                        std::vector <double> const & phi_max,
                        std::vector <double> const & phi_arm,
                        std::vector <double> const & ps_radius,
                        std::vector <double> const & ps_theta,
                        std::vector <fbg::shape> const & excl_theta,
                        std::vector <fbg::shape> const & excl_phi,
                        std::vector <fbg::shape> const & excl_gfa,
                        std::vector <fbg::shape> const & excl_petal) {
    nloc = location.size();

    fba::Logger & logger = fba::Logger::get();
    std::ostringstream logmsg;

    timestr_ = timestr;

    ps_radius_ = ps_radius;
    ps_theta_ = ps_theta;

    int32_t maxpetal = 0;
    for (auto const & p : petal) {
        if (p > maxpetal) {
            maxpetal = p;
        }
    }
    npetal = static_cast <size_t> (maxpetal + 1);

    loc_pos_xy_mm.clear();
    loc_petal.clear();
    loc_device.clear();
    loc_device_type.clear();
    loc_fiber.clear();
    loc_slitblock.clear();
    loc_blockfiber.clear();
    locations.resize(0);
    state.clear();
    loc_theta_offset.clear();
    loc_theta_min.clear();
    loc_theta_max.clear();
    loc_theta_arm.clear();
    loc_phi_offset.clear();
    loc_phi_min.clear();
    loc_phi_max.clear();
    loc_phi_arm.clear();
    loc_theta_excl.clear();
    loc_phi_excl.clear();
    petal_edge.clear();
    gfa_edge.clear();

    petal_locations.clear();
    for (int32_t i = 0; i < npetal; ++i) {
        petal_locations[i].resize(0);
    }

    int32_t stcount = 0;

    for (int32_t i = 0; i < nloc; ++i) {
        locations.push_back(location[i]);
        loc_petal[location[i]] = petal[i];
        loc_device[location[i]] = device[i];
        loc_device_type[location[i]] = device_type[i];
        loc_fiber[location[i]] = fiber[i];
        loc_slitblock[location[i]] = slitblock[i];
        loc_blockfiber[location[i]] = blockfiber[i];
        petal_locations[petal[i]].push_back(location[i]);
        loc_pos_xy_mm[location[i]] = std::make_pair(x_mm[i], y_mm[i]);
        state[location[i]] = status[i];
        if (status[i] != FIBER_STATE_OK) {
            stcount++;
        }
        neighbors[location[i]].clear();
        petal_edge[location[i]] = false;
        gfa_edge[location[i]] = false;
        loc_theta_offset[location[i]] = theta_offset[i] * M_PI / 180.0;
        loc_theta_min[location[i]] = theta_min[i] * M_PI / 180.0;
        loc_theta_max[location[i]] = theta_max[i] * M_PI / 180.0;
        loc_theta_arm[location[i]] = theta_arm[i];
        loc_phi_offset[location[i]] = phi_offset[i] * M_PI / 180.0;
        loc_phi_min[location[i]] = phi_min[i] * M_PI / 180.0;
        loc_phi_max[location[i]] = phi_max[i] * M_PI / 180.0;
        loc_phi_arm[location[i]] = phi_arm[i];
        loc_theta_excl[location[i]] = excl_theta[i];
        loc_phi_excl[location[i]] = excl_phi[i];
        loc_gfa_excl[location[i]] = excl_gfa[i];
        loc_petal_excl[location[i]] = excl_petal[i];
    }

    logmsg.str("");
    logmsg << "Focalplane has " << stcount
        << " fibers that are stuck / broken";
    logger.info(logmsg.str().c_str());

    // Sort the locations
    std::stable_sort(locations.begin(), locations.end());
    for (int32_t i = 0; i < npetal; ++i) {
        std::stable_sort(petal_locations[i].begin(), petal_locations[i].end());
    }

    // Hard-coded parameters.  These could be moved to desimodel and passed
    // into this constructor as arguments.

    // The number of science positioners per petal.
    nfiber_petal = 500;

    // The tile / focalplane radius in degrees, used for selecting targets
    // that are available to a particular tile.
    focalplane_radius_deg = 1.65;

    // The radius in mm on the focalplane for considering which positioners
    // are "neighbors".
    neighbor_radius_mm = 14.05;

    // The amount to reduce the total arm length when considering which
    // targets are reachable by a positioner.  This was set to 200 microns
    // long ago...
    patrol_buffer_mm = 0.2;

    // Compute neighboring locations
    for (int32_t x = 0; x < nloc; ++x) {
        int32_t xid = locations[x];
        for (int32_t y = x + 1; y < nloc; ++y) {
            int32_t yid = locations[y];
            double dist = fbg::dist(loc_pos_xy_mm[xid],
                                    loc_pos_xy_mm[yid]);
            if (dist <= neighbor_radius_mm) {
                neighbors[xid].push_back(yid);
                neighbors[yid].push_back(xid);
            }
        }
    }

    // For each location, we rotate the petal and GFA exclusion polygons
    // to the correct petal location.

    for (auto const & lid : locations) {
        int32_t pt = loc_petal[lid];
        double petalrot_deg = fmod((double)(7 + pt) * 36.0, 360.0);
        double petalrot_rad = petalrot_deg * M_PI / 180.0;
        auto csang = std::make_pair(cos(petalrot_rad), sin(petalrot_rad));
        loc_gfa_excl.at(lid).rotation_origin(csang);
        loc_petal_excl.at(lid).rotation_origin(csang);
    }

}


std::string fba::Hardware::time() const {
    return timestr_;
}


std::vector <int32_t> fba::Hardware::device_locations(
        std::string const & type) const {
    std::vector <int32_t> ret;
    for (auto const & fid : locations) {
        if (type.compare(loc_device_type.at(fid)) == 0) {
            ret.push_back(fid);
        }
    }
    return ret;
}


// Returns the radial distance on the focalplane (mm) given the angle,
// theta (radians).  This is simply a fit to the data provided.
double fba::Hardware::radial_ang2dist (double const & theta_rad) const {
    const double p[4] = {8.297e5, -1750., 1.394e4, 0.0};
    double dist_mm = 0.0;
    for (size_t i = 0; i < 4; ++i) {
        dist_mm = theta_rad * dist_mm + p[i];
    }
    return dist_mm;
}


// Returns the radial angle (theta) on the focalplane given the distance (mm)
double fba::Hardware::radial_dist2ang (double const & dist_mm) const {
    double delta_theta = 1e-4;
    double inv_delta = 1.0 / delta_theta;

    // starting guess
    double theta_rad = 0.01;

    double distcur;
    double distdelta;
    double correction;
    double error = 1.0;

    while (::abs(error) > 1e-7) {
        distcur = radial_ang2dist(theta_rad);
        distdelta = radial_ang2dist(theta_rad + delta_theta);
        error = distcur - dist_mm;
        correction = error / (inv_delta * (distdelta - distcur));
        theta_rad -= correction;
    }
    return theta_rad;
}


fiberassign::geom::dpair fba::Hardware::radec2xy(
    double const & tilera, double const & tiledec, double const & tiletheta,
    double const & ra, double const & dec) const {

    double deg_to_rad = M_PI / 180.0;

    // Inclination is 90 degrees minus the declination in degrees
    double inc_rad = (90.0 - dec) * deg_to_rad;

    double ra_rad = ra * deg_to_rad;
    //double dec_rad = dec * deg_to_rad;
    double tilera_rad = tilera * deg_to_rad;
    double tiledec_rad = tiledec * deg_to_rad;
    double tiletheta_rad = tiletheta * deg_to_rad;

    double sin_inc_rad = ::sin(inc_rad);
    double x0 = sin_inc_rad * ::cos(ra_rad);
    double y0 = sin_inc_rad * ::sin(ra_rad);
    double z0 = ::cos(inc_rad);

    double cos_tilera_rad = ::cos(tilera_rad);
    double sin_tilera_rad = ::sin(tilera_rad);
    double x1 = cos_tilera_rad * x0 + sin_tilera_rad * y0;
    double y1 = -sin_tilera_rad * x0 + cos_tilera_rad * y0;
    double z1 = z0;

    double cos_tiledec_rad = ::cos(tiledec_rad);
    double sin_tiledec_rad = ::sin(tiledec_rad);
    double x = cos_tiledec_rad * x1 + sin_tiledec_rad * z1;
    double y = y1;
    double z = -sin_tiledec_rad * x1 + cos_tiledec_rad * z1;

    double ra_ang_rad = ::atan2(y, x);
    if (ra_ang_rad < 0) {
        ra_ang_rad = 2.0 * M_PI + ra_ang_rad;
    }

    double dec_ang_rad = (M_PI / 2)
        - ::acos(z / ::sqrt((x*x) + (y*y) + (z*z)) );

    double radius_rad = 2 *
        ::asin( ::sqrt( ::pow( ::sin(dec_ang_rad / 2), 2) +
        ::cos(dec_ang_rad) * ::pow( ::sin(ra_ang_rad / 2), 2) ) );

    double q_rad = ::atan2(z, -y);

    double radius_mm = radial_ang2dist(radius_rad);

    // Apply field rotation
    double rotated = q_rad + tiletheta_rad;

    double x_focalplane = radius_mm * ::cos(rotated);
    double y_focalplane = radius_mm * ::sin(rotated);

    return std::make_pair(x_focalplane, y_focalplane);
}


void fba::Hardware::radec2xy_multi(
    double const & tilera, double const & tiledec, double const & tiletheta,
    std::vector <double> const & ra,
    std::vector <double> const & dec,
    std::vector <std::pair <double, double> > & xy, int threads) const {

    size_t ntg = ra.size();
    xy.resize(ntg);

    int max_threads = 1;
    #ifdef _OPENMP
    max_threads = omp_get_num_threads();
    #endif
    int run_threads;
    if (threads > 0) {
        run_threads = threads;
    } else {
        run_threads = max_threads;
    }
    if (run_threads > max_threads) {
        run_threads = max_threads;
    }

    #pragma omp parallel for schedule(static) default(none) shared(ntg, tilera, tiledec, tiletheta, xy, ra, dec) num_threads(run_threads)
    for (size_t t = 0; t < ntg; ++t) {
        xy[t] = radec2xy(tilera, tiledec, tiletheta, ra[t], dec[t]);
    }

    return;
}


fiberassign::geom::dpair fba::Hardware::xy2radec(
        double const & tilera, double const & tiledec,
        double const & tiletheta,
        double const & x_mm, double const & y_mm) const {

    double deg_to_rad = M_PI / 180.0;
    double rad_to_deg = 180.0 / M_PI;

    double tilera_rad = tilera * deg_to_rad;
    double tiledec_rad = tiledec * deg_to_rad;
    double tiletheta_rad = tiletheta * deg_to_rad;

    // radial distance on the focal plane
    double radius_mm = ::sqrt(x_mm * x_mm + y_mm * y_mm);
    double radius_rad = radial_dist2ang(radius_mm);

    // q is the angle the position makes with the +x-axis of focal plane
    double rotated = ::atan2(y_mm, x_mm);

    // Remove field rotation
    double q_rad = rotated - tiletheta_rad;

    // The focal plane is oriented with +yfocal = +dec but +xfocal = -RA
    // Rotate clockwise around z by r_rad

    double x1 = ::cos(radius_rad);     // y0=0 so drop sin(r_rad) term
    double y1 = -::sin(radius_rad);    // y0=0 so drop cos(r_rad) term
    //double z1 = 0.0;

    // clockwise rotation around the x-axis

    double x2 = x1;
    double y2 = y1 * ::cos(q_rad);    // z1=0 so drop sin(q_rad) term
    double z2 = -y1 * ::sin(q_rad);

    double cos_tiledec = ::cos(tiledec_rad);
    double sin_tiledec = ::sin(tiledec_rad);
    double cos_tilera = ::cos(tilera_rad);
    double sin_tilera = ::sin(tilera_rad);

    // Clockwise rotation around y axis by declination of the tile center

    double x3 = cos_tiledec * x2 - sin_tiledec * z2;
    double y3 = y2;
    double z3 = sin_tiledec * x2 + cos_tiledec * z2;

    // Counter-clockwise rotation around the z-axis by the right
    // ascension of the tile center
    double x4 = cos_tilera * x3 - sin_tilera * y3;
    double y4 = sin_tilera * x3 + cos_tilera * y3;
    double z4 = z3;

    double ra_rad = ::atan2(y4, x4);
    if (ra_rad < 0.0) {
        ra_rad = 2.0 * M_PI + ra_rad;
    }

    double dec_rad = M_PI_2 - ::acos(z4);

    double ra = ::fmod( (ra_rad * rad_to_deg), 360.0);
    double dec = dec_rad * rad_to_deg;

    return std::make_pair(ra, dec);
}


void fba::Hardware::xy2radec_multi(
        double const & tilera, double const & tiledec,
        double const & tiletheta,
        std::vector <double> const & x_mm, std::vector <double> const & y_mm,
        std::vector <std::pair <double, double> > & radec, int threads) const {
    size_t npos = x_mm.size();
    radec.resize(npos);

    int max_threads = 1;
    #ifdef _OPENMP
    max_threads = omp_get_num_threads();
    #endif
    int run_threads;
    if (threads > 0) {
        run_threads = threads;
    } else {
        run_threads = max_threads;
    }
    if (run_threads > max_threads) {
        run_threads = max_threads;
    }

    #pragma omp parallel for schedule(static) default(none) shared(npos, tilera, tiledec, tiletheta, x_mm, y_mm, radec) num_threads(run_threads)
    for (size_t i = 0; i < npos; ++i) {
        radec[i] = xy2radec(tilera, tiledec, tiletheta, x_mm[i], y_mm[i]);
    }

    return;
}


bool _check_angle_range(
        double & ang, double ang_zero,
        double ang_min, double ang_max) {
    double twopi = 2.0 * M_PI;
    double abs_min = ang_zero + ang_min;
    double abs_max = ang_zero + ang_max;
    if (ang < abs_min) {
        ang += twopi;
    }
    if (ang > abs_max) {
        ang -= twopi;
    }
    if ((ang < abs_min) || (ang > abs_max)) {
        // Out of range
        return true;
    } else {
        return false;
    }
}


bool fba::Hardware::move_positioner_thetaphi(
        fbg::shape & shptheta, fbg::shape & shpphi,
        fbg::dpair const & center, double theta, double phi,
        double theta_arm, double phi_arm, double theta_zero, double phi_zero,
        double theta_min, double phi_min, double theta_max, double phi_max
        ) const {
    // Check that requested angles are in range.
    bool bad_phi = _check_angle_range(phi, phi_zero, phi_min, phi_max);
    bool bad_theta = _check_angle_range(
        theta, theta_zero, theta_min, theta_max
    );
    if (bad_phi || bad_theta) {
        return true;
    }

    double ctheta = ::cos(theta);
    double stheta = ::sin(theta);
    double cphi = ::cos(phi);
    double sphi = ::sin(phi);
    auto cstheta = std::make_pair(ctheta, stheta);
    auto csphi = std::make_pair(cphi, sphi);

    // Move the phi polygon into the fully extended position along the X axis.
    shpphi.transl(std::make_pair(theta_arm, 0.0));

    // std::cout << "move_positioner_thetaphi:  after transl:" << std::endl;
    // shptheta.print();
    // shpphi.print();

    // Rotate fully extended positioner an angle of theta about the center.
    shptheta.rotation_origin(cstheta);
    shpphi.rotation_origin(cstheta);

    // std::cout << "move_positioner_thetaphi:  after rot origin:" << std::endl;
    // shptheta.print();
    // shpphi.print();

    // Rotate just the phi arm an angle phi about the theta arm center.
    shpphi.rotation(csphi);

    // std::cout << "move_positioner_thetaphi:  after phi rot of " << csphi.first << ", " << csphi.second << ":" << std::endl;
    // shptheta.print();
    // shpphi.print();

    // Translate the whole positioner to the center.
    shpphi.transl(center);
    shptheta.transl(center);

    // std::cout << "move_positioner_thetaphi:  after center transl:" << std::endl;
    // shptheta.print();
    // shpphi.print();

    return false;
}


bool fba::Hardware::xy_to_thetaphi(
        double & theta, double & phi,
        fbg::dpair const & center, fbg::dpair const & position,
        double theta_arm, double phi_arm, double theta_zero, double phi_zero,
        double theta_min, double phi_min, double theta_max, double phi_max
        ) const {
    fbg::dpair offset = std::make_pair(position.first - center.first,
                                       position.second - center.second);

    double sq_theta_arm = theta_arm * theta_arm;
    double sq_phi_arm = phi_arm * phi_arm;
    double sq_offset = fbg::sq(offset);
    double sq_total_arm = fbg::sq(theta_arm + phi_arm);
    double sq_diff_arm = fbg::sq(theta_arm - phi_arm);

    phi = M_PI;
    theta = 0.0;

    if (fabs(sq_offset - sq_total_arm) <=
        std::numeric_limits<float>::epsilon()) {
        // We are at the maximum arm extension.  Force phi angle to zero
        // and compute theta.
        phi = 0.0;
        theta = ::atan2(offset.second, offset.first);
    } else if (fabs(sq_diff_arm - sq_offset) <=
        std::numeric_limits<float>::epsilon()) {
        // We are at the limit of the arm folded inwards.  Force phi angle
        // to PI and compute theta.
        phi = M_PI;
        theta = ::atan2(offset.second, offset.first);
    } else {
        // We are on neither limit.

        if (sq_total_arm < sq_offset) {
            // Physically impossible to get there for any choice of angles
            // std::cout << "xy_to_thetaphi: sqoffset - sqtot = " << sq_offset - sq_total_arm << std::endl;
            return true;
        }

        if (sq_offset < sq_diff_arm) {
            // Physically impossible to get there for any choice of angles
            // std::cout << "xy_to_thetaphi: sqdiff - sqoffset = " << sq_diff_arm - sq_offset << std::endl;
            return true;
        }

        // Use law of cosines to compute "opening" angle at the "elbow".
        double opening = ::acos((sq_theta_arm + sq_phi_arm - sq_offset)
                                / (2.0 * theta_arm * phi_arm));

        // std::cout << "xy_to_thetaphi: opening = " << opening << std::endl;

        // The PHI angle is just the supplement of this.
        phi = M_PI - opening;

        // std::cout << "xy_to_thetaphi: phi = " << phi << std::endl;

        // Compute the theta angle.
        // Use law of cosines to compute angle from theta arm to the line from
        // the origin to the X/Y position.
        double nrm_offset = ::sqrt(sq_offset);
        double txy = ::acos((sq_theta_arm + sq_offset - sq_phi_arm)
                            / (2 * theta_arm * nrm_offset));

        // std::cout << "xy_to_thetaphi: txy = " << txy << std::endl;

        theta = ::atan2(offset.second, offset.first) - txy;
    }

    // Check that angles are in range
    bool bad_phi = _check_angle_range(phi, phi_zero, phi_min, phi_max);
    bool bad_theta = _check_angle_range(
        theta, theta_zero, theta_min, theta_max
    );
    if (bad_phi || bad_theta) {
        return true;
    }

    return false;
}


bool fba::Hardware::move_positioner_xy(
        fbg::shape & shptheta, fbg::shape & shpphi,
        fbg::dpair const & center, fbg::dpair const & position,
        double theta_arm, double phi_arm, double theta_zero, double phi_zero,
        double theta_min, double phi_min, double theta_max, double phi_max
        ) const {

    double phi;
    double theta;
    bool fail = xy_to_thetaphi(
        theta, phi, center, position, theta_arm, phi_arm, theta_zero,
        phi_zero, theta_min, phi_min, theta_max, phi_max
    );
    if (fail) {
        return true;
    }
    return move_positioner_thetaphi(
        shptheta, shpphi, center, theta, phi,
        theta_arm, phi_arm, theta_zero, phi_zero,
        theta_min, phi_min, theta_max, phi_max
    );
}


bool fba::Hardware::position_xy_bad(int32_t loc, fbg::dpair const & xy) const {
    double phi;
    double theta;
    bool fail = xy_to_thetaphi(
        theta, phi,
        loc_pos_xy_mm.at(loc),
        xy,
        loc_theta_arm.at(loc),
        loc_phi_arm.at(loc),
        loc_theta_offset.at(loc),
        loc_phi_offset.at(loc),
        loc_theta_min.at(loc),
        loc_phi_min.at(loc),
        loc_theta_max.at(loc),
        loc_phi_max.at(loc)
    );
    return fail;
}


bool fba::Hardware::loc_position_xy(
    int32_t loc, fbg::dpair const & xy, fbg::shape & shptheta,
    fbg::shape & shpphi) const {

    // Start from exclusion polygon for this location.
    shptheta = loc_theta_excl.at(loc);
    shpphi = loc_phi_excl.at(loc);

    // std::cout << "loc_position_xy start" << std::endl;
    // shptheta.print();
    // shpphi.print();

    bool failed = move_positioner_xy(
        shptheta, shpphi,
        loc_pos_xy_mm.at(loc),
        xy,
        loc_theta_arm.at(loc),
        loc_phi_arm.at(loc),
        loc_theta_offset.at(loc),
        loc_phi_offset.at(loc),
        loc_theta_min.at(loc),
        loc_phi_min.at(loc),
        loc_theta_max.at(loc),
        loc_phi_max.at(loc)
    );

    return failed;
}


bool fba::Hardware::loc_position_thetaphi(
    int32_t loc, double theta, double phi, fbg::shape & shptheta,
    fbg::shape & shpphi) const {

    // Start from exclusion polygon for this location.
    shptheta = loc_theta_excl.at(loc);
    shpphi = loc_phi_excl.at(loc);

    bool failed = move_positioner_thetaphi(
        shptheta, shpphi,
        loc_pos_xy_mm.at(loc),
        theta, phi,
        loc_theta_arm.at(loc),
        loc_phi_arm.at(loc),
        loc_theta_offset.at(loc),
        loc_phi_offset.at(loc),
        loc_theta_min.at(loc),
        loc_phi_min.at(loc),
        loc_theta_max.at(loc),
        loc_phi_max.at(loc));

    return failed;
}


bool fba::Hardware::collide_xy(int32_t loc1, fbg::dpair const & xy1,
                               int32_t loc2, fbg::dpair const & xy2) const {

    fbg::shape shptheta1(loc_theta_excl.at(loc1));
    fbg::shape shpphi1(loc_phi_excl.at(loc1));
    bool failed1 = loc_position_xy(loc1, xy1, shptheta1, shpphi1);
    if (failed1) {
        // A positioner movement failure means that the angles needed to reach
        // the X/Y position are out of range.  While not strictly a collision,
        // it still means that we can't accept this positioner configuration.
        return true;
    }

    fbg::shape shptheta2(loc_theta_excl.at(loc2));
    fbg::shape shpphi2(loc_phi_excl.at(loc2));
    bool failed2 = loc_position_xy(loc2, xy2, shptheta2, shpphi2);
    if (failed2) {
        // A positioner movement failure means that the angles needed to reach
        // the X/Y position are out of range.  While not strictly a collision,
        // it still means that we can't accept this positioner configuration.
        return true;
    }

    // We were able to move positioners into place.  Now check for
    // intersections.

    if (fbg::intersect(shpphi1, shpphi2)) {
        return true;
    }
    if (fbg::intersect(shptheta1, shpphi2)) {
        return true;
    }
    if (fbg::intersect(shptheta2, shpphi1)) {
        return true;
    }

    return false;
}


bool fba::Hardware::collide_xy_edges(
        int32_t loc, fbg::dpair const & xy
    ) const {

    fbg::shape shptheta(loc_theta_excl.at(loc));
    fbg::shape shpphi(loc_phi_excl.at(loc));
    bool failed = loc_position_xy(loc, xy, shptheta, shpphi);
    if (failed) {
        // A positioner movement failure means that the angles needed to reach
        // the X/Y position are out of range.  While not strictly a collision,
        // it still means that we can't accept this positioner configuration.
        return true;
    }

    // We were able to move positioner into place.  Now check for
    // intersections with the GFA and petal boundaries.

    fbg::shape shpgfa(loc_gfa_excl.at(loc));
    fbg::shape shppetal(loc_petal_excl.at(loc));

    // The central body (theta arm) should never hit the GFA or petal edge,
    // so we only need to check the phi arm.

    if (fbg::intersect(shpphi, shpgfa)) {
        return true;
    }
    if (fbg::intersect(shpphi, shppetal)) {
        return true;
    }

    return false;
}


bool fba::Hardware::collide_thetaphi(
        int32_t loc1, double theta1, double phi1,
        int32_t loc2, double theta2, double phi2) const {

    fbg::shape shptheta1(loc_theta_excl.at(loc1));
    fbg::shape shpphi1(loc_phi_excl.at(loc1));
    bool failed1 = loc_position_thetaphi(loc1, theta1, phi1, shptheta1,
                                         shpphi1);
    if (failed1) {
        // A positioner movement failure means that the angles needed to reach
        // the X/Y position are out of range.  While not strictly a collision,
        // it still means that we can't accept this positioner configuration.
        return true;
    }

    fbg::shape shptheta2(loc_theta_excl.at(loc2));
    fbg::shape shpphi2(loc_phi_excl.at(loc2));
    bool failed2 = loc_position_thetaphi(loc2, theta2, phi2, shptheta2,
                                         shpphi2);
    if (failed2) {
        // A positioner movement failure means that the angles needed to reach
        // the X/Y position are out of range.  While not strictly a collision,
        // it still means that we can't accept this positioner configuration.
        return true;
    }

    // We were able to move positioners into place.  Now check for
    // intersections.

    if (fbg::intersect(shpphi1, shpphi2)) {
        return true;
    }
    if (fbg::intersect(shptheta1, shpphi2)) {
        return true;
    }
    if (fbg::intersect(shptheta2, shpphi1)) {
        return true;
    }

    return false;
}


std::vector <std::pair <bool, std::pair <fbg::shape, fbg::shape> > >
    fba::Hardware::loc_position_xy_multi(
        std::vector <int32_t> const & loc,
        std::vector <fbg::dpair> const & xy, int threads) const {
    size_t nlc = loc.size();
    std::vector <std::pair <bool, std::pair <fbg::shape, fbg::shape> > >
        result(nlc);

    int max_threads = 1;
    #ifdef _OPENMP
    max_threads = omp_get_num_threads();
    #endif
    int run_threads = 1;
    if (threads > 0) {
        run_threads = threads;
    } else {
        run_threads = max_threads;
    }
    if (run_threads > max_threads) {
        run_threads = max_threads;
    }

    #pragma omp parallel for schedule(static) default(none) shared(nlc, loc, xy, result) num_threads(run_threads)
    for (size_t f = 0; f < nlc; ++f) {
        fbg::shape & shptheta = result[f].second.first;
        fbg::shape & shpphi = result[f].second.second;
        result[f].first = loc_position_xy(loc[f], xy[f], shptheta, shpphi);
    }
    return result;
}


std::vector <std::pair <bool, std::pair <fbg::shape, fbg::shape> > >
    fba::Hardware::loc_position_thetaphi_multi(
        std::vector <int32_t> const & loc,
        std::vector <double> const & theta,
        std::vector <double> const & phi,
        int threads) const {
    size_t nlc = loc.size();
    std::vector <std::pair <bool, std::pair <fbg::shape, fbg::shape> > >
        result(nlc);

    int max_threads = 1;
    #ifdef _OPENMP
    max_threads = omp_get_num_threads();
    #endif
    int run_threads = 1;
    if (threads > 0) {
        run_threads = threads;
    } else {
        run_threads = max_threads;
    }
    if (run_threads > max_threads) {
        run_threads = max_threads;
    }

    #pragma omp parallel for schedule(static) default(none) shared(nlc, loc, theta, phi, result) num_threads(run_threads)
    for (size_t f = 0; f < nlc; ++f) {
        fbg::shape & shptheta = result[f].second.first;
        fbg::shape & shpphi = result[f].second.second;
        result[f].first = loc_position_thetaphi(loc[f], theta[f], phi[f],
                                                shptheta, shpphi);
    }
    return result;
}


std::vector <bool> fba::Hardware::check_collisions_xy(
    std::vector <int32_t> const & loc,
    std::vector <fbg::dpair> const & xy, int threads) const {

    size_t nlc = loc.size();

    auto fpos = loc_position_xy_multi(loc, xy, threads);

    std::map <int32_t, int32_t> loc_indx;

    // Build list of all location pairs to check for a collision
    std::map <int32_t, std::vector <int32_t> > checklookup;
    size_t idx = 0;
    for (auto const & lid : loc) {
        loc_indx[lid] = idx;
        for (auto const & nb : neighbors.at(lid)) {
            int32_t low;
            int32_t high;
            if (lid < nb) {
                low = lid;
                high = nb;
            } else {
                low = nb;
                high = lid;
            }
            if (checklookup.count(low) == 0) {
                checklookup[low].clear();
            }
            bool found = false;
            for (auto const & ck : checklookup[low]) {
                if (ck == high) {
                    found = true;
                }
            }
            if (! found) {
                checklookup[low].push_back(high);
            }
        }
        idx++;
    }
    std::vector <std::pair <int32_t, int32_t> > checkpairs;
    for (auto const & it : checklookup) {
        int32_t low = it.first;
        for (auto const & high : it.second) {
            checkpairs.push_back(std::make_pair(low, high));
        }
    }
    checklookup.clear();

    size_t npairs = checkpairs.size();

    std::vector <bool> result(nlc);
    result.assign(nlc, false);

    int max_threads = 1;
    #ifdef _OPENMP
    max_threads = omp_get_num_threads();
    #endif
    int run_threads = 1;
    if (threads > 0) {
        run_threads = threads;
    } else {
        run_threads = max_threads;
    }
    if (run_threads > max_threads) {
        run_threads = max_threads;
    }

    #pragma omp parallel for schedule(static) default(none) shared(npairs, checkpairs, fpos, result, loc_indx) num_threads(run_threads)
    for (size_t p = 0; p < npairs; ++p) {
        int32_t flow = checkpairs[p].first;
        int32_t fhigh = checkpairs[p].second;

        bool failed1 = fpos[loc_indx.at(flow)].first;
        fbg::shape const & shptheta1 = fpos[loc_indx.at(flow)].second.first;
        fbg::shape const &shpphi1 = fpos[loc_indx.at(flow)].second.second;
        bool failed2 = fpos[loc_indx.at(fhigh)].first;
        fbg::shape const & shptheta2 = fpos[loc_indx.at(fhigh)].second.first;
        fbg::shape const & shpphi2 = fpos[loc_indx.at(fhigh)].second.second;

        bool hit = false;
        if (failed1 || failed2) {
            hit = true;
        } else if (fbg::intersect(shpphi1, shpphi2)) {
            hit = true;
        } else if (fbg::intersect(shptheta1, shpphi2)) {
            hit = true;
        } else if (fbg::intersect(shptheta2, shpphi1)) {
            hit = true;
        }
        if (hit) {
            #pragma omp critical
            {
                result[loc_indx[flow]] = true;
                result[loc_indx[fhigh]] = true;
            }
        }
    }
    return result;
}


std::vector <bool> fba::Hardware::check_collisions_thetaphi(
    std::vector <int32_t> const & loc,
    std::vector <double> const & theta,
    std::vector <double> const & phi, int threads) const {

    size_t nlc = loc.size();

    auto fpos = loc_position_thetaphi_multi(loc, theta, phi, threads);

    std::map <int32_t, int32_t> loc_indx;

    // Build list of all location pairs to check for a collision
    std::map <int32_t, std::vector <int32_t> > checklookup;
    size_t idx = 0;
    for (auto const & lid : loc) {
        loc_indx[lid] = idx;
        for (auto const & nb : neighbors.at(lid)) {
            int32_t low;
            int32_t high;
            if (lid < nb) {
                low = lid;
                high = nb;
            } else {
                low = nb;
                high = lid;
            }
            if (checklookup.count(low) == 0) {
                checklookup[low].clear();
            }
            bool found = false;
            for (auto const & ck : checklookup[low]) {
                if (ck == high) {
                    found = true;
                }
            }
            if (! found) {
                checklookup[low].push_back(high);
            }
        }
        idx++;
    }
    std::vector <std::pair <int32_t, int32_t> > checkpairs;
    for (auto const & it : checklookup) {
        int32_t low = it.first;
        for (auto const & high : it.second) {
            checkpairs.push_back(std::make_pair(low, high));
        }
    }
    checklookup.clear();

    size_t npairs = checkpairs.size();

    std::vector <bool> result(nlc);
    result.assign(nlc, false);

    int max_threads = 1;
    #ifdef _OPENMP
    max_threads = omp_get_num_threads();
    #endif
    int run_threads = 1;
    if (threads > 0) {
        run_threads = threads;
    } else {
        run_threads = max_threads;
    }
    if (run_threads > max_threads) {
        run_threads = max_threads;
    }

    #pragma omp parallel for schedule(static) default(none) shared(npairs, checkpairs, fpos, result, loc_indx, theta, phi) num_threads(run_threads)
    for (size_t p = 0; p < npairs; ++p) {
        int32_t flow = checkpairs[p].first;
        int32_t fhigh = checkpairs[p].second;

        bool failed1 = fpos[loc_indx.at(flow)].first;
        fbg::shape const & shptheta1 = fpos[loc_indx.at(flow)].second.first;
        fbg::shape const & shpphi1 = fpos[loc_indx.at(flow)].second.second;
        bool failed2 = fpos[loc_indx.at(fhigh)].first;
        fbg::shape const & shptheta2 = fpos[loc_indx.at(fhigh)].second.first;
        fbg::shape const & shpphi2 = fpos[loc_indx.at(fhigh)].second.second;

        bool hit = false;
        if (failed1 || failed2) {
            hit = true;
        } else if (fbg::intersect(shpphi1, shpphi2)) {
            hit = true;
        } else if (fbg::intersect(shptheta1, shpphi2)) {
            hit = true;
        } else if (fbg::intersect(shptheta2, shpphi1)) {
            hit = true;
        }
        if (hit) {
            #pragma omp critical
            {
                result[loc_indx[flow]] = true;
                result[loc_indx[fhigh]] = true;
            }
        }
    }
    return result;
}
