#ifndef GLOBAL_H
#define GLOBAL_H

#include	<cstdlib>
#include	<cmath>
#include	<fstream>
#include	<sstream>
#include	<iostream>
#include	<iomanip>
#include	<string>
#include	<vector>
#include	<algorithm>
#include	<exception>
#include	<sys/time.h>
#include        "omp.h"
#include        "misc.h"
#include        "feat.h"
#include        "structs.h"

#ifdef FITS
#include        "fitsio.h"
#endif

// Collect -----------------------------------------------------------
// From the HTM tree, collects for each fiber and for each plate, the available galaxies
void collect_galaxies_for_all(const MTL& M, const htmTree<struct target>& T, Plates& P, const PP& pp, const Feat& F);

// From the previous computations, computes the inverse map, that is to say, for each target, computes the tile-fibers which can reach it
void collect_available_tilefibers(MTL& M, const Plates& P, const Feat& F);

// Assignment functions ----------------------------------------------
// First simple assignment plan, executing find_best on every plate on every fiber
void simple_assign(const MTL& M, const Plates& P, const PP& pp, const Feat& F, Assignment& A, int next=-1);

// More fine first assignment plan, 
void new_assign_fibers(const MTL& M, const Plates& P, const PP& pp, const Feat& F, Assignment& A, int next=-1);

void improve(const MTL& M, const Plates&P, const PP& pp, const Feat& F, Assignment& A, int next=-1);

void improve_from_kind(const MTL& M, const Plates&P, const PP& pp, const Feat& F, Assignment& A, str kind, int next=-1);

void update_plan_from_one_obs(const MTL& M, const Plates&P, const PP& pp, const Feat& F, Assignment& A, int end);

void redistribute_tf(const MTL& M, const Plates&P, const PP& pp, const Feat& F, Assignment& A, int next=-1);

void assign_sf_ss(int j, const MTL& M, const Plates& P, const PP& pp, const Feat& F, Assignment& A);

void assign_unused(int j, const MTL& M, const Plates& P, const PP& pp, const Feat& F, Assignment& A);

// Results functions --------------------------------------------------
void results_on_inputs(str outdir, const MTL& M, const Plates& P, const Feat& F, bool latex=false);

void display_results(str outdir, const MTL& M, const Plates &P, const PP& pp, Feat& F, const Assignment& A, bool latex=false);

void write_FAtile_ascii(int j, str outdir, const MTL& M, const Plates& P, const PP& pp, const Feat& F, const Assignment& A);

void fa_write(int j, const char *filename, const MTL& M, const Plates& P, const PP& pp, const Feat& F, const Assignment& A);

void pyplotTile(int j, str fname, const MTL& M, const Plates& P, const PP& pp, const Feat& F, const Assignment& A);

void overlappingTiles(str fname, const Feat& F, const Assignment& A);
#endif
