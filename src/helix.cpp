/***************************************************************************
 *
 * Author: "Shaoda He"
 * MRC Laboratory of Molecular Biology
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This complete copyright notice must be included in any revised version of the
 * source code. Additional authorship citations may be added, but existing
 * author citations must be preserved.
 ***************************************************************************/

#include "src/helix.h"

//#define DEBUG_SEARCH_HELICAL_SYMMETRY

void makeHelicalSymmetryList(
		std::vector<HelicalSymmetryItem>& list,
		RFLOAT rise_min_pix,
		RFLOAT rise_max_pix,
		RFLOAT rise_step_pix,
		bool search_rise,
		RFLOAT twist_min_deg,
		RFLOAT twist_max_deg,
		RFLOAT twist_step_deg,
		bool search_twist)
{
	// Assume all parameters are within range
	RFLOAT rise_pix, twist_deg;
	int rise_samplings, twist_samplings;
	std::vector<HelicalSymmetryItem> tmp_list;

	if (search_rise)
		rise_samplings = ROUND(fabs(rise_max_pix - rise_min_pix) / fabs(rise_step_pix));
	else
	{
		rise_min_pix = (rise_min_pix + rise_max_pix) / 2.;
		rise_samplings = 0;
	}
	if (search_twist)
		twist_samplings = ROUND(fabs(twist_max_deg - twist_min_deg) / fabs(twist_step_deg));
	else
	{
		twist_min_deg = (twist_min_deg + twist_max_deg) / 2.;
		twist_samplings = 0;
	}

	// Store a matrix of symmetries
	tmp_list.clear();
	for (int ii = 0; ii <= rise_samplings; ii++)
	{
		for (int jj = 0; jj <= twist_samplings; jj++)
		{
			rise_pix = rise_min_pix + rise_step_pix * RFLOAT(ii);
			twist_deg = twist_min_deg + twist_step_deg * RFLOAT(jj);
			tmp_list.push_back(HelicalSymmetryItem(twist_deg, rise_pix));
		}
	}

	// Check duplications and return this matrix
	RFLOAT twist_dev_deg, twist_avg_deg, rise_dev_pix, rise_avg_pix;
	RFLOAT err_max = (1e-10);
	bool same_twist, same_rise;
	for (int ii = 0; ii < list.size(); ii++)
	{
		for (int jj = 0; jj < tmp_list.size(); jj++)
		{
			same_twist = same_rise = false;

			twist_dev_deg = fabs(list[ii].twist_deg - tmp_list[jj].twist_deg);
			rise_dev_pix = fabs(list[ii].rise_pix - tmp_list[jj].rise_pix);
			twist_avg_deg = (fabs(list[ii].twist_deg) + fabs(tmp_list[jj].twist_deg)) / 2.;
			rise_avg_pix = (fabs(list[ii].rise_pix) + fabs(tmp_list[jj].rise_pix)) / 2.;

			if (twist_avg_deg < err_max)
			{
				if (twist_dev_deg < err_max)
					same_twist = true;
			}
			else
			{
				if ((twist_dev_deg / twist_avg_deg) < err_max)
					same_twist = true;
			}

			if (rise_avg_pix < err_max)
			{
				if (rise_dev_pix < err_max)
					same_rise = true;
			}
			else
			{
				if ( (rise_dev_pix / rise_avg_pix) < err_max)
					same_rise = true;
			}

			if (same_twist && same_rise)
				tmp_list[jj].dev = list[ii].dev;
		}
	}
	list.clear();
	list = tmp_list;
	return;
};

bool calcCCofHelicalSymmetry(
		const MultidimArray<RFLOAT>& v,
		RFLOAT r_min_pix,
		RFLOAT r_max_pix,
		RFLOAT z_percentage,
		RFLOAT rise_pix,
		RFLOAT twist_deg,
		RFLOAT& cc,
		int& nr_asym_voxels)
{
	// TODO: go through every line to find bugs!!!
	int rec_len, r_max_XY, startZ, finishZ;
	RFLOAT dist_r_pix, sum_pw1, sum_pw2;
	std::vector<RFLOAT> sin_rec, cos_rec, dev_voxel, dev_chunk;

	if ( (STARTINGZ(v) != FIRST_XMIPP_INDEX(ZSIZE(v))) || (STARTINGY(v) != FIRST_XMIPP_INDEX(YSIZE(v))) || (STARTINGX(v) != FIRST_XMIPP_INDEX(XSIZE(v))) )
		REPORT_ERROR("helix.cpp::calcCCofHelicalSymmetry(): The origin of input 3D MultidimArray is not at the center (use v.setXmippOrigin() before calling this function)!");

	// Check r_max
	r_max_XY = (XSIZE(v) < YSIZE(v)) ? XSIZE(v) : YSIZE(v);
	r_max_XY = (r_max_XY + 1) / 2 - 1;
	if ( r_max_pix > (((RFLOAT)(r_max_XY)) - 0.01) )  // 0.01 - avoid segmentation fault
		r_max_pix = (((RFLOAT)(r_max_XY)) - 0.01);

	// Set startZ and finishZ
	startZ = FLOOR( (-1.) * ((RFLOAT)(ZSIZE(v)) * z_percentage * 0.5) );
	finishZ = CEIL( ((RFLOAT)(ZSIZE(v))) * z_percentage * 0.5 );
	startZ = (startZ <= (STARTINGZ(v))) ? (STARTINGZ(v) + 1) : (startZ);
	finishZ = (finishZ >= (FINISHINGZ(v))) ? (FINISHINGZ(v) - 1) : (finishZ);

	// Calculate tabulated sine and cosine values
	rec_len = 2 + (CEIL((RFLOAT(ZSIZE(v)) + 2.) / rise_pix));
	sin_rec.clear();
	cos_rec.clear();
	sin_rec.resize(rec_len);
	cos_rec.resize(rec_len);
	for (int id = 0; id < rec_len; id++)
		sincos(DEG2RAD(((RFLOAT)(id)) * twist_deg), &sin_rec[id], &cos_rec[id]);

	rise_pix = fabs(rise_pix);

	// Test a chunk of Z length = rise
	dev_chunk.clear();
	// Iterate through all coordinates on Z, Y and then X axes
	FOR_ALL_ELEMENTS_IN_ARRAY3D(v)
	{
		RFLOAT xp, yp, zp, fx, fy, fz;

		// Test a chunk of Z length = rise
		// for(idz = startZ; (idz <= (startZ + ((int)(floor(rise_pix))))) && (idz <= finishZ); idz++)
		if ( (k < startZ) || (k > (startZ + (FLOOR(rise_pix)))) || (k > finishZ) )
			continue;

		dist_r_pix = sqrt(i * i + j * j);
		if ( (dist_r_pix < r_min_pix) || (dist_r_pix > r_max_pix) )
			continue;

		// Pick a voxel in the chunk
		dev_voxel.clear();
		dev_voxel.push_back(A3D_ELEM(v, k, i, j));

		// Pick other voxels according to this voxel and helical symmetry
		zp = k;
		int rot_id = 0;
		while (1)
		{
			// Rise
			zp += rise_pix;
			if (zp > finishZ) // avoid segmentation fault - finishZ is always strictly smaller than FINISHINGZ(v)!
				break;

			// Twist
			rot_id++;
			xp = ((RFLOAT)(j)) * cos_rec[rot_id] - ((RFLOAT)(i)) * sin_rec[rot_id];
			yp = ((RFLOAT)(j)) * sin_rec[rot_id] + ((RFLOAT)(i)) * cos_rec[rot_id];

			// Trilinear interpolation (with physical coords)
			// Subtract STARTINGX,Y,Z to accelerate access to data
			// In that way use DIRECT_A3D_ELEM, rather than A3D_ELEM
			int x0, y0, z0, x1, y1, z1;
			x0 = FLOOR(xp); fx = xp - x0; x0 -= STARTINGX(v); x1 = x0 + 1;
			y0 = FLOOR(yp); fy = yp - y0; y0 -= STARTINGY(v); y1 = y0 + 1;
			z0 = FLOOR(zp); fz = zp - z0; z0 -= STARTINGZ(v); z1 = z0 + 1;
			// DEBUG
			if ( (x0 < 0) || (y0 < 0) || (z0 < 0) || (x1 >= XSIZE(v)) || (y1 >= YSIZE(v)) || (z1 >= ZSIZE(v)) )
				std::cout << " idzidyidx= " << k << ", " << i << ", " << j << ", x0x1y0y1z0z1= " << x0 << ", " << x1 << ", " << y0 << ", " << y1 << ", " << z0 << ", " << z1 << std::endl;

			RFLOAT d000, d001, d010, d011, d100, d101, d110, d111;
			d000 = DIRECT_A3D_ELEM(v, z0, y0, x0);
			d001 = DIRECT_A3D_ELEM(v, z0, y0, x1);
			d010 = DIRECT_A3D_ELEM(v, z0, y1, x0);
			d011 = DIRECT_A3D_ELEM(v, z0, y1, x1);
			d100 = DIRECT_A3D_ELEM(v, z1, y0, x0);
			d101 = DIRECT_A3D_ELEM(v, z1, y0, x1);
			d110 = DIRECT_A3D_ELEM(v, z1, y1, x0);
			d111 = DIRECT_A3D_ELEM(v, z1, y1, x1);

			RFLOAT dx00, dx01, dx10, dx11;
			dx00 = LIN_INTERP(fx, d000, d001);
			dx01 = LIN_INTERP(fx, d100, d101);
			dx10 = LIN_INTERP(fx, d010, d011);
			dx11 = LIN_INTERP(fx, d110, d111);

			RFLOAT dxy0, dxy1, ddd;
			dxy0 = LIN_INTERP(fy, dx00, dx10);
			dxy1 = LIN_INTERP(fy, dx01, dx11);

			ddd = LIN_INTERP(fz, dxy0, dxy1);

			// Record this voxel
			dev_voxel.push_back(ddd);
		}

		// Calc dev of this voxel in the chunk
		if (dev_voxel.size() > 1)
		{
			sum_pw1 = sum_pw2 = 0.;
			for (int id = 0; id < dev_voxel.size(); id++)
			{
				sum_pw1 += dev_voxel[id];
				sum_pw2 += dev_voxel[id] * dev_voxel[id];
			}
			sum_pw1 /= dev_voxel.size();
			sum_pw2 /= dev_voxel.size();
			// TODO: record stddev or dev???
			dev_chunk.push_back(sum_pw2 - sum_pw1 * sum_pw1);
		}
		dev_voxel.clear();
	}

	// Calc avg of all voxels' devs in this chunk (for a specific helical symmetry)
	if (dev_chunk.size() < 1)
	{
		cc = (1e10);
		nr_asym_voxels = 0;
		return false;
	}
	else
	{
		sum_pw1 = 0.;
		for (int id = 0; id < dev_chunk.size(); id++)
			sum_pw1 += dev_chunk[id];
		cc = (sum_pw1 / dev_chunk.size());
	}
	nr_asym_voxels = dev_chunk.size();
	dev_chunk.clear();

	return true;
};

void checkRangesForLocalSearchHelicalSymmetry(
		RFLOAT rise_A,
		RFLOAT rise_min_A,
		RFLOAT rise_max_A,
		RFLOAT twist_deg,
		RFLOAT twist_min_deg,
		RFLOAT twist_max_deg)
{
	RFLOAT rise_avg_A = (rise_min_A + rise_max_A) / 2.;
	RFLOAT twist_avg_deg = (twist_min_deg + twist_max_deg) / 2.;

	if ( (rise_min_A < 0.001) || (rise_max_A < 0.001) || (rise_avg_A < 0.001) )
		REPORT_ERROR("helix.cpp::checkRangesForLocalSearchHelicalSymmetry(): Invalid helical rise!");
	if ( (rise_min_A > rise_max_A) || (twist_min_deg > twist_max_deg) )
		REPORT_ERROR("helix.cpp::checkRangesForLocalSearchHelicalSymmetry(): Minimum values of twist/rise should be smaller than their maximum values!");
	if ( (fabs(twist_avg_deg - twist_min_deg) > 180.01) || ((fabs(rise_avg_A - rise_min_A) / fabs(rise_avg_A)) > 0.3334) )
		REPORT_ERROR("helix.cpp::checkRangesForLocalSearchHelicalSymmetry(): Search ranges of helical parameters are too large!");
	if ( (rise_A < rise_min_A) || (rise_A > rise_max_A) || (twist_deg < twist_min_deg) || (twist_deg > twist_max_deg) )
		REPORT_ERROR("helix.cpp::checkRangesForLocalSearchHelicalSymmetry(): Helical twist and/or rise are out of their specified ranges!");
};

bool localSearchHelicalSymmetry(
		const MultidimArray<RFLOAT>& v,
		RFLOAT pixel_size_A,
		RFLOAT sphere_radius_A,
		RFLOAT cyl_inner_radius_A,
		RFLOAT cyl_outer_radius_A,
		RFLOAT z_percentage,
		RFLOAT rise_min_A,
		RFLOAT rise_max_A,
		RFLOAT rise_inistep_A,
		RFLOAT& rise_refined_A,
		RFLOAT twist_min_deg,
		RFLOAT twist_max_deg,
		RFLOAT twist_inistep_deg,
		RFLOAT& twist_refined_deg)
{
	// TODO: whether iterations can exit & this function works for negative twist
	int iter, box_len, nr_asym_voxels, nr_rise_samplings, nr_twist_samplings, nr_min_samplings, nr_max_samplings, best_id, iter_not_converged;
	RFLOAT r_min_pix, r_max_pix, best_dev, err_max;
	RFLOAT rise_min_pix, rise_max_pix, rise_step_pix, rise_inistep_pix, twist_step_deg, rise_refined_pix;
	RFLOAT rise_local_min_pix, rise_local_max_pix, twist_local_min_deg, twist_local_max_deg;
	std::vector<HelicalSymmetryItem> helical_symmetry_list;
	bool out_of_range, search_rise, search_twist;

	// Check input 3D reference
	if (v.getDim() != 3)
		REPORT_ERROR("helix.cpp::localSearchHelicalSymmetry(): Input helical reference is not 3D! (v.getDim() = " + integerToString(v.getDim()) + ")");
	box_len = (XSIZE(v) < YSIZE(v)) ? XSIZE(v) : YSIZE(v);
	box_len = (box_len < ZSIZE(v)) ? box_len : ZSIZE(v);

	// Check helical parameters
	checkHelicalParametersFor3DHelicalReference(
			box_len,
			pixel_size_A,
			twist_min_deg,
			rise_min_A,
			z_percentage,
			sphere_radius_A,
			cyl_inner_radius_A,
			cyl_outer_radius_A);
	checkHelicalParametersFor3DHelicalReference(
			box_len,
			pixel_size_A,
			twist_max_deg,
			rise_max_A,
			z_percentage,
			sphere_radius_A,
			cyl_inner_radius_A,
			cyl_outer_radius_A);
	rise_refined_A = (rise_min_A + rise_max_A) / 2.;
	rise_refined_pix = rise_refined_A / pixel_size_A;
	twist_refined_deg = (twist_min_deg + twist_max_deg) / 2.;
	checkRangesForLocalSearchHelicalSymmetry(
			rise_refined_A,
			rise_min_A,
			rise_max_A,
			twist_refined_deg,
			twist_min_deg,
			twist_max_deg);

	r_min_pix = cyl_inner_radius_A / pixel_size_A;
	r_max_pix = cyl_outer_radius_A / pixel_size_A;
	rise_inistep_pix = rise_inistep_A / pixel_size_A;
	rise_local_min_pix = rise_min_pix = rise_min_A / pixel_size_A;
	rise_local_max_pix = rise_max_pix = rise_max_A / pixel_size_A;
	twist_local_min_deg = twist_min_deg;
	twist_local_max_deg = twist_max_deg;

	// Initial searches - Iteration 1
	// Sampling of twist should be smaller than 1 degree
	// Sampling of rise should be smaller than 1%
	// And also make sure to search for at least 5*5 sampling points
	// Avoid too many searches (at most 1000*1000)
	err_max = (1e-5);
	search_rise = search_twist = true;
	nr_min_samplings = 5;
	nr_max_samplings = 1000;

	twist_inistep_deg = (twist_inistep_deg < (1e-5)) ? (1.) : (twist_inistep_deg);
	nr_twist_samplings = CEIL(fabs(twist_local_min_deg - twist_local_max_deg) / twist_inistep_deg);
	nr_twist_samplings = (nr_twist_samplings > nr_min_samplings) ? (nr_twist_samplings) : (nr_min_samplings);
	nr_twist_samplings = (nr_twist_samplings < nr_max_samplings) ? (nr_twist_samplings) : (nr_max_samplings);
	twist_step_deg = fabs(twist_local_min_deg - twist_local_max_deg) / RFLOAT(nr_twist_samplings);
	if ( fabs(twist_local_min_deg - twist_local_max_deg) < err_max)
	{
		twist_step_deg = 0.;
		twist_min_deg = twist_max_deg = twist_local_min_deg = twist_local_max_deg = twist_refined_deg;
		search_twist = false;
	}
#ifdef DEBUG_SEARCH_HELICAL_SYMMETRY
	std::cout << " ### twist_step_deg = " << twist_step_deg << ", nr_twist_samplings = " << nr_twist_samplings << ", search_twist = " << (int)(search_twist) << std::endl;
#endif

	rise_inistep_pix = (rise_inistep_pix < (1e-5)) ? (1e30) : (rise_inistep_pix);
	rise_step_pix = 0.01 * ((fabs(rise_local_min_pix) + fabs(rise_local_max_pix)) / 2.);
	rise_step_pix = (rise_step_pix < rise_inistep_pix) ? (rise_step_pix) : (rise_inistep_pix);
	nr_rise_samplings = CEIL(fabs(rise_local_min_pix - rise_local_max_pix) / rise_step_pix);
	nr_rise_samplings = (nr_rise_samplings > nr_min_samplings) ? (nr_rise_samplings) : (nr_min_samplings);
	nr_rise_samplings = (nr_rise_samplings < nr_max_samplings) ? (nr_rise_samplings) : (nr_max_samplings);
	rise_step_pix = fabs(rise_local_min_pix - rise_local_max_pix) / RFLOAT(nr_rise_samplings);
	if ((fabs(rise_local_min_pix - rise_local_max_pix) / fabs(rise_refined_pix)) < err_max)
	{
		rise_step_pix = 0.;
		rise_min_pix = rise_max_pix = rise_local_min_pix = rise_local_max_pix = rise_refined_pix;
		search_rise = false;
	}
#ifdef DEBUG_SEARCH_HELICAL_SYMMETRY
	std::cout << " ### rise_step_A = " << rise_step_pix * pixel_size_A << ", nr_rise_samplings = " << nr_rise_samplings << ", search_rise = " << (int)(search_rise) << std::endl;
#endif

	if ( (!search_twist) && (!search_rise) )
		return true;

	// Local searches
	helical_symmetry_list.clear();
	iter_not_converged = 0;
	for (iter = 1; iter <= 100; iter++)
	{
		// TODO: please check this!!!
		// rise_step_pix and twist_step_deg should be strictly > 0 now!
		makeHelicalSymmetryList(
				helical_symmetry_list,
				rise_local_min_pix,
				rise_local_max_pix,
				rise_step_pix,
				search_rise,
				twist_local_min_deg,
				twist_local_max_deg,
				twist_step_deg,
				search_twist);
		if (helical_symmetry_list.size() < 1)
			REPORT_ERROR("helix.cpp::localSearchHelicalSymmetry(): BUG No helical symmetries are found in the search list!");

		best_dev = (1e30);
		best_id = -1;
		for (int ii = 0; ii < helical_symmetry_list.size(); ii++)
		{
			// If this symmetry is not calculated before
			if (helical_symmetry_list[ii].dev > (1e30))
			{
				// TODO: please check this!!!
				calcCCofHelicalSymmetry(
						v,
						r_min_pix,
						r_max_pix,
						z_percentage,
						helical_symmetry_list[ii].rise_pix,
						helical_symmetry_list[ii].twist_deg,
						helical_symmetry_list[ii].dev,
						nr_asym_voxels);
#ifdef DEBUG_SEARCH_HELICAL_SYMMETRY
				std::cout << " NEW -->" << std::flush;
#endif
			}
			else
			{
#ifdef DEBUG_SEARCH_HELICAL_SYMMETRY
				std::cout << " OLD -->" << std::flush;
#endif
			}

			if (helical_symmetry_list[ii].dev < best_dev)
			{
				best_dev = helical_symmetry_list[ii].dev;
				best_id = ii;
			}
#ifdef DEBUG_SEARCH_HELICAL_SYMMETRY
			std::cout << " Twist = " << helical_symmetry_list[ii].twist_deg << ", Rise = " << helical_symmetry_list[ii].rise_pix * pixel_size_A << ", Dev = " << helical_symmetry_list[ii].dev << std::endl;
#endif
		}

		// Update refined symmetry
		rise_refined_pix = helical_symmetry_list[best_id].rise_pix;
		rise_refined_A = rise_refined_pix * pixel_size_A;
		twist_refined_deg = helical_symmetry_list[best_id].twist_deg;
#ifdef DEBUG_SEARCH_HELICAL_SYMMETRY
		std::cout << " ################################################################################" << std::endl;
		std::cout << " ##### Refined Twist = " << twist_refined_deg << ", Rise = " << rise_refined_A << ", Dev = " << helical_symmetry_list[best_id].dev << std::endl;
		std::cout << " ################################################################################" << std::endl;
#endif
		out_of_range = false;
		if ( (search_rise) && (rise_refined_pix < rise_min_pix) )
		{
			out_of_range = true;
			rise_refined_pix = rise_min_pix;
			rise_refined_A = rise_refined_pix * pixel_size_A;
		}
		if ( (search_rise) && (rise_refined_pix > rise_max_pix) )
		{
			out_of_range = true;
			rise_refined_pix = rise_max_pix;
			rise_refined_A = rise_refined_pix * pixel_size_A;
		}
		if ( (search_twist) && (twist_refined_deg < twist_min_deg) )
		{
			out_of_range = true;
			twist_refined_deg = twist_min_deg;
		}
		if ( (search_twist) && (twist_refined_deg > twist_max_deg) )
		{
			out_of_range = true;
			twist_refined_deg = twist_max_deg;
		}

		if (out_of_range)
		{
			std::cout << " WARNING: Refined helical symmetry is out of the search range. Check whether the initial guess of helical symmetry is reasonable. Or you may want to modify the search range." << std::endl;
			return false;
		}
		else
		{
			if (iter > 1)
			{
				// If the symmetry does not fall into the local search range
				// Try 7*, 9*, 11* ... samplings
				bool this_iter_not_converged = false;
				if (search_rise)
				{
					if ( (rise_refined_pix < (rise_local_min_pix + rise_step_pix * 0.5))
							|| (rise_refined_pix > (rise_local_max_pix - rise_step_pix * 0.5)) )
					{
						this_iter_not_converged = true;
						rise_local_min_pix = rise_refined_pix - ((RFLOAT)(iter_not_converged) + 3.) * rise_step_pix;
						rise_local_max_pix = rise_refined_pix + ((RFLOAT)(iter_not_converged) + 3.) * rise_step_pix;
					}
				}
				if (search_twist)
				{
					if ( (twist_refined_deg < (twist_local_min_deg + twist_step_deg * 0.5))
							|| (twist_refined_deg > (twist_local_max_deg - twist_step_deg * 0.5)) )
					{
						this_iter_not_converged = true;
						twist_local_min_deg = twist_refined_deg - ((RFLOAT)(iter_not_converged) + 3.) * twist_step_deg;
						twist_local_max_deg = twist_refined_deg + ((RFLOAT)(iter_not_converged) + 3.) * twist_step_deg;
					}
				}
				if (this_iter_not_converged)
				{
					iter_not_converged++;
#ifdef DEBUG_SEARCH_HELICAL_SYMMETRY
					std::cout << " !!! NR_ITERATION_NOT_CONVERGED = " << iter_not_converged << " !!!" << std::endl;
#endif
					if (iter_not_converged > 10) // Up to 25*25 samplings are allowed (original 5*5 samplings)
					{
						std::cout << " WARNING: Local searches of helical symmetry cannot converge. Consider a finer initial sampling of helical parameters." << std::endl;
						return false;
					}
					continue;
				}
			}
			iter_not_converged = 0;

			// Set 5*5 finer samplings for the next iteration
			if (search_rise)
			{
				rise_local_min_pix = rise_refined_pix - rise_step_pix;
				rise_local_max_pix = rise_refined_pix + rise_step_pix;
			}
			if (search_twist)
			{
				twist_local_min_deg = twist_refined_deg - twist_step_deg;
				twist_local_max_deg = twist_refined_deg + twist_step_deg;
			}

			// When there is no need to search for either twist or rise
			if ((rise_step_pix / fabs(rise_refined_pix)) < err_max)
			{
				rise_local_min_pix = rise_local_max_pix = rise_refined_pix;
				search_rise = false;
			}
			if ((twist_step_deg / fabs(twist_refined_deg)) < err_max)
			{
				twist_local_min_deg = twist_local_max_deg = twist_refined_deg;
				search_twist = false;
			}

			// Stop searches if step sizes are too small
			if ( (!search_twist) && (!search_rise) )
				return true;

			// Decrease step size
			if (search_rise)
				rise_step_pix /= 2.;
			if (search_twist)
				twist_step_deg /= 2.;
		}
	}
	return true;
};

RFLOAT getHelicalSigma2Rot(
		RFLOAT helical_rise_pix,
		RFLOAT helical_twist_deg,
		RFLOAT helical_offset_step_pix,
		RFLOAT rot_step_deg,
		RFLOAT old_sigma2_rot)
{
	if ( (helical_offset_step_pix < 0.) || (rot_step_deg < 0.) || (old_sigma2_rot < 0.) )
		REPORT_ERROR("helix.cpp::getHelicalSigma2Rot: Helical offset step, rot step or sigma2_rot cannot be negative!");

	RFLOAT nr_samplings_along_helical_axis = (fabs(helical_rise_pix)) / helical_offset_step_pix;
	RFLOAT rot_search_range = (fabs(helical_twist_deg)) / nr_samplings_along_helical_axis;
	RFLOAT new_rot_step = rot_search_range / 6.;
	RFLOAT factor = ceil(new_rot_step / rot_step_deg);
	RFLOAT new_sigma2_rot = old_sigma2_rot;
	//RFLOAT factor_max = 10.;
	if (factor > 1.)
	{
		// Avoid extremely big sigma_rot!!! Too expensive in time and memory!!!
		//if (factor > factor_max)
		//	factor = factor_max;
		new_sigma2_rot *= factor * factor;
	}
	return new_sigma2_rot;
};

// Assume all parameters are within range
RFLOAT get_lenZ_percentage_max(
		int box_len,
		RFLOAT sphere_radius_A,
		RFLOAT cyl_outer_radius_A,
		RFLOAT pixel_size_A)
{
	return (((2.) * sqrt(sphere_radius_A * sphere_radius_A - cyl_outer_radius_A * cyl_outer_radius_A) / pixel_size_A) / box_len);
};

// Assume all parameters are within range
RFLOAT get_rise_A_max(
		int box_len,
		RFLOAT pixel_size_A,
		RFLOAT lenZ_percentage,
		RFLOAT nr_units_min)
{
	return (pixel_size_A * box_len * lenZ_percentage / nr_units_min);
};

void checkHelicalParametersFor3DHelicalReference(
		int box_len,
		RFLOAT pixel_size_A,
		RFLOAT twist_deg,
		RFLOAT rise_A,
		RFLOAT lenZ_percentage,
		RFLOAT sphere_radius_A,
		RFLOAT cyl_inner_radius_A,
		RFLOAT cyl_outer_radius_A)
{
	long int half_box_len;
	RFLOAT nr_units_min = 2.; // Minimum nr_particles required along lenZ_max
	RFLOAT lenZ_percentage_max, rise_A_max, sphere_radius_pix, cyl_inner_radius_pix, cyl_outer_radius_pix;

	if (pixel_size_A < 0.001)
		REPORT_ERROR("helix.cpp::checkHelicalParametersFor3DHelicalReference(): Pixel size (in Angstroms) should be larger than 0.001!");

	sphere_radius_pix = sphere_radius_A / pixel_size_A;
	cyl_inner_radius_pix = cyl_inner_radius_A / pixel_size_A;
	cyl_outer_radius_pix = cyl_outer_radius_A / pixel_size_A;

	if (box_len < 10)
		REPORT_ERROR("helix.cpp::checkHelicalParametersFor3DHelicalReference(): Input box size should be larger than 5!");
	half_box_len = box_len / 2 - ((box_len + 1) % 2);

	if ( (fabs(twist_deg) > 360.) || ((rise_A / pixel_size_A) < 0.001) || (lenZ_percentage < 0.001) || (lenZ_percentage > 0.999) )
		REPORT_ERROR("helix.cpp::checkHelicalParametersFor3DHelicalReference(): Wrong helical twist, rise or lenZ!");

	if ( (sphere_radius_pix < 2.) || (sphere_radius_pix > half_box_len)
			|| ( (cyl_inner_radius_pix + 2.) > cyl_outer_radius_pix)
			|| (cyl_outer_radius_pix < 2.) || (cyl_outer_radius_pix > half_box_len)
			|| ( (sphere_radius_pix + 0.001) < cyl_outer_radius_pix ) )
	{
		std::cout << "sphere_radius_pix= " << sphere_radius_pix << ", half_box_len= " << half_box_len
				<< ", cyl_inner_radius_pix= " << cyl_inner_radius_pix << ", cyl_outer_radius_pix= " << cyl_outer_radius_pix << std::endl;
		REPORT_ERROR("helix.cpp::checkHelicalParametersFor3DHelicalReference(): Radii of spherical and/or cylindrical masks are invalid!");
	}

	lenZ_percentage_max = get_lenZ_percentage_max(box_len, sphere_radius_A, cyl_outer_radius_A, pixel_size_A);
	if (lenZ_percentage > lenZ_percentage_max)
		REPORT_ERROR("helix.cpp::checkHelicalParametersFor3DHelicalReference(): Central Z part is too long. (lenZ_percentage = "
				+ floatToString(lenZ_percentage) + "; lenZ_percentage < " + floatToString(lenZ_percentage_max) + ")");

	rise_A_max = get_rise_A_max(box_len, pixel_size_A, lenZ_percentage, nr_units_min);
	if (fabs(rise_A) > rise_A_max)
		REPORT_ERROR("helix.cpp::checkHelicalParametersFor3DHelicalReference(): Central Z part is too short (< nr_particles_min * helical_rise_A). (rise_A = "
				+ floatToString(rise_A) + ", lenZ_percentage = " + floatToString(lenZ_percentage)
				+ ", nr_particles_min = " + floatToString(nr_units_min) + "; lenZ_percentage > "
				+ floatToString((nr_units_min * rise_A_max) / (pixel_size_A * box_len)) + ")");
};

void imposeHelicalSymmetryInRealSpace(
		MultidimArray<RFLOAT>& v,
		RFLOAT pixel_size_A,
		RFLOAT sphere_radius_A,
		RFLOAT cyl_inner_radius_A,
		RFLOAT cyl_outer_radius_A,
		RFLOAT z_percentage,
		RFLOAT rise_A,
		RFLOAT twist_deg,
		RFLOAT cosine_width_pix)
{
	long int Xdim, Ydim, Zdim, Ndim, box_len;
	RFLOAT rise_pix, sphere_radius_pix, cyl_inner_radius_pix, cyl_outer_radius_pix, r_min, r_max, d_min, d_max, D_min, D_max, z_min, z_max;

	int rec_len;
	std::vector<RFLOAT> sin_rec, cos_rec;
	MultidimArray<RFLOAT> vout;

	if (v.getDim() != 3)
		REPORT_ERROR("helix.cpp::imposeHelicalSymmetryInRealSpace(): Input helical reference is not 3D! (vol.getDim() = " + integerToString(v.getDim()) + ")");
	v.getDimensions(Xdim, Ydim, Zdim, Ndim);
	box_len = (Xdim < Ydim) ? Xdim : Ydim;
	box_len = (box_len < Zdim) ? box_len : Zdim;

	// Check helical parameters
	checkHelicalParametersFor3DHelicalReference(
			box_len,
			pixel_size_A,
			twist_deg,
			rise_A,
			z_percentage,
			sphere_radius_A,
			cyl_inner_radius_A,
			cyl_outer_radius_A);

	// Parameters of mask
	if (cosine_width_pix < 1.)
		cosine_width_pix = 1.;  // Avoid 'divided by 0' error
	rise_pix = fabs(rise_A / pixel_size_A);  // Keep helical rise as a positive number
	sphere_radius_pix = sphere_radius_A / pixel_size_A;
	cyl_inner_radius_pix = cyl_inner_radius_A / pixel_size_A;
	cyl_outer_radius_pix = cyl_outer_radius_A / pixel_size_A;
	r_min = sphere_radius_pix;
	r_max = sphere_radius_pix + cosine_width_pix;
	d_min = cyl_inner_radius_pix - cosine_width_pix;
	d_max = cyl_inner_radius_pix;
	D_min = cyl_outer_radius_pix;
	D_max = cyl_outer_radius_pix + cosine_width_pix;

	// Crop the central slices
	v.setXmippOrigin();
	z_max = ((RFLOAT)(Zdim)) * z_percentage / 2.;
	if (z_max > (((RFLOAT)(FINISHINGZ(v))) - 1.))
		z_max = (((RFLOAT)(FINISHINGZ(v))) - 1.);
	z_min = -z_max;
	if (z_min < (((RFLOAT)(STARTINGZ(v))) + 1.))
		z_min = (((RFLOAT)(STARTINGZ(v))) + 1.);

	// Init volumes
	v.setXmippOrigin();
	vout.clear();
	vout.resize(v);
	vout.setXmippOrigin();

	// Calculate tabulated sine and cosine values
	rec_len = 2 + (CEIL((RFLOAT(Zdim) + 2.) / rise_pix));
	sin_rec.clear();
	cos_rec.clear();
	sin_rec.resize(rec_len);
	cos_rec.resize(rec_len);
	for (int id = 0; id < rec_len; id++)
		sincos(DEG2RAD(((RFLOAT)(id)) * twist_deg), &sin_rec[id], &cos_rec[id]);

	FOR_ALL_ELEMENTS_IN_ARRAY3D(v)
	{
		// Out of the mask
		RFLOAT dd = (RFLOAT)(i * i + j * j);
		RFLOAT rr = dd + (RFLOAT)(k * k);
		RFLOAT d = sqrt(dd);
		RFLOAT r = sqrt(rr);
		if ( (r > r_max) || (d < d_min) || (d > D_max) )
		{
			A3D_ELEM(v, k, i, j) = 0.;
			continue;
		}

		// How many voxels should be used to calculate the average?
		RFLOAT zi = (RFLOAT)(k);
		RFLOAT yi = (RFLOAT)(i);
		RFLOAT xi = (RFLOAT)(j);
		int rot_max = -(CEIL((zi - z_max) / rise_pix));
		int rot_min = -(FLOOR((zi - z_min) / rise_pix));
		if (rot_max < rot_min)
			REPORT_ERROR("helix.cpp::makeHelicalReferenceInRealSpace(): ERROR in imposing symmetry!");

		// Do the average
		RFLOAT pix_sum, pix_weight;
		pix_sum = pix_weight = 0.;
		for (int id = rot_min; id <= rot_max; id++)
		{
			// Get the sine and cosine value
			RFLOAT sin_val, cos_val;
			if (id >= 0)
			{
				sin_val = sin_rec[id];
				cos_val = cos_rec[id];
			}
			else
			{
				sin_val = (-1.) * sin_rec[-id];
				cos_val = cos_rec[-id];
			}

			// Get the voxel coordinates
			RFLOAT zp = zi + ((RFLOAT)(id)) * rise_pix;
			RFLOAT yp = xi * sin_val + yi * cos_val;
			RFLOAT xp = xi * cos_val - yi * sin_val;

			// Trilinear interpolation (with physical coords)
			// Subtract STARTINGY and STARTINGZ to accelerate access to data (STARTINGX=0)
			// In that way use DIRECT_A3D_ELEM, rather than A3D_ELEM
			int x0, y0, z0, x1, y1, z1;
			RFLOAT fx, fy, fz;
			x0 = FLOOR(xp); fx = xp - x0; x0 -= STARTINGX(v); x1 = x0 + 1;
			y0 = FLOOR(yp); fy = yp - y0; y0 -= STARTINGY(v); y1 = y0 + 1;
			z0 = FLOOR(zp); fz = zp - z0; z0 -= STARTINGZ(v); z1 = z0 + 1;

			RFLOAT d000, d001, d010, d011, d100, d101, d110, d111;
			d000 = DIRECT_A3D_ELEM(v, z0, y0, x0);
			d001 = DIRECT_A3D_ELEM(v, z0, y0, x1);
			d010 = DIRECT_A3D_ELEM(v, z0, y1, x0);
			d011 = DIRECT_A3D_ELEM(v, z0, y1, x1);
			d100 = DIRECT_A3D_ELEM(v, z1, y0, x0);
			d101 = DIRECT_A3D_ELEM(v, z1, y0, x1);
			d110 = DIRECT_A3D_ELEM(v, z1, y1, x0);
			d111 = DIRECT_A3D_ELEM(v, z1, y1, x1);

			RFLOAT dx00, dx01, dx10, dx11;
			dx00 = LIN_INTERP(fx, d000, d001);
			dx01 = LIN_INTERP(fx, d100, d101);
			dx10 = LIN_INTERP(fx, d010, d011);
			dx11 = LIN_INTERP(fx, d110, d111);

			RFLOAT dxy0, dxy1;
			dxy0 = LIN_INTERP(fy, dx00, dx10);
			dxy1 = LIN_INTERP(fy, dx01, dx11);

			pix_sum += LIN_INTERP(fz, dxy0, dxy1);
			pix_weight += 1.;
		}

		if (pix_weight > 0.9)
		{
			A3D_ELEM(vout, k, i, j) = pix_sum / pix_weight;

			if ( (d > d_max) && (d < D_min) && (r < r_min) )
			{}
			else // The pixel is within cosine edge(s)
			{
				pix_weight = 1.;
				if (d < d_max)  // d_min < d < d_max : w=(0~1)
					pix_weight = 0.5 + (0.5 * cos(PI * ((d_max - d) / cosine_width_pix)));
				else if (d > D_min) // D_min < d < D_max : w=(1~0)
					pix_weight = 0.5 + (0.5 * cos(PI * ((d - D_min) / cosine_width_pix)));
				if (r > r_min) // r_min < r < r_max
				{
					pix_sum = 0.5 + (0.5 * cos(PI * ((r - r_min) / cosine_width_pix)));
					pix_weight = (pix_sum < pix_weight) ? (pix_sum) : (pix_weight);
				}
				A3D_ELEM(vout, k, i, j) *= pix_weight;
			}
		}
		else
			A3D_ELEM(vout, k, i, j) = 0.;
    }

	// Copy and exit
	v = vout;
	sin_rec.clear();
	cos_rec.clear();
	vout.clear();

	return;
};

/*
void searchCnZSymmetry(
		const MultidimArray<RFLOAT>& v,
		RFLOAT r_min_pix,
		RFLOAT r_max_pix,
		int cn_start,
		int cn_end,
		std::vector<RFLOAT>& cn_list,
		std::vector<RFLOAT>& cc_list,
		std::vector<int>& nr_asym_voxels_list,
		std::ofstream* fout_ptr)
{
	int cn, Xdim, Ydim, Zdim, Ndim, nr_asym_voxels;
	RFLOAT cc;
	bool ok_flag;

	Xdim = XSIZE(v); Ydim = YSIZE(v); Zdim = ZSIZE(v); Ndim = NSIZE(v);

	if( (Ndim != 1) || (Zdim < 5) || (Ydim < 5) || (Xdim < 5) )
		REPORT_ERROR("helix.cpp::searchCnZSymmetry(): Input 3D MultidimArray has Wrong dimensions! (Ndim = " + integerToString(Ndim) + ", Zdim = " + integerToString(Zdim) + ", Ydim = " + integerToString(Ydim) + ", Xdim = " + integerToString(Xdim) + ")");

	if( (r_max_pix < 2.) || (r_min_pix < 0.) || ((r_max_pix - r_min_pix) < 2.)
			|| (cn_start <= 1) || (cn_end <= 1) || (cn_start > cn_end) || (cn_end > 36) )
		REPORT_ERROR("helix.cpp::searchCnZSymmetry(): Wrong parameters!");

	cn_list.clear();
	cc_list.clear();
	nr_asym_voxels_list.clear();

	for(cn = cn_start; cn <= cn_end; cn++)
	{
		ok_flag = calcCCOfCnZSymmetry(v, r_min_pix, r_max_pix, cn, cc, nr_asym_voxels);
		if(!ok_flag)
			continue;

		cn_list.push_back(cn);
		cc_list.push_back(cc);
		nr_asym_voxels_list.push_back(nr_asym_voxels);

		if(fout_ptr != NULL)
			(*fout_ptr) << "Test Cn = " << cn << ", cc = " << cc << ",                               asym voxels = " << nr_asym_voxels << std::endl;
	}
	return;
}
*/

/*
RFLOAT calcCCofPsiFor2DHelicalSegment(
		const MultidimArray<RFLOAT>& v,
		RFLOAT psi_deg,
		RFLOAT pixel_size_A,
		RFLOAT sphere_radius_A,
		RFLOAT cyl_outer_radius_A)
{
	RFLOAT sphere_radius_pix, sphere_radius2_pix, cyl_radius_pix, r2, x, y, xp, yp, sum, sum2, nr, val;
	int x0, y0, vec_id, vec_len, half_box_len, box_len;
	std::vector<RFLOAT> sum_list, pix_list;
	Matrix2D<RFLOAT> R;

	if (pixel_size_A < 0.001)
		REPORT_ERROR("helix.cpp::calcCCofPsiFor2DHelicalSegment(): Pixel size (in Angstroms) should be larger than 0.001!");

	if (v.getDim() != 2)
		REPORT_ERROR("helix.cpp::calcCCofPsiFor2DHelicalSegment(): Input MultidimArray should be 2D!");

	if ( (YSIZE(v) < 10) || (XSIZE(v) < 10) )
		REPORT_ERROR("helix.cpp::calcCCofPsiFor2DHelicalSegment(): Input 2D MultidimArray should be larger than 10*10 pixels!");

	if ( (STARTINGY(v) != FIRST_XMIPP_INDEX(YSIZE(v))) || (STARTINGX(v) != FIRST_XMIPP_INDEX(XSIZE(v))) )
		REPORT_ERROR("helix.cpp::calcCCofPsiFor2DHelicalSegment(): The origin of input 2D MultidimArray is not at the center (use v.setXmippOrigin() before calling this function)!");

	box_len = (XSIZE(v) < YSIZE(v)) ? XSIZE(v) : YSIZE(v);
	half_box_len = box_len / 2 - ((box_len + 1) % 2);
	sphere_radius_pix = sphere_radius_A / pixel_size_A;
	cyl_radius_pix = cyl_outer_radius_A / pixel_size_A;
	if ( (sphere_radius_pix < 2.) || (sphere_radius_pix > half_box_len)
			|| (cyl_radius_pix < 2.) || (cyl_radius_pix > half_box_len)
			|| ( (sphere_radius_pix + 0.001) < cyl_radius_pix ) )
		REPORT_ERROR("helix.cpp::calcCCofPsiFor2DHelicalSegment(): Radii of spherical and/or cylindrical masks are invalid!");

	sphere_radius2_pix = sphere_radius_pix * sphere_radius_pix;
	x0 = STARTINGX(v);
	y0 = STARTINGY(v);
	vec_len = (int)((2. * cyl_radius_pix)) + 2;
	sum_list.clear();
	sum_list.resize(vec_len);
	pix_list.clear();
	pix_list.resize(vec_len);
	for (vec_id = 0; vec_id < vec_len; vec_id++)
		sum_list[vec_id] = pix_list[vec_id] = 0.;
	rotation2DMatrix(psi_deg, R, false);
	R.setSmallValuesToZero();

	FOR_ALL_ELEMENTS_IN_ARRAY2D(v)
	{
		x = j;
		y = i;
		r2 = i * i + j * j;
		if (r2 > sphere_radius2_pix)
			continue;
		//xp = x * R(0, 0) + y * R(0, 1);
		//vec_id = ROUND(xp - x0);
		yp = x * R(1, 0) + y * R(1, 1);
		vec_id = ROUND(yp - y0);
		if ( (vec_id < 0) || (vec_id >= vec_len) )
			continue;
		pix_list[vec_id]++;
		sum_list[vec_id] += A2D_ELEM(v, i, j);
	}

	nr = sum = sum2 = 0.;
	for (vec_id = 0; vec_id < vec_len; vec_id++)
	{
		if (pix_list[vec_id] > 0.5)
		{
			nr += 1.;
			val = sum_list[vec_id] / pix_list[vec_id];
			sum += val;
			sum2 += val * val;
		}
	}
	if (nr < 0.5)
		return (-1.);
	return ( (sum2 / nr) - ((sum / nr) * (sum / nr)) );
}

RFLOAT localSearchPsiFor2DHelicalSegment(
		const MultidimArray<RFLOAT>& v,
		RFLOAT pixel_size_A,
		RFLOAT sphere_radius_A,
		RFLOAT cyl_outer_radius_A,
		RFLOAT ori_psi_deg,
		RFLOAT search_half_range_deg,
		RFLOAT search_step_deg)
{
	RFLOAT psi_deg, best_psi_deg, max_cc, cc;
	if ( (search_step_deg < 0.000001) || (search_step_deg > 2.) )
		REPORT_ERROR("helix.cpp::localSearchPsiFor2DHelicalSegment(): Search step of psi angle should be 0.000001~1.999999 degrees!");
	if ( (search_half_range_deg < 0.000001) || (search_half_range_deg < search_step_deg) )
		REPORT_ERROR("helix.cpp::localSearchPsiFor2DHelicalSegment(): Search half range of psi angle should be not be less than 0.000001 degrees or the step size!");

	max_cc = -1.;
	best_psi_deg = 0.;
	for (psi_deg = (ori_psi_deg - search_half_range_deg); psi_deg < (ori_psi_deg + search_half_range_deg); psi_deg += search_step_deg)
	{
		cc = calcCCofPsiFor2DHelicalSegment(v, psi_deg, pixel_size_A, sphere_radius_A, cyl_outer_radius_A);
		if ( (cc > 0.) && (cc > max_cc) )
		{
			max_cc = cc;
			best_psi_deg = psi_deg;
		}
		// DEBUG
		//std::cout << "psi_deg = " << psi_deg << ", cc = " << cc << std::endl;
	}
	// DEBUG
	//std::cout << "------------------------------------------------" << std::endl;
	if (max_cc < 0.)
		REPORT_ERROR("helix.cpp::localSearchPsiFor2DHelicalSegment(): Error! No cc values for any psi angles are valid! Check if the input images are blank!");
	return best_psi_deg;
}

RFLOAT searchPsiFor2DHelicalSegment(
		const MultidimArray<RFLOAT>& v,
		RFLOAT pixel_size_A,
		RFLOAT sphere_radius_A,
		RFLOAT cyl_outer_radius_A)
{
	int nr_iter;
	RFLOAT search_half_range_deg, search_step_deg, best_psi_deg;

	search_half_range_deg = 89.999;
	search_step_deg = 0.5;
	best_psi_deg = 0.;

	for(nr_iter = 1; nr_iter <= 20; nr_iter++)
	{
		best_psi_deg = localSearchPsiFor2DHelicalSegment(v, pixel_size_A, sphere_radius_A, cyl_outer_radius_A,
				best_psi_deg, search_half_range_deg, search_step_deg);
		search_half_range_deg = 2. * search_step_deg;
		search_step_deg /= 2.;
		if (fabs(search_step_deg) < 0.00001)
			break;
	}
	return best_psi_deg;
}
*/

void calcRadialAverage(
		const MultidimArray<RFLOAT>& v,
		std::vector<RFLOAT>& radial_avg_val_list)
{
	int ii, Xdim, Ydim, Zdim, Ndim, list_size, dist;
	MultidimArray<RFLOAT> vol;
	std::vector<RFLOAT> radial_pix_counter_list;

	vol.clear();
	radial_pix_counter_list.clear();
	radial_avg_val_list.clear();

	// Check dimensions
	Xdim = XSIZE(v); Ydim = YSIZE(v); Zdim = ZSIZE(v); Ndim = NSIZE(v);

	if( (Ndim != 1) || (Zdim < 5) || (Ydim < 5) || (Xdim < 5) )
		REPORT_ERROR("helix.cpp::calcRadialAverage(): Input 3D MultidimArray has wrong dimensions! (Ndim = " + integerToString(Ndim) + ", Zdim = " + integerToString(Zdim) + ", Ydim = " + integerToString(Ydim) + ", Xdim = " + integerToString(Xdim) + ")");

	// Resize and init vectors
	list_size = ROUND(sqrt(Xdim * Xdim + Ydim * Ydim)) + 2;
	radial_pix_counter_list.resize(list_size);
	radial_avg_val_list.resize(list_size);
	for(ii = 0; ii < list_size; ii++)
	{
		radial_pix_counter_list[ii] = 0.;
		radial_avg_val_list[ii] = 0.;
	}

	vol = v;
	vol.setXmippOrigin();
	FOR_ALL_ELEMENTS_IN_ARRAY3D(vol)
	{
		dist = ROUND(sqrt(i * i + j * j));
		if( (dist < 0) || (dist > (list_size - 1)) )
			continue;
		radial_pix_counter_list[dist] += 1.;
		radial_avg_val_list[dist] += A3D_ELEM(vol, k, i, j);
	}

	for(ii = 0; ii < list_size; ii++)
	{
		if(radial_pix_counter_list[ii] > 0.9)
			radial_avg_val_list[ii] /= radial_pix_counter_list[ii];
		else
			radial_avg_val_list[ii] = 0.;
	}

	radial_pix_counter_list.clear();
	vol.clear();

	return;
};

void cutZCentralPartOfSoftMask(
		MultidimArray<RFLOAT>& mask,
		RFLOAT z_percentage,
		RFLOAT cosine_width)
{
	int dim = mask.getDim();
	int Zdim = ZSIZE(mask);
	RFLOAT idz_s, idz_s_w, idz_e, idz_e_w, idz, val;
	if (dim != 3)
		REPORT_ERROR("helix.cpp::cutZCentralPartOfSoftMask(): Input mask should have a dimension of 3!");
	if (Zdim < 5)
		REPORT_ERROR("helix.cpp::cutZCentralPartOfSoftMask(): Z length of 3D mask os less than 5!");
	if ( (z_percentage < 0.1) || (z_percentage > 0.9) )
		REPORT_ERROR("helix.cpp::cutZCentralPartOfSoftMask(): Only 10%-90% of total Z length should be retained!");
	if (cosine_width < 0.001)
		REPORT_ERROR("helix.cpp::cutZCentralPartOfSoftMask(): Cosine width for soft edge should larger than 0!");

	idz_e = ((RFLOAT)(Zdim)) * z_percentage / 2.;
	idz_s = idz_e * (-1.);
	idz_s_w = idz_s - cosine_width;
	idz_e_w = idz_e + cosine_width;
	std::cout << "z_len, z_percentage, cosine_width = " << Zdim << ", " << z_percentage << ", " << cosine_width << std::endl;
	std::cout << "idz_s_w, idz_s, idz_e, idz_e_w = " << idz_s_w << ", " << idz_s << ", " << idz_e << ", " << idz_e_w << std::endl;
	mask.setXmippOrigin();
	FOR_ALL_ELEMENTS_IN_ARRAY3D(mask)
	{
		idz = ((RFLOAT)(k));
		if ( (idz > idz_s) && (idz < idz_e) )
		{}
		else if ( (idz < idz_s_w) || (idz > idz_e_w) )
			A3D_ELEM(mask, k, i, j) = 0.;
		else
		{
			val = 1.;
			if (idz < idz_s)
				val = 0.5 + 0.5 * cos(PI * (idz_s - idz) / cosine_width);
			else if (idz > idz_e)
				val = 0.5 + 0.5 * cos(PI * (idz - idz_e) / cosine_width);
			A3D_ELEM(mask, k, i, j) *= val;
		}
	}
	return;
};

void createCylindricalReference(
		MultidimArray<RFLOAT>& v,
		int box_size,
		RFLOAT inner_diameter_pix,
		RFLOAT outer_diameter_pix,
		RFLOAT cosine_width)
{
	RFLOAT r, dist, inner_radius_pix, outer_radius_pix;

	// Check dimensions
	if(box_size < 5)
		REPORT_ERROR("helix.cpp::createCylindricalReference(): Invalid box size.");

	if( (inner_diameter_pix > outer_diameter_pix)
			|| (outer_diameter_pix < 0.) || (outer_diameter_pix > (box_size - 1))
			|| (cosine_width < 0.) )
		REPORT_ERROR("helix.cpp::createCylindricalReference(): Parameter(s) error!");

	inner_radius_pix = inner_diameter_pix / 2.;
	outer_radius_pix = outer_diameter_pix / 2.;

	v.clear();
	v.resize(box_size, box_size, box_size);
	v.setXmippOrigin();
	FOR_ALL_ELEMENTS_IN_ARRAY3D(v)
	{
		r = sqrt(i * i + j * j);
		if( (r > inner_radius_pix) && (r < outer_radius_pix) )
		{
			A3D_ELEM(v, k, i, j) = 1.;
			continue;
		}
		dist = -9999.;
		if( (r > outer_radius_pix) && (r < (outer_radius_pix + cosine_width)) )
			dist = r - outer_radius_pix;
		else if( (r < inner_radius_pix) && (r > (inner_radius_pix - cosine_width)) )
			dist = inner_radius_pix - r;
		if(dist > 0.)
		{
			A3D_ELEM(v, k, i, j) = 0.5 + 0.5 * cos(PI * dist / cosine_width);
			continue;
		}
		A3D_ELEM(v, k, i, j) = 0.;
	}
	return;
}

void transformCartesianAndHelicalCoords(
		Matrix1D<RFLOAT>& in,
		Matrix1D<RFLOAT>& out,
		RFLOAT psi_deg,
		RFLOAT tilt_deg,
		bool direction)
{
	int dim;
	RFLOAT x0, y0, z0;
	Matrix1D<RFLOAT> aux;
	Matrix2D<RFLOAT> A;

	dim = in.size();
	if( (dim != 2) && (dim != 3) )
		REPORT_ERROR("helix.cpp::transformCartesianAndHelicalCoords(): Vector of input coordinates should have 2 or 3 values!");

	aux.clear();
	aux.resize(3);
	XX(aux) = XX(in);
	YY(aux) = YY(in);
	ZZ(aux) = (dim == 3) ? (ZZ(in)) : (0.);

	if(dim == 2)
	{
		tilt_deg = 0.;
	}
	if(direction == CART_TO_HELICAL_COORDS)
	{
		psi_deg *= -1.;
		tilt_deg *= -1.;
	}

	A.clear();
	A.resize(3, 3);
	Euler_angles2matrix(0., tilt_deg, psi_deg, A, false);
	aux = A * aux;

	out.clear();
	out.resize(2);
	XX(out) = XX(aux);
	YY(out) = YY(aux);
	if(dim == 3)
	{
		out.resize(3);
		ZZ(out) = ZZ(aux);
	}
	aux.clear();
	A.clear();

	return;
}

void transformCartesianAndHelicalCoords(
		RFLOAT xin,
		RFLOAT yin,
		RFLOAT zin,
		RFLOAT& xout,
		RFLOAT& yout,
		RFLOAT& zout,
		RFLOAT psi_deg,
		RFLOAT tilt_deg,
		int dim,
		bool direction)
{
	if( (dim != 2) && (dim != 3) )
		REPORT_ERROR("helix.cpp::transformCartesianAndHelicalCoords(): Vector of input coordinates should have 2 or 3 values!");

	Matrix1D<RFLOAT> in, out;
	in.clear();
	out.clear();
	in.resize(dim);
	XX(in) = xin;
	YY(in) = yin;
	if (dim == 3)
		ZZ(in) = zin;
	transformCartesianAndHelicalCoords(in, out, psi_deg, tilt_deg, direction);
	xout = XX(out);
	yout = YY(out);
	if (dim == 3)
		zout = ZZ(out);
	return;
}

/*
void makeBlot(
		MultidimArray<RFLOAT>& v,
		RFLOAT y,
		RFLOAT x,
		RFLOAT r)
{
	int Xdim, Ydim, Zdim, Ndim;
	RFLOAT dist, min;
	v.getDimensions(Xdim, Ydim, Zdim, Ndim);
	if( (Ndim != 1) || (Zdim != 1) || (YXSIZE(v) <= 2) )
		return;

	min = DIRECT_A2D_ELEM(v, 0, 0);
	FOR_ALL_DIRECT_ELEMENTS_IN_ARRAY2D(v)
		min = (DIRECT_A2D_ELEM(v, i, j) < min) ? DIRECT_A2D_ELEM(v, i, j) : min;

	v.setXmippOrigin();
	FOR_ALL_ELEMENTS_IN_ARRAY2D(v)
	{
		dist = (i - y) * (i - y) + (j - x) * (j - x);
		dist = sqrt(dist);
		if(dist < r)
			A2D_ELEM(v, i, j) = min;
	}
	return;
}
*/

void makeSimpleHelixFromPDBParticle(
		const Assembly& ori,
		Assembly& helix,
		RFLOAT radius_A,
		RFLOAT twist_deg,
		RFLOAT rise_A,
		int nr_copy,
		bool do_center)
{
	int nr_ori_atoms;
	Matrix1D<RFLOAT> mass_centre, shift;
	Matrix2D<RFLOAT> rotational_matrix;
	Assembly aux0, aux1;

	if (nr_copy < 3)
		REPORT_ERROR("helix.cpp::makeHelixFromPDBParticle(): More than 3 copies of original assemblies are required to form a helix!");

	nr_ori_atoms = ori.numberOfAtoms();
	std::cout << "Original assembly contains " << nr_ori_atoms << " atoms." << std::endl;

	if (nr_ori_atoms < 1)
		REPORT_ERROR("helix.cpp::makeHelixFromPDBParticle(): Original assembly contains no atoms!");

	// Calculate centre of mass of the original assembly
	mass_centre.resize(3);
	mass_centre.initZeros();
	for (int imol = 0; imol < ori.molecules.size(); imol++)
	{
		for (int ires = 0; ires < ori.molecules[imol].residues.size(); ires++)
		{
			for (int iatom = 0; iatom < ori.molecules[imol].residues[ires].atoms.size(); iatom++)
			{
				if( ((ori.molecules[imol].residues[ires].atoms[iatom]).coords).size() != 3 )
					REPORT_ERROR("helix.cpp::makeHelixFromPDBParticle(): Coordinates of atoms should have a dimension of 3!");
				mass_centre += (ori.molecules[imol].residues[ires].atoms[iatom]).coords;
			}
		}
	}
	mass_centre /= (RFLOAT)(nr_ori_atoms);

	aux0.clear();
	aux0 = ori;
	// Set the original particle on (r, 0, 0), if r > 0
	// Else just impose helical symmetry (make copies) according to the original particle
	if (do_center)
	{
		for (int imol = 0; imol < aux0.molecules.size(); imol++)
		{
			for (int ires = 0; ires < aux0.molecules[imol].residues.size(); ires++)
			{
				for (int iatom = 0; iatom < aux0.molecules[imol].residues[ires].atoms.size(); iatom++)
				{
					(aux0.molecules[imol].residues[ires].atoms[iatom]).coords -= mass_centre;
					XX((aux0.molecules[imol].residues[ires].atoms[iatom]).coords) += radius_A;
				}
			}
		}
		std::cout << "Centre of mass (in Angstroms) = " << XX(mass_centre) << ", " << YY(mass_centre) << ", " << ZZ(mass_centre) << std::endl;
		std::cout << "Bring the centre of mass to the (helical_radius, 0, 0)" << std::endl;
		std::cout << "Helical radius (for centre of mass) assigned = " << radius_A << " Angstroms" << std::endl;
	}

	// Construct the helix
	rotational_matrix.clear();
	shift.resize(3);
	shift.initZeros();
	helix.clear();
	helix.join(aux0);
	for (int ii = (((nr_copy + 1) % 2) - (nr_copy / 2)) ; ii <= (nr_copy / 2); ii++)
	{
		if (ii == 0)
			continue;

		rotation2DMatrix(((RFLOAT)(ii)) * twist_deg, rotational_matrix, true);
		ZZ(shift) = (RFLOAT)(ii) * rise_A;

		aux1.clear();
		aux1 = aux0;
		aux1.applyTransformation(rotational_matrix, shift);
		helix.join(aux1);
	}

	return;
}

/*
void normalise2DImageSlices(
		const FileName& fn_in,
		const FileName& fn_out,
		int bg_radius,
		RFLOAT white_dust_stddev,
		RFLOAT black_dust_stddev)
{
	Image<RFLOAT> stack_in, slice;
	int Xdim, Ydim, Zdim;
	long int Ndim, ii;

	if ( (fn_in.getExtension() != "mrcs") || (fn_out.getExtension() != "mrcs") )
	{
		REPORT_ERROR("helix.cpp::normalise2DImageSlices(): Input and output should be .mrcs files!");
	}
	stack_in.clear();
	stack_in.read(fn_in, false, -1, false, false); // readData = false, select_image = -1, mapData= false, is_2D = false);
	stack_in.getDimensions(Xdim, Ydim, Zdim, Ndim);
	std::cout << "File = " << fn_in.c_str() << std::endl;
	std::cout << "X, Y, Z, N dim = " << Xdim << ", " << Ydim << ", " << Zdim << ", " << Ndim << std::endl;
	std::cout << "bg_radius = " << bg_radius << ", white_dust_stddev = " << white_dust_stddev << ", black_dust_stddev = " << black_dust_stddev << std::endl;
	if( (Zdim != 1) || (Ndim < 1) || (Xdim < 3) || (Ydim < 3) )
	{
		REPORT_ERROR("helix.cpp::normalise2DImageSlices(): Invalid image dimensionality!");
	}

	for(ii = 0; ii < Ndim; ii++)
	{
		slice.read(fn_in, true, ii, false, false);
		normalise(slice, bg_radius, white_dust_stddev, black_dust_stddev, false);

		// Write this particle to the stack on disc
		// First particle: write stack in overwrite mode, from then on just append to it
		if (ii == 0)
			slice.write(fn_out, -1, (Ndim > 1), WRITE_OVERWRITE);
		else
			slice.write(fn_out, -1, false, WRITE_APPEND);
	}

	return;
}
*/

void applySoftSphericalMask(
		MultidimArray<RFLOAT>& v,
		RFLOAT sphere_diameter,
		RFLOAT cosine_width)
{
	RFLOAT r, r_max, r_max_edge;
	int dim = v.getDim();
	if (dim != 3)
		REPORT_ERROR("helix.cpp::applySoftSphericalMask(): Input image should have a dimension of 3!");
	if (cosine_width < 0.01)
		REPORT_ERROR("helix.cpp::applySoftSphericalMask(): Cosine width should be a positive value!");

	v.setXmippOrigin();
	r_max = (XSIZE(v) < YSIZE(v)) ? (XSIZE(v)) : (YSIZE(v));
	r_max = (r_max < ZSIZE(v)) ? (r_max) : (ZSIZE(v));
	if (cosine_width > 0.05 * r_max)
		r_max -= 2. * cosine_width;
	r_max *= 0.45;
	if ( (sphere_diameter > 0.01) && ((sphere_diameter / 2.) < r_max) )
		r_max = sphere_diameter / 2.;
	r_max_edge = r_max + cosine_width;

	FOR_ALL_ELEMENTS_IN_ARRAY3D(v)
	{
		r = sqrt(k * k + i * i + j * j);
		if (r > r_max)
		{
			if (r < r_max_edge)
				A3D_ELEM(v, k, i, j) *= 0.5 + 0.5 * cos(PI * (r - r_max) / cosine_width);
			else
				A3D_ELEM(v, k, i, j) = 0.;
		}
	}
	return;
}

/*
int extractCoordsForAllHelicalSegments(
		FileName& fn_in,
		FileName& fn_out,
		int nr_asu,
		RFLOAT rise_ang,
		RFLOAT pixel_size_ang,
		RFLOAT Xdim,
		RFLOAT Ydim,
		RFLOAT box_size_pix)
{
	int nr_segments, nr_lines_header, tube_id;
	RFLOAT rot, tilt, psi, xoff, yoff;
	char charBuf[10000];
	RFLOAT k, b, x1, y1, x2, y2, delta_x, delta_y, now_x, now_y, step, half_box_size_pix;

	// Check parameters and open files
	if ( (nr_asu < 1) || (rise_ang < 0.001) || (pixel_size_ang < 0.01) )
		REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments(): Wrong parameters!");
	if ( (box_size_pix < 2) || (Xdim < box_size_pix) || (Ydim < box_size_pix))
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalCoordsToStarFile(): Wrong dimensions or box size!");
	std::ifstream infile(fn_in.data(), std::ios_base::in);
    if (infile.fail())
    	REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments(): File " + fn_in.c_str() + (std::string) " does not exists." );
    FileName ext = fn_in.getFileFormat();
    if (ext != "star")
    	REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments(): MetadataTable should have .star extension.");
    std::ofstream outfile(fn_out.data(), std::ios_base::out);
    if (outfile.fail())
    	REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments(): Output to file: " + fn_out.c_str() + (std::string) " failed." );

    half_box_size_pix = box_size_pix / 2.;

    // Handle input and output for headers
    nr_lines_header = 0;
    while(1)
    {
    	charBuf[0] = '\0';
    	infile.getline(charBuf, 9999);
    	if ( (strlen(charBuf) < 4) || (strstr(charBuf, "_rln") != NULL) || (strstr(charBuf, "data_") != NULL) || (strstr(charBuf, "loop_") != NULL) )
    		nr_lines_header++;
    	else
    		break;
    }
    infile.seekg(std::ios_base::beg);
    for (int ii = 0; ii < nr_lines_header; ii++)
    {
    	charBuf[0] = '\0';
    	infile.getline(charBuf, 999);
    }

    outfile << std::endl << "data_" << std::endl << std::endl << "loop_" << std::endl;
    outfile << "_rlnCoordinateX #1" << std::endl << "_rlnCoordinateY #2" << std::endl;
	outfile << "_rlnAngleRot #3" << std::endl << "_rlnAngleTilt #4" << std::endl << "_rlnAnglePsi #5" << std::endl;
	outfile << "_rlnOriginX #6" << std::endl << "_rlnOriginY #7" << std::endl;
	outfile << "_rlnHelicalTubeID #8" << std::endl;

	// Calculate all coordinates for helical segments
	nr_segments = 0;
	rot = psi = xoff = yoff = now_x = now_y = 0.;
	tilt = 90.;
    tube_id = 0;
    step = nr_asu * rise_ang / pixel_size_ang;
    tube_id = 0;
    while (infile >> x1)
    {
    	tube_id++;

    	infile >> y1 >> x2 >> y2;
    	//std::cout << "x1: " << x1 << " y1: " << y1 << " x2: " << x2 << " y2: " << y2 << std::endl;
    	if (fabs(x2 - x1) > (1e-5))
    	{
    		k = (y2 - y1) / (x2 - x1);
        	b = y1 - k * x1;
        	std::cout << "Detected Line " << tube_id << ": y = (" << k << ") x + (" << b << ")" << std::flush;

        	delta_x = step / (sqrt(k * k + 1.));
        	if (x1 > x2)
        		delta_x *= -1.;
        	delta_y = k * delta_x;
    	}
    	else
    	{
    		std::cout << "Detected Line " << tube_id << ": x = (" << x1 << ")" << std::flush;
    		delta_x = 0.;
    		delta_y = step;
    		if (y1 > y2)
    			delta_y *= -1.;
    	}
    	std::cout << "   ###   Delta x: " << delta_x << "    Delta y: " << delta_y << std::endl;

    	now_x = x1;
    	now_y = y1;
    	while (1)
    	{
    		now_x += delta_x;
    		now_y += delta_y;
    		if ( ((now_x > x1) && (now_x > x2)) || ((now_x < x1) && (now_x < x2))
    				|| ((now_y > y1) && (now_y > y2)) || ((now_y < y1) && (now_y < y2)) )
    			break;
    		else
    		{
    			// Avoid segments lying on the edges of the micrographs
    			if ( (now_x < half_box_size_pix)
    					|| (now_x > (Xdim - half_box_size_pix))
    					|| (now_y < half_box_size_pix)
    					|| (now_y > (Ydim - half_box_size_pix)) )
    			{
    				continue;
    			}

    			psi = (-180.) * atan(k) / PI;
    			outfile << std::setiosflags(std::ios::fixed) << std::setprecision(6)
						<< now_x << "	" << now_y << "	"
						<< rot << "	" << tilt << "	" << psi << "	"
						<< xoff << "	" << yoff << "	"
						<< std::resetiosflags(std::ios::fixed)
    					<< tube_id << std::endl;
    			nr_segments++;
    		}
    	}
    }

    if (nr_segments < 1)
    	std::cout << "WARNING: no segments extracted from file '"
				<< fn_in << "'!" << std::endl;
    else
    	std::cout << "Total segments / particles extracted from file '"
				<< fn_in << "' = " << nr_segments << " / " << (nr_segments * nr_asu) << std::endl;
    std::cout << "---------------------------------------------------" << std::endl;

    infile.close();
    outfile.close();

    return nr_segments;
}
*/

int extractCoordsForAllHelicalSegments(
		FileName& fn_in,
		FileName& fn_out,
		int nr_asu,
		RFLOAT rise_ang,
		RFLOAT pixel_size_ang,
		RFLOAT Xdim,
		RFLOAT Ydim,
		RFLOAT box_size_pix)
{
	int nr_segments, tube_id, MDobj_id;
	RFLOAT rot, tilt, psi, xoff, yoff;
	RFLOAT k, b, x1, y1, x2, y2, delta_x, delta_y, now_x, now_y, step, half_box_size_pix;
	MetaDataTable MD_in, MD_out;
	std::vector<RFLOAT> x1_coord_list, y1_coord_list, x2_coord_list, y2_coord_list;

	// Check parameters and open files
	if ( (nr_asu < 1) || (rise_ang < 0.001) || (pixel_size_ang < 0.01) )
		REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments(): Wrong parameters!");
	if ( (box_size_pix < 2) || (Xdim < box_size_pix) || (Ydim < box_size_pix))
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalCoordsToStarFile(): Wrong dimensions or box size!");
    if ( (fn_in.getExtension() != "star") || (fn_out.getExtension() != "star") )
    	REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments(): MetadataTable should have .star extension.");

    half_box_size_pix = box_size_pix / 2.;

    // Read input STAR file
    MD_in.clear();
    MD_in.read(fn_in);
    if ( (!MD_in.containsLabel(EMDL_IMAGE_COORD_X)) || (!MD_in.containsLabel(EMDL_IMAGE_COORD_Y)) )
    	REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments(): Input STAR file does not contain X and Y coordinates!");
    if (MD_in.numberOfObjects() % 2)
    	REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments(): Input coordinates should be in pairs!");
    x1_coord_list.clear();
    y1_coord_list.clear();
    x2_coord_list.clear();
    y2_coord_list.clear();
    MDobj_id = 0;
    FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD_in)
    {
    	MDobj_id++;
    	MD_in.getValue(EMDL_IMAGE_COORD_X, now_x);
    	MD_in.getValue(EMDL_IMAGE_COORD_Y, now_y);
    	if (MDobj_id % 2)
    	{
    		x1_coord_list.push_back(now_x);
    		y1_coord_list.push_back(now_y);
    	}
    	else
    	{
    		x2_coord_list.push_back(now_x);
    		y2_coord_list.push_back(now_y);
    	}
    }
    if ( (x1_coord_list.size() != x2_coord_list.size())
    		|| (x2_coord_list.size() != y1_coord_list.size())
			|| (y1_coord_list.size() != y2_coord_list.size())
			|| (y2_coord_list.size() != x1_coord_list.size()) )
    	REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments(): BUG in reading input STAR file!");
    MD_in.clear();

    // Init output STAR file
    MD_out.clear();
    MD_out.addLabel(EMDL_IMAGE_COORD_X);
    MD_out.addLabel(EMDL_IMAGE_COORD_Y);
    MD_out.addLabel(EMDL_ORIENT_ROT);
    MD_out.addLabel(EMDL_ORIENT_TILT);
    MD_out.addLabel(EMDL_ORIENT_PSI);
    MD_out.addLabel(EMDL_ORIENT_ORIGIN_X);
    MD_out.addLabel(EMDL_ORIENT_ORIGIN_Y);
    MD_out.addLabel(EMDL_PARTICLE_HELICAL_TUBE_ID);

	// Calculate all coordinates for helical segments
	nr_segments = 0;
	rot = psi = xoff = yoff = now_x = now_y = 0.;
	tilt = 90.;
    step = nr_asu * rise_ang / pixel_size_ang;
    tube_id = 0;

    for (tube_id = 0; tube_id < x1_coord_list.size(); tube_id++)
    {
    	x1 = x1_coord_list[tube_id];
    	y1 = y1_coord_list[tube_id];
    	x2 = x2_coord_list[tube_id];
    	y2 = y2_coord_list[tube_id];

    	if (fabs(x2 - x1) > (1e-5))
    	{
    		k = (y2 - y1) / (x2 - x1);
        	b = y1 - k * x1;
        	std::cout << "Detected Line #" << tube_id << ": y = (" << k << ") x + (" << b << ")" << std::flush;

        	delta_x = step / (sqrt(k * k + 1.));
        	if (x1 > x2)
        		delta_x *= -1.;
        	delta_y = k * delta_x;
    	}
    	else
    	{
    		std::cout << "Detected Line #" << tube_id << ": x = (" << x1 << ")" << std::flush;
    		delta_x = 0.;
    		delta_y = step;
    		if (y1 > y2)
    			delta_y *= -1.;
    	}
    	std::cout << "   ###   Delta x: " << delta_x << "    Delta y: " << delta_y << std::endl;

    	now_x = x1;
    	now_y = y1;
    	while (1)
    	{
    		now_x += delta_x;
    		now_y += delta_y;
    		if ( ((now_x > x1) && (now_x > x2)) || ((now_x < x1) && (now_x < x2))
    				|| ((now_y > y1) && (now_y > y2)) || ((now_y < y1) && (now_y < y2)) )
    			break;
    		else
    		{
    			// Avoid segments lying on the edges of the micrographs
    			if ( (now_x < half_box_size_pix)
    					|| (now_x > (Xdim - half_box_size_pix))
    					|| (now_y < half_box_size_pix)
    					|| (now_y > (Ydim - half_box_size_pix)) )
    			{
    				continue;
    			}

    			psi = (-180.) * atan(k) / PI;

    			MD_out.addObject();
    	    	MD_out.setValue(EMDL_IMAGE_COORD_X, now_x);
    	    	MD_out.setValue(EMDL_IMAGE_COORD_Y, now_y);
    	    	MD_out.setValue(EMDL_ORIENT_ROT, rot);
    	    	MD_out.setValue(EMDL_ORIENT_TILT, tilt);
    	    	MD_out.setValue(EMDL_ORIENT_PSI, psi);
    	    	MD_out.setValue(EMDL_ORIENT_ORIGIN_X, xoff);
    	    	MD_out.setValue(EMDL_ORIENT_ORIGIN_Y, yoff);
    	    	MD_out.setValue(EMDL_PARTICLE_HELICAL_TUBE_ID, (tube_id + 1));

    			nr_segments++;
    		}
    	}
    }

    if (nr_segments < 1)
    	std::cout << "WARNING: no segments extracted from file '" << fn_in << "'!" << std::endl;
    else
    	std::cout << "Total segments / particles extracted from file '" << fn_in << "' = " << nr_segments << " / " << (nr_segments * nr_asu) << std::endl;
    std::cout << "---------------------------------------------------" << std::endl;

    MD_out.write(fn_out);
    MD_out.clear();

    return nr_segments;
}

void extractCoordsForAllHelicalSegments_Multiple(
		FileName& suffix_in,
		FileName& suffix_out,
		int nr_asu,
		RFLOAT rise_ang,
		RFLOAT pixel_size_ang,
		RFLOAT Xdim,
		RFLOAT Ydim,
		RFLOAT box_size_pix)
{
	int nr_segments;
	FileName fns_in;
	std::vector<FileName> fn_in_list;

	fns_in = "*" + suffix_in;
	fns_in.globFiles(fn_in_list);
	std::cout << "Number of input files = " << fn_in_list.size() << std::endl;
	if (fn_in_list.size() < 1)
		REPORT_ERROR( (std::string) "helix.cpp::extractCoordsForAllHelicalSegments_Multiple(): No input files are found!");

	nr_segments = 0;
	for (int ii = 0; ii < fn_in_list.size(); ii++)
	{
		FileName fn_out;
		fn_out = fn_in_list[ii].beforeFirstOf(suffix_in) + suffix_out;
		nr_segments += extractCoordsForAllHelicalSegments(fn_in_list[ii], fn_out, nr_asu, rise_ang, pixel_size_ang, Xdim, Ydim, box_size_pix);
	}
	std::cout << "Total number of segments / particles from all files = "
			<< nr_segments << " / " << (nr_segments * nr_asu) << std::endl;
	return;
}

void combineParticlePriorsWithKaiLocalCTF(
		FileName& fn_priors,
		FileName& fn_local_ctf,
		FileName& fn_combined)
{
	MetaDataTable MD_priors, MD_local_ctf;
	std::vector<RFLOAT> x, y, rot, tilt, psi, xoff, yoff;
	RFLOAT _x, _y, _rot, _tilt, _psi, _xoff, _yoff;
	int ii;

	if ( (fn_priors.getFileFormat() != "star") || (fn_local_ctf.getFileFormat() != "star") || (fn_combined.getFileFormat() != "star") )
		REPORT_ERROR( (std::string) "helix.cpp::combineParticlePriorsWithKaiLocalCTF(): MetaDataTable should have .star extension.");
	if ( (fn_priors == fn_local_ctf) || (fn_local_ctf == fn_combined) || (fn_combined == fn_priors) )
		REPORT_ERROR( (std::string) "helix.cpp::combineParticlePriorsWithKaiLocalCTF(): File names must be different.");

	MD_priors.clear();
	MD_local_ctf.clear();
	MD_priors.read(fn_priors);
	MD_local_ctf.read(fn_local_ctf);
	if (MD_priors.numberOfObjects() != MD_local_ctf.numberOfObjects())
		REPORT_ERROR( (std::string) "helix.cpp::combineParticlePriorsWithKaiLocalCTF(): MetaDataTables to be combined are not of the same size.");

	if ( (!MD_priors.containsLabel(EMDL_IMAGE_COORD_X))
			|| (!MD_priors.containsLabel(EMDL_IMAGE_COORD_Y))
			|| (!MD_priors.containsLabel(EMDL_ORIENT_ROT))
			|| (!MD_priors.containsLabel(EMDL_ORIENT_TILT))
			|| (!MD_priors.containsLabel(EMDL_ORIENT_PSI))
			|| (!MD_priors.containsLabel(EMDL_ORIENT_ORIGIN_X))
			|| (!MD_priors.containsLabel(EMDL_ORIENT_ORIGIN_Y))
			|| (!MD_local_ctf.containsLabel(EMDL_MICROGRAPH_NAME))
			|| (!MD_local_ctf.containsLabel(EMDL_IMAGE_COORD_X))
			|| (!MD_local_ctf.containsLabel(EMDL_IMAGE_COORD_Y))
			|| (!MD_local_ctf.containsLabel(EMDL_CTF_VOLTAGE))
			|| (!MD_local_ctf.containsLabel(EMDL_CTF_DEFOCUSU))
			|| (!MD_local_ctf.containsLabel(EMDL_CTF_DEFOCUSV))
			|| (!MD_local_ctf.containsLabel(EMDL_CTF_DEFOCUS_ANGLE))
			|| (!MD_local_ctf.containsLabel(EMDL_CTF_CS))
			|| (!MD_local_ctf.containsLabel(EMDL_CTF_DETECTOR_PIXEL_SIZE))
			|| (!MD_local_ctf.containsLabel(EMDL_CTF_FOM))
			|| (!MD_local_ctf.containsLabel(EMDL_CTF_MAGNIFICATION))
			|| (!MD_local_ctf.containsLabel(EMDL_CTF_Q0))
			|| (!MD_local_ctf.containsLabel(EMDL_PARTICLE_AUTOPICK_FOM))
			)
		REPORT_ERROR( (std::string) "helix.cpp::combineParticlePriorsWithKaiLocalCTF(): Labels missing in MetaDataTables.");

	x.clear(); y.clear();
	rot.clear(); tilt.clear(); psi.clear();
	xoff.clear(); yoff.clear();

	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD_priors)
	{
		MD_priors.getValue(EMDL_IMAGE_COORD_X, _x);
		MD_priors.getValue(EMDL_IMAGE_COORD_Y, _y);
		MD_priors.getValue(EMDL_ORIENT_ROT, _rot);
		MD_priors.getValue(EMDL_ORIENT_TILT, _tilt);
		MD_priors.getValue(EMDL_ORIENT_PSI, _psi);
		MD_priors.getValue(EMDL_ORIENT_ORIGIN_X, _xoff);
		MD_priors.getValue(EMDL_ORIENT_ORIGIN_Y, _yoff);
		x.push_back(_x);
		y.push_back(_y);
		rot.push_back(_rot);
		tilt.push_back(_tilt);
		psi.push_back(_psi);
		xoff.push_back(_xoff);
		yoff.push_back(_yoff);
	}

	if (!MD_local_ctf.containsLabel(EMDL_ORIENT_ROT))
		MD_local_ctf.addLabel(EMDL_ORIENT_ROT);
	if (!MD_local_ctf.containsLabel(EMDL_ORIENT_TILT))
		MD_local_ctf.addLabel(EMDL_ORIENT_TILT);
	if (!MD_local_ctf.containsLabel(EMDL_ORIENT_PSI))
		MD_local_ctf.addLabel(EMDL_ORIENT_PSI);
	if (!MD_local_ctf.containsLabel(EMDL_ORIENT_ORIGIN_X))
		MD_local_ctf.addLabel(EMDL_ORIENT_ORIGIN_X);
	if (!MD_local_ctf.containsLabel(EMDL_ORIENT_ORIGIN_Y))
		MD_local_ctf.addLabel(EMDL_ORIENT_ORIGIN_Y);
	ii = -1;
	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD_local_ctf)
	{
		ii++;
		MD_local_ctf.getValue(EMDL_IMAGE_COORD_X, _x);
		MD_local_ctf.getValue(EMDL_IMAGE_COORD_Y, _y);
		if ( (fabs(x[ii] - _x) > 1.001) || (fabs(y[ii] - _y) > 1.001) )
			REPORT_ERROR( (std::string) "helix.cpp::combineParticlePriorsWithKaiLocalCTF(): Coordinates from the two MetaDataTables do not match.");
		MD_local_ctf.setValue(EMDL_IMAGE_COORD_X, x[ii]);
		MD_local_ctf.setValue(EMDL_IMAGE_COORD_Y, y[ii]);
		MD_local_ctf.setValue(EMDL_ORIENT_ROT, rot[ii]);
		MD_local_ctf.setValue(EMDL_ORIENT_TILT, tilt[ii]);
		MD_local_ctf.setValue(EMDL_ORIENT_PSI, psi[ii]);
		MD_local_ctf.setValue(EMDL_ORIENT_ORIGIN_X, xoff[ii]);
		MD_local_ctf.setValue(EMDL_ORIENT_ORIGIN_Y, yoff[ii]);
	}

	MD_local_ctf.write(fn_combined);
	return;
}

void addPriorsToParticleDataFile(
		FileName& fn_priors,
		FileName& fn_data,
		FileName& fn_out)
{
	MetaDataTable MD_priors, MD_data;
	bool have_rot, have_tilt, have_dxy;
	std::string str_img_name;
	RFLOAT x1, y1, x2, y2, rot, tilt, psi, dx, dy;
	std::map<std::string, MetaDataContainer*> priors_list;
	std::map<std::string, MetaDataContainer*>::iterator prior_iterator;
	MetaDataContainer* aux;

	if ( (fn_priors.getFileFormat() != "star") || (fn_data.getFileFormat() != "star") || (fn_out.getFileFormat() != "star") )
		REPORT_ERROR("helix.cpp::combineParticlePriorsWithClass2DDataStar(): MetaDataTable should have .star extension.");
	if ( (fn_priors == fn_data) || (fn_data == fn_out) || (fn_out == fn_priors) )
		REPORT_ERROR("helix.cpp::combineParticlePriorsWithClass2DDataStar(): File names must be different.");

	std::cout << " Loading input files..." << std::endl;
	MD_priors.clear();
	MD_data.clear();
	MD_priors.read(fn_priors);
	MD_data.read(fn_data);

	if ( (!MD_priors.containsLabel(EMDL_IMAGE_COORD_X))
			|| (!MD_priors.containsLabel(EMDL_IMAGE_COORD_Y))
			|| (!MD_priors.containsLabel(EMDL_IMAGE_NAME))
			|| (!MD_priors.containsLabel(EMDL_ORIENT_PSI))

			|| (!MD_data.containsLabel(EMDL_IMAGE_COORD_X))
			|| (!MD_data.containsLabel(EMDL_IMAGE_COORD_Y))
			|| (!MD_data.containsLabel(EMDL_IMAGE_NAME))
			|| (!MD_data.containsLabel(EMDL_CTF_DEFOCUSU))
			|| (!MD_data.containsLabel(EMDL_CTF_DEFOCUSV))
			|| (!MD_data.containsLabel(EMDL_CTF_DEFOCUS_ANGLE))
			|| (!MD_data.containsLabel(EMDL_CTF_VOLTAGE))
			|| (!MD_data.containsLabel(EMDL_CTF_CS))
			|| (!MD_data.containsLabel(EMDL_CTF_Q0))
			|| (!MD_data.containsLabel(EMDL_CTF_MAGNIFICATION))
			|| (!MD_data.containsLabel(EMDL_CTF_DETECTOR_PIXEL_SIZE)) )
	{
		REPORT_ERROR("helix.cpp::combineParticlePriorsWithClass2DDataStar(): Labels missing in MetaDataTables.");
	}

	have_rot = have_tilt = have_dxy = true;
	if (!MD_priors.containsLabel(EMDL_ORIENT_ROT))
		have_rot = false;
	if (!MD_priors.containsLabel(EMDL_ORIENT_TILT))
		have_tilt = false;
	if ( (!MD_priors.containsLabel(EMDL_ORIENT_ORIGIN_X))
			|| (!MD_priors.containsLabel(EMDL_ORIENT_ORIGIN_Y)) )
		have_dxy = false;

	std::cout << " Number of segments in the prior / data files = "
			<< MD_priors.numberOfObjects() << " / " << MD_data.numberOfObjects() << std::endl;
	std::cout << " Adding orientational and translational priors to the data file..." << std::endl;

	priors_list.clear();
	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD_priors)
	{
		aux = MD_priors.getObject();
		MD_priors.getValue(EMDL_IMAGE_NAME, str_img_name);
		priors_list.insert(std::pair<std::string, MetaDataContainer*>(str_img_name, aux));
	}

	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD_data)
	{
		MD_data.getValue(EMDL_IMAGE_NAME, str_img_name);
		prior_iterator = priors_list.find(str_img_name);
		if (prior_iterator != priors_list.end())
		{
			aux = prior_iterator->second;

			aux->getValue(EMDL_IMAGE_COORD_X, x1);
			aux->getValue(EMDL_IMAGE_COORD_Y, y1);
			MD_data.getValue(EMDL_IMAGE_COORD_X, x2);
			MD_data.getValue(EMDL_IMAGE_COORD_Y, y2);
			if ( (fabs(x1 - x2) > 1.) || (fabs(y1 - y2) > 1.) )
				REPORT_ERROR("helix.cpp::combineParticlePriorsWithClass2DDataStar(): Some pairs of particles have the same EMDL_IMAGE_NAME values but different XY coordinates. Check if the two files come from the same set of particles.");
			else
			{
				rot = dx = dy = 0.;
				tilt = 90.;
				if (have_rot)
					aux->getValue(EMDL_ORIENT_ROT, rot);
				if (have_tilt)
					aux->getValue(EMDL_ORIENT_TILT, tilt);
				aux->getValue(EMDL_ORIENT_PSI, psi);
				if (have_dxy)
				{
					aux->getValue(EMDL_ORIENT_ORIGIN_X, dx);
					aux->getValue(EMDL_ORIENT_ORIGIN_Y, dy);
				}

				MD_data.setValue(EMDL_ORIENT_ROT, rot);
				MD_data.setValue(EMDL_ORIENT_TILT, tilt);
				MD_data.setValue(EMDL_ORIENT_PSI, psi);
				MD_data.setValue(EMDL_ORIENT_ORIGIN_X, dx);
				MD_data.setValue(EMDL_ORIENT_ORIGIN_Y, dy);
			}
		}
		else
		{
			std::cout << "  EMDL_IMAGE_NAME = " << str_img_name << std::endl;
			REPORT_ERROR("helix.cpp::combineParticlePriorsWithClass2DDataStar(): Priors are missing for some particles in the data file.");
		}
	}

	std::cout << " Writing output file..." << std::endl;
	MD_data.write(fn_out);
	std::cout << " Done!" << std::endl;

	return;
}

void combineParticlePriorsWithKaiLocalCTF_Multiple(
		std::string& suffix_priors,
		std::string& suffix_local_ctf,
		std::string& suffix_combined)
{
	FileName fns_priors;
	std::vector<FileName> fn_priors_list;

	if ( (suffix_priors == suffix_local_ctf) || (suffix_priors == suffix_combined) || (suffix_combined == suffix_priors) )
		REPORT_ERROR( (std::string) "helix.cpp::combineParticlePriorsWithKaiLocalCTF_Multiple(): File names error!");

	fns_priors = "*" + suffix_priors;
	fns_priors.globFiles(fn_priors_list);
	std::cout << "Number of input files = " << fn_priors_list.size() << std::endl;
	if (fn_priors_list.size() < 1)
		REPORT_ERROR( (std::string) "helix.cpp::combineParticlePriorsWithKaiLocalCTF_Multiple(): No input files are found!");

	for (int ii = 0; ii < fn_priors_list.size(); ii++)
	{
		FileName fn_local_ctf, fn_combined;
		fn_local_ctf = fn_priors_list[ii].beforeFirstOf(suffix_priors) + suffix_local_ctf;
		fn_combined = fn_priors_list[ii].beforeFirstOf(suffix_priors) + suffix_combined;
		combineParticlePriorsWithKaiLocalCTF(fn_priors_list[ii], fn_local_ctf, fn_combined);
	}
	return;
}

void setNullAlignmentPriorsInDataStar(
		FileName& fn_in,
		FileName& fn_out,
		bool rewrite_tilt,
		bool rewrite_psi)
{
	MetaDataTable MD;
	bool rot, tilt, psi, xoff, yoff;
	if ( (fn_in.getFileFormat() != "star")
			|| (fn_out.getFileFormat() != "star") )
		REPORT_ERROR( (std::string) "helix.cpp::addNullAlignmentPriorsToDataStar(): MetaDataTable should have .star extension.");
	if (fn_in == fn_out)
		REPORT_ERROR( (std::string) "helix.cpp::addNullAlignmentPriorsToDataStar(): File names must be different.");

	MD.read(fn_in);
	rot = MD.containsLabel(EMDL_ORIENT_ROT);
	tilt = MD.containsLabel(EMDL_ORIENT_TILT);
	psi = MD.containsLabel(EMDL_ORIENT_PSI);
	xoff = MD.containsLabel(EMDL_ORIENT_ORIGIN_X);
	yoff = MD.containsLabel(EMDL_ORIENT_ORIGIN_Y);
	if (!rot)
		MD.addLabel(EMDL_ORIENT_ROT);
	if (!tilt)
		MD.addLabel(EMDL_ORIENT_TILT);
	if (!psi)
		MD.addLabel(EMDL_ORIENT_PSI);
	if (!xoff)
		MD.addLabel(EMDL_ORIENT_ORIGIN_X);
	if (!yoff)
		MD.addLabel(EMDL_ORIENT_ORIGIN_Y);
	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD)
	{
		//if (!rot)
			MD.setValue(EMDL_ORIENT_ROT, 0.);
		if ( (rewrite_tilt) || (!tilt) )
			MD.setValue(EMDL_ORIENT_TILT, 90.);
		if ( (rewrite_psi) || (!psi) )
			MD.setValue(EMDL_ORIENT_PSI, 0.);
		//if ( (rewrite_old_values) || (!xoff) )
			MD.setValue(EMDL_ORIENT_ORIGIN_X, 0.);
		//if ( (rewrite_old_values) || (!yoff) )
			MD.setValue(EMDL_ORIENT_ORIGIN_Y, 0.);
	}
	MD.write(fn_out);
	return;
}

void removeBadTiltHelicalSegmentsFromDataStar(
		FileName& fn_in,
		FileName& fn_out,
		RFLOAT max_dev_deg)
{
	MetaDataTable MD_in, MD_out;
	int nr_segments_old, nr_segments_new;
	RFLOAT tilt_deg;
	if ( (max_dev_deg < 0.) || (max_dev_deg > 89.) )
		REPORT_ERROR( (std::string) "helix.cpp::removeBadTiltParticlesFromDataStar(): Max deviations of tilt angles from 90 degree should be in the range of 0~89 degrees.");
	if ( (fn_in.getFileFormat() != "star") || (fn_out.getFileFormat() != "star") )
		REPORT_ERROR( (std::string) "helix.cpp::removeBadTiltParticlesFromDataStar(): MetaDataTable should have .star extension.");
	if (fn_in == fn_out)
		REPORT_ERROR( (std::string) "helix.cpp::removeBadTiltParticlesFromDataStar(): File names must be different.");

	MD_in.clear();
	MD_out.clear();
	MD_in.read(fn_in);
	if (!MD_in.containsLabel(EMDL_ORIENT_TILT))
		REPORT_ERROR( (std::string) "helix.cpp::removeBadTiltParticlesFromDataStar(): Input .star file contains no tilt angles.");

	nr_segments_old = nr_segments_new = 0;
	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD_in)
	{
		nr_segments_old++;
		MD_in.getValue(EMDL_ORIENT_TILT, tilt_deg);
		if (fabs(tilt_deg - 90.) < max_dev_deg)
		{
			nr_segments_new++;
			MD_out.addObject(MD_in.getObject());
		}
	}
	MD_out.write(fn_out);
	std::cout << " Number of segments (input / output) = " << nr_segments_old << " / " << nr_segments_new << std::endl;
	return;
}

int transformXimdispHelicalSegmentCoordsToStarFile(
		FileName& fn_in,
		FileName& fn_out,
		RFLOAT Xdim,
		RFLOAT Ydim,
		RFLOAT box_size_pix)
{
	if ( (box_size_pix < 2) || (Xdim < box_size_pix) || (Ydim < box_size_pix))
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalSegmentCoordsToStarFile(): Wrong dimensions or box size!");
	if (!fn_out.isStarFile())
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalSegmentCoordsToStarFile(): Output should be a star file!");

	char tmpstr[1000];
	int nr_segments_on_edges, nr_segments;
	RFLOAT x, y, psi_deg, half_box_size_pix;
	MetaDataTable MD;
	std::ifstream fin;
	std::string line;
	std::vector<std::string> words;

	half_box_size_pix = box_size_pix / 2.;
	fin.open(fn_in.c_str(), std::ios_base::in);
	if (fin.fail())
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalSegmentCoordsToStarFile(): Cannot open input file!");
	MD.clear();
	MD.addLabel(EMDL_IMAGE_COORD_X);
	MD.addLabel(EMDL_IMAGE_COORD_Y);
	MD.addLabel(EMDL_ORIENT_ROT);
	MD.addLabel(EMDL_ORIENT_TILT);
	MD.addLabel(EMDL_ORIENT_PSI);
	MD.addLabel(EMDL_ORIENT_ORIGIN_X);
	MD.addLabel(EMDL_ORIENT_ORIGIN_Y);
	nr_segments_on_edges = nr_segments = 0;
	getline(fin, line, '\n');
	while (getline(fin, line, '\n'))
	{
		words.clear();
		tokenize(line, words);
		if (words.size() != 3)
			REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalSegmentCoordsToStarFile(): Every line in input Ximdisp file should only contain x, y and psi angle!");
		x = textToFloat(words[0]);
		y = textToFloat(words[1]);
		psi_deg = textToFloat(words[2]);

		// Avoid segments lying on the edges of the micrographs
		if ( (x < half_box_size_pix) || (x > (Xdim - half_box_size_pix)) || (y < half_box_size_pix) || (y > (Ydim - half_box_size_pix)) )
		{
			nr_segments_on_edges++;
			continue;
		}

		nr_segments++;
		MD.addObject();
		MD.setValue(EMDL_IMAGE_COORD_X, x);
		MD.setValue(EMDL_IMAGE_COORD_Y, y);
		MD.setValue(EMDL_ORIENT_ROT, 0.);
		MD.setValue(EMDL_ORIENT_TILT, 90.);
		MD.setValue(EMDL_ORIENT_PSI, -psi_deg);
		MD.setValue(EMDL_ORIENT_ORIGIN_X, 0.);
		MD.setValue(EMDL_ORIENT_ORIGIN_Y, 0.);
	}
	MD.write(fn_out);
	std::cout << "Input Ximdisp .coords = " << fn_in.c_str() << ", output STAR file = " << fn_out.c_str()
			<< ", size of micrograph = " << Xdim << " * " << Ydim << ", box size = " << box_size_pix << ", "
			<< nr_segments_on_edges << " segments excluded, " << nr_segments << " segments left." << std::endl;
	return nr_segments;
}

void transformXimdispHelicalSegmentCoordsToStarFile_Multiple(
		FileName& suffix_coords,
		FileName& suffix_out,
		RFLOAT Xdim,
		RFLOAT Ydim,
		RFLOAT boxsize)
{
	int nr_segments;
	FileName fns_coords;
	std::vector<FileName> fn_coords_list;

	fns_coords = "*" + suffix_coords;
	fns_coords.globFiles(fn_coords_list);
	std::cout << "Number of input files = " << fn_coords_list.size() << std::endl;
	if (fn_coords_list.size() < 1)
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalCoordsToStarFile_Multiple(): No input files are found!");

	nr_segments = 0;
	for (int ii = 0; ii < fn_coords_list.size(); ii++)
	{
		FileName fn_out;
		fn_out = fn_coords_list[ii].beforeFirstOf(suffix_coords) + suffix_out;
		nr_segments += transformXimdispHelicalSegmentCoordsToStarFile(fn_coords_list[ii], fn_out, Xdim, Ydim, boxsize);
	}
	std::cout << "Total number of segments = " << nr_segments << std::endl;
	return;
}

int transformXimdispHelicalTubeCoordsToStarFile(
		FileName& fn_in,
		FileName& fn_out,
		int nr_asu,
		RFLOAT rise_ang,
		RFLOAT pixel_size_ang,
		RFLOAT Xdim,
		RFLOAT Ydim,
		RFLOAT box_size_pix)
{
	if ( (box_size_pix < 2) || (Xdim < box_size_pix) || (Ydim < box_size_pix))
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalTubeCoordsToStarFile(): Wrong dimensions or box size!");
	if (!fn_out.isStarFile())
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalTubeCoordsToStarFile(): Output should be a star file!");
	if (pixel_size_ang < 0.001)
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalTubeCoordsToStarFile(): Invalid pixel size!");
	RFLOAT step_pix = ((RFLOAT)(nr_asu)) * rise_ang / pixel_size_ang;
	if ( (nr_asu < 1) || (rise_ang < 0.001) || (step_pix < 0.001) )
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalTubeCoordsToStarFile(): Invalid helical rise or number of asymmetrical units!");

	std::cout << "NOTICE: Each .coords file should contain coordinates of only ONE rectangular box!" << std::endl;

	char tmpstr[1000];
	int nr_segments;
	RFLOAT xp, yp, dx, dy, x0, y0, x1, y1, dist, psi_deg, psi_rad, half_box_size_pix;
	MetaDataTable MD;
	std::ifstream fin;
	std::string line;
	std::vector<std::string> words;
	std::vector<RFLOAT> x, y;

	x.resize(4);
	y.resize(4);
	half_box_size_pix = box_size_pix / 2.;
	fin.open(fn_in.c_str(), std::ios_base::in);
	if (fin.fail())
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalTubeCoordsToStarFile(): Cannot open input file!");
	getline(fin, line, '\n');
	for (int iline = 0; iline < 4; iline++)
	{
		getline(fin, line, '\n');
		words.clear();
		tokenize(line, words);
		if (words.size() != 2)
			REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalTubeCoordsToStarFile(): Every line in input Ximdisp file should only contain x, y and psi angle!");
		x[iline] = textToFloat(words[0]);
		y[iline] = textToFloat(words[1]);
	}
	fin.close();

	x0 = (x[0] + x[1]) / 2.;
	y0 = (y[0] + y[1]) / 2.;
	x1 = (x[2] + x[3]) / 2.;
	y1 = (y[2] + y[3]) / 2.;
	psi_rad = atan2(y1 - y0, x1 - x0);
	psi_deg = RAD2DEG(psi_rad);
	dx = step_pix * cos(psi_rad);
	dy = step_pix * sin(psi_rad);
	dist = sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
	nr_segments = FLOOR(dist / step_pix);
	//if (nr_segments < 1)
	//	return 0;

	MD.clear();
	MD.addLabel(EMDL_IMAGE_COORD_X);
	MD.addLabel(EMDL_IMAGE_COORD_Y);
	MD.addLabel(EMDL_ORIENT_ROT);
	MD.addLabel(EMDL_ORIENT_TILT);
	MD.addLabel(EMDL_ORIENT_PSI);
	MD.addLabel(EMDL_ORIENT_ORIGIN_X);
	MD.addLabel(EMDL_ORIENT_ORIGIN_Y);
	int id_segment = -1;
	while (1)
	{
		id_segment++;
		if ((id_segment + 1) > nr_segments)
			break;
		xp = x0 + ((RFLOAT)(id_segment)) * dx;
		yp = y0 + ((RFLOAT)(id_segment)) * dy;

		// Avoid segments lying on the edges of the micrographs
		if ( (xp < half_box_size_pix) || (xp > (Xdim - half_box_size_pix)) || (yp < half_box_size_pix) || (yp > (Ydim - half_box_size_pix)) )
			continue;

		MD.addObject();
		MD.setValue(EMDL_IMAGE_COORD_X, xp);
		MD.setValue(EMDL_IMAGE_COORD_Y, yp);
		MD.setValue(EMDL_ORIENT_ROT, 0.);
		MD.setValue(EMDL_ORIENT_TILT, 90.);
		MD.setValue(EMDL_ORIENT_PSI, -psi_deg);
		MD.setValue(EMDL_ORIENT_ORIGIN_X, 0.);
		MD.setValue(EMDL_ORIENT_ORIGIN_Y, 0.);
	}
	MD.write(fn_out);

	std::cout << "Input Ximdisp .coords = " << fn_in.c_str() << ", output STAR file = " << fn_out.c_str()
			<< ", size of micrograph = " << Xdim << " * " << Ydim << ", box size = " << box_size_pix
			<< ", number of segments = " << MD.numberOfObjects() << "." << std::endl;
	return MD.numberOfObjects();
}

void transformXimdispHelicalTubeCoordsToStarFile_Multiple(
		FileName& suffix_coords,
		FileName& suffix_out,
		int nr_asu,
		RFLOAT rise_ang,
		RFLOAT pixel_size_ang,
		RFLOAT Xdim,
		RFLOAT Ydim,
		RFLOAT boxsize)
{
	int nr_segments;
	FileName fns_coords;
	std::vector<FileName> fn_coords_list;

	fns_coords = "*" + suffix_coords;
	fns_coords.globFiles(fn_coords_list);
	std::cout << "Number of input files = " << fn_coords_list.size() << std::endl;
	if (fn_coords_list.size() < 1)
		REPORT_ERROR( (std::string) "helix.cpp::transformXimdispHelicalTubeCoordsToStarFile_Multiple(): No input files are found!");

	nr_segments = 0;
	for (int ii = 0; ii < fn_coords_list.size(); ii++)
	{
		FileName fn_out;
		fn_out = fn_coords_list[ii].beforeFirstOf(suffix_coords) + suffix_out;
		nr_segments += transformXimdispHelicalTubeCoordsToStarFile(fn_coords_list[ii], fn_out, nr_asu, rise_ang, pixel_size_ang, Xdim, Ydim, boxsize);
	}
	std::cout << "Total number of segments = " << nr_segments << std::endl;
	return;
}

void divideHelicalSegmentsFromMultipleMicrographsIntoRandomHalves(
		FileName& fn_in,
		FileName& fn_out,
		int random_seed)
{
	RFLOAT ratio;
	std::string mic_name;
	int nr_swaps, nr_segments_subset1, nr_segments_subset2, helical_tube_id;
	bool divide_according_to_helical_tube_id;
	MetaDataTable MD;
	std::map<std::string, int> map_mics;
	std::map<std::string, int>::const_iterator ii_map;
	std::vector<std::pair<std::string, int> > vec_mics;

	if (fn_in == fn_out)
		REPORT_ERROR("helix.cpp::divideHelicalSegmentsFromMultipleMicrographsIntoRandomHalves(): File names error!");

	MD.clear();
	std::cout << " Loading input file..." << std::endl;
	MD.read(fn_in);
	init_random_generator(random_seed);

	if (!MD.containsLabel(EMDL_MICROGRAPH_NAME))
		REPORT_ERROR("helix.cpp::divideHelicalSegmentsFromMultipleMicrographsIntoRandomHalves(): Input MetadataTable should contain rlnMicrographName!");
	if (!MD.containsLabel(EMDL_PARTICLE_RANDOM_SUBSET))
		MD.addLabel(EMDL_PARTICLE_RANDOM_SUBSET);
	if (MD.containsLabel(EMDL_PARTICLE_HELICAL_TUBE_ID))
		divide_according_to_helical_tube_id = true;
	else
		divide_according_to_helical_tube_id = false;

	// Count micrograph names
	map_mics.clear();
	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD)
	{
		mic_name.clear();
		MD.getValue(EMDL_MICROGRAPH_NAME, mic_name);
		if (divide_according_to_helical_tube_id)
		{
			MD.getValue(EMDL_PARTICLE_HELICAL_TUBE_ID, helical_tube_id);
			if (helical_tube_id < 1)
				REPORT_ERROR("helix.cpp::divideHelicalSegmentsFromMultipleMicrographsIntoRandomHalves(): Helical tube ID should be positive integer!");
			mic_name += std::string("_TUBEID_");
			mic_name += std::string(integerToString(helical_tube_id));
		}
		if ((map_mics.insert(std::make_pair(mic_name, 1))).second == false)
			map_mics[mic_name]++;
	}
	vec_mics.clear();
	for (ii_map = map_mics.begin(); ii_map != map_mics.end(); ii_map++)
		vec_mics.push_back(*ii_map);

	if (random_seed != 0)
	{
		// Randomise
		// 1. Randomise total number of swaps needed
		nr_swaps = ROUND(rnd_unif(vec_mics.size(), 2. * vec_mics.size()));
		// DEBUG
		if (divide_according_to_helical_tube_id)
			std::cout << " Helical tubes= " << vec_mics.size() << ", nr_swaps= " << nr_swaps << std::endl;
		else
			std::cout << " Micrographs= " << vec_mics.size() << ", nr_swaps= " << nr_swaps << std::endl;
		// 2. Perform swaps
		for (int ii = 0; ii < nr_swaps; ii++)
		{
			int ptr_a, ptr_b;
			std::pair<std::string, int> tmp;
			ptr_a = ROUND(rnd_unif(0, vec_mics.size()));
			ptr_b = ROUND(rnd_unif(0, vec_mics.size()));
			if ( (ptr_a == ptr_b) || (ptr_a < 0 ) || (ptr_b < 0) || (ptr_a >= vec_mics.size()) || (ptr_b >= vec_mics.size()) )
				continue;
			tmp = vec_mics[ptr_a];
			vec_mics[ptr_a] = vec_mics[ptr_b];
			vec_mics[ptr_b] = tmp;

			// DEBUG
			//std::cout << " Swap mic_id= " << ptr_a << " with mic_id= " << ptr_b << "." << std::endl;
		}
	}

	// Divide micrographs into halves
	map_mics.clear();
	nr_segments_subset1 = nr_segments_subset2 = 0;
	for (int ii = 0; ii < vec_mics.size(); ii++)
	{
		if (nr_segments_subset1 < nr_segments_subset2)
		{
			nr_segments_subset1 += vec_mics[ii].second;
			vec_mics[ii].second = 1;
		}
		else
		{
			nr_segments_subset2 += vec_mics[ii].second;
			vec_mics[ii].second = 2;
		}
		map_mics.insert(vec_mics[ii]);
	}

	if ( (nr_segments_subset1 < 1) || (nr_segments_subset2 < 1) )
		REPORT_ERROR("helix.cpp::divideHelicalSegmentsFromMultipleMicrographsIntoRandomHalves(): Number of helical segments from one of the two half sets is 0!");
	ratio = (RFLOAT(nr_segments_subset1) / RFLOAT(nr_segments_subset2));
	if ( (ratio > 1.5) || ( (1. / ratio) > 1.5) )
		REPORT_ERROR("helix.cpp::divideHelicalSegmentsFromMultipleMicrographsIntoRandomHalves(): Numbers of helical segments from two half sets are extremely unbalanced!");

	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD)
	{
		mic_name.clear();
		MD.getValue(EMDL_MICROGRAPH_NAME, mic_name);
		if (divide_according_to_helical_tube_id)
		{
			MD.getValue(EMDL_PARTICLE_HELICAL_TUBE_ID, helical_tube_id);
			if (helical_tube_id < 1)
				REPORT_ERROR("helix.cpp::divideHelicalSegmentsFromMultipleMicrographsIntoRandomHalves(): Helical tube ID should be positive integer!");
			mic_name += std::string("_TUBEID_");
			mic_name += std::string(integerToString(helical_tube_id));
		}
		MD.setValue(EMDL_PARTICLE_RANDOM_SUBSET, map_mics[mic_name]);
	}

	// DEBUG
	std::cout << " Helical segments in two half sets = " << nr_segments_subset1 << ", " << nr_segments_subset2 << std::endl;
	std::cout << " Writing output file..." << std::endl;
	MD.write(fn_out);
	std::cout << " Done!" << std::endl;

	return;
}

void makeHelicalReference2D(
		MultidimArray<RFLOAT>& out,
		int box_size,
		RFLOAT particle_diameter_A,
		RFLOAT tube_diameter_A,
		RFLOAT pixel_size_A,
		bool is_tube_white)
{
	RFLOAT particle_diameter_pix, tube_diameter_pix, p2, dist2, r2, t2;

	if (pixel_size_A < 0.0001)
		REPORT_ERROR("helix.cpp::makeHelicalReference2D(): Invalid pixel size!");

	particle_diameter_pix = particle_diameter_A / pixel_size_A;
	tube_diameter_pix = tube_diameter_A / pixel_size_A;

	if (box_size < 10)
		REPORT_ERROR("helix.cpp::makeHelicalReference2D(): Invalid box size!");
	if ( (particle_diameter_pix < 2.) || (particle_diameter_pix > box_size) )
		REPORT_ERROR("helix.cpp::makeHelicalReference2D(): Invalid particle diameter!");
	if ( (tube_diameter_pix < 1.) || (tube_diameter_pix > particle_diameter_pix) )
		REPORT_ERROR("helix.cpp::makeHelicalReference2D(): Invalid tube diameter!");

	r2 = (particle_diameter_pix / 2.) * (particle_diameter_pix / 2.);
	t2 = (tube_diameter_pix / 2.) * (tube_diameter_pix / 2.);

	out.clear();
	out.resize(box_size, box_size);
	out.initZeros();
	out.setXmippOrigin();

	FOR_ALL_ELEMENTS_IN_ARRAY2D(out)
	{
		dist2 = (RFLOAT)(i * i + j * j);
		p2 = (RFLOAT)(j * j);
		if ( (dist2 < r2) && (p2 < t2) )
		{
			if (is_tube_white)
				A2D_ELEM(out, i, j) = 1.;
			else
				A2D_ELEM(out, i, j) = (-1.);
		}
	}
	return;
};

void makeHelicalReference3D(
		MultidimArray<RFLOAT>& out,
		int box_size,
		RFLOAT pixel_size_A,
		RFLOAT twist_deg,
		RFLOAT rise_A,
		RFLOAT tube_diameter_A,
		RFLOAT particle_diameter_A,
		int sym_Cn)
{
	RFLOAT rise_pix, tube_diameter_pix, particle_diameter_pix, particle_radius_pix;
	int particle_radius_max_pix;
	Matrix2D<RFLOAT> matrix1, matrix2;
	Matrix1D<RFLOAT> vec0, vec1, vec2;
	out.clear();

	if (box_size < 5)
		REPORT_ERROR("helix.cpp::makeHelicalReference3D(): Box size should be larger than 5!");
	if (pixel_size_A < 0.001)
		REPORT_ERROR("helix.cpp::makeHelicalReference3D(): Pixel size (in Angstroms) should be larger than 0.001!");
	if ( (fabs(twist_deg) < 0.01) || (fabs(twist_deg) > 179.99) || ((rise_A / pixel_size_A) < 0.001) )
		REPORT_ERROR("helix.cpp::makeHelicalReference3D(): Wrong helical twist or rise!");
	if (sym_Cn < 1)
		REPORT_ERROR("helix.cpp::makeHelicalReference3D(): Rotation symmetry Cn is invalid (n should be positive integer)!");

	rise_pix = rise_A / pixel_size_A;
	tube_diameter_pix = tube_diameter_A / pixel_size_A;
	particle_diameter_pix = particle_diameter_A / pixel_size_A;
	particle_radius_pix = particle_diameter_pix / 2.;
	particle_radius_max_pix = (CEIL(particle_diameter_pix / 2.)) + 1;

	if (particle_diameter_pix < 2.)
		REPORT_ERROR("helix.cpp::makeHelicalReference3D(): Particle diameter should be larger than 2 pixels!");
	if ( (tube_diameter_pix < 0.001) || (tube_diameter_pix > (RFLOAT)(box_size)) )
		REPORT_ERROR("helix.cpp::makeHelicalReference3D(): Tube diameter should be larger than 1 pixel and smaller than box size!");

	out.resize(box_size, box_size, box_size);
	out.initZeros();
	out.setXmippOrigin();

	RFLOAT x0, y0, z0;
	x0 = tube_diameter_pix / 2.;
	y0 = 0.;
	z0 = (RFLOAT)(FIRST_XMIPP_INDEX(box_size));
	vec0.clear();
	vec0.resize(2);
	XX(vec0) = x0;
	YY(vec0) = y0;
	vec1.clear();
	vec1.resize(2);
	vec2.clear();
	vec2.resize(2);

	for (int id = 0; ;id++)
	{
		RFLOAT rot1_deg, x1, y1, z1;
		rot1_deg = (RFLOAT)(id) * twist_deg;
		rotation2DMatrix(rot1_deg, matrix1, false);
		vec1 = matrix1 * vec0;

		x1 = XX(vec1);
		y1 = YY(vec1);
		z1 = z0 + (RFLOAT)(id) * rise_pix;
		if (z1 > LAST_XMIPP_INDEX(box_size))
			break;

		for (int Cn = 0; Cn < sym_Cn; Cn++)
		{
			RFLOAT rot2_deg, x2, y2, z2;
			rot2_deg = (360.) * (RFLOAT)(Cn) / (RFLOAT)(sym_Cn);
			rotation2DMatrix(rot2_deg, matrix2, false);
			vec2 = matrix2 * vec1;
			x2 = XX(vec2);
			y2 = YY(vec2);
			z2 = z1;

			for (int dx = -particle_radius_max_pix; dx <= particle_radius_max_pix; dx++)
			{
				for (int dy = -particle_radius_max_pix; dy <= particle_radius_max_pix; dy++)
				{
					for (int dz = -particle_radius_max_pix; dz <= particle_radius_max_pix; dz++)
					{
						RFLOAT _x, _y, _z, dist, val_old, val_new;
						int x3, y3, z3;

						x3 = ROUND(x2) + dx;
						y3 = ROUND(y2) + dy;
						z3 = ROUND(z2) + dz;

						if ( (x3 < FIRST_XMIPP_INDEX(box_size)) || (x3 > LAST_XMIPP_INDEX(box_size))
								|| (y3 < FIRST_XMIPP_INDEX(box_size)) || (y3 > LAST_XMIPP_INDEX(box_size))
								|| (z3 < FIRST_XMIPP_INDEX(box_size)) || (z3 > LAST_XMIPP_INDEX(box_size)) )
							continue;

						_x = (RFLOAT)(x3) - x2;
						_y = (RFLOAT)(y3) - y2;
						_z = (RFLOAT)(z3) - z2;

						dist = sqrt(_x * _x + _y * _y + _z * _z);
						if (dist > particle_radius_pix)
							continue;

						val_old = A3D_ELEM(out, z3, y3, x3);
						val_new = 0.5 + 0.5 * cos(PI * dist / particle_radius_pix);
						if (val_new > val_old)
							A3D_ELEM(out, z3, y3, x3) = val_new;
					}
				}
			}
		}
	}

	return;
}

void makeHelicalReconstructionStarFileFromSingle2DClassAverage(
		FileName& fn_in_class2D,
		FileName& fn_out_star,
		RFLOAT pixel_size_A,
		RFLOAT twist_deg,
		RFLOAT rise_A,
		RFLOAT tilt_deg,
		RFLOAT psi_deg,
		int nr_projections)
{
	RFLOAT twist_deg_acc, offset_pix_acc, rise_pix;
	MetaDataTable MD;

	if (fn_out_star.getExtension() != "star")
		REPORT_ERROR("helix.cpp::makeReconstructionStarFileFromSingle2DClassAverage: Output file should be in .star format!");
	if (pixel_size_A < 0.001)
		REPORT_ERROR("helix.cpp::makeReconstructionStarFileFromSingle2DClassAverage: Pixel size (in Angstroms) should be larger than 0.001!");
	if ((rise_A / pixel_size_A) < 0.001)
		REPORT_ERROR("helix.cpp::makeReconstructionStarFileFromSingle2DClassAverage: Wrong helical rise!");
	if (nr_projections < 1)
		REPORT_ERROR("helix.cpp::makeReconstructionStarFileFromSingle2DClassAverage: Number of projections should be larger than 1!");

	rise_pix = rise_A / pixel_size_A;

	MD.clear();
	MD.addLabel(EMDL_IMAGE_NAME);
	MD.addLabel(EMDL_ORIENT_ROT);
	MD.addLabel(EMDL_ORIENT_TILT);
	MD.addLabel(EMDL_ORIENT_PSI);
	MD.addLabel(EMDL_ORIENT_ORIGIN_X);
	MD.addLabel(EMDL_ORIENT_ORIGIN_Y);

	twist_deg_acc = -twist_deg;
	offset_pix_acc = -rise_pix;
	for (int ii = 0; ii < nr_projections; ii++)
	{
		twist_deg_acc += twist_deg;
		offset_pix_acc += rise_pix;

		MD.addObject();
		MD.setValue(EMDL_IMAGE_NAME, fn_in_class2D);
		MD.setValue(EMDL_ORIENT_ROT, twist_deg_acc);
		MD.setValue(EMDL_ORIENT_TILT, tilt_deg);
		MD.setValue(EMDL_ORIENT_PSI, psi_deg);
		MD.setValue(EMDL_ORIENT_ORIGIN_X, 0.);
		MD.setValue(EMDL_ORIENT_ORIGIN_Y, offset_pix_acc);
	}

	MD.write(fn_out_star);
	return;
}

void divideStarFile(
		FileName& fn_in,
		int nr)
{
	FileName fn_out;
	MetaDataTable MD_in, MD;
	int total_lines, nr_lines, line_id, file_id;

	if (fn_in.getExtension() != "star")
		REPORT_ERROR("helix.cpp::divideStarFile: Input file should be in .star format!");
	if ( (nr < 2) || (nr > 999999) )
		REPORT_ERROR("helix.cpp::divideStarFile: The number of output files should be within range 2~999999!");

	std::cout << " Loading input file: " << fn_in << " ..." << std::endl;
	MD_in.clear();
	MD_in.read(fn_in);
	std::cout << " Input file loaded." << std::endl;

	total_lines = MD_in.numberOfObjects();
	if (total_lines < nr)
		REPORT_ERROR("helix.cpp::divideStarFile: The number of total objects is smaller than the number of output files!");
	nr_lines = total_lines / nr;
	std::cout << " Total objects = " << total_lines << ", number of output files = " << nr << std::endl;
	std::cout << " Writing output files..." << std::endl;

	line_id = 0;
	file_id = 0;
	MD.clear();
	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD_in)
	{
		line_id++;
		MD.addObject(MD_in.getObject());
		if ( ((line_id % nr_lines) == 0) && ((total_lines - line_id) > nr_lines) )
		{
			file_id++;
			fn_out = fn_in.withoutExtension() + "_sub" + integerToString(file_id, 6, '0');
			fn_out = fn_out.addExtension("star");
			MD.write(fn_out);
			MD.clear();
		}
	}
	if (MD.numberOfObjects() != 0)
	{
		file_id++;
		fn_out = fn_in.withoutExtension() + "_sub" + integerToString(file_id, 6, '0');
		fn_out = fn_out.addExtension("star");
		MD.write(fn_out);
		MD.clear();
	}
	std::cout << " Done!" << std::endl;
	return;
}

void mergeStarFiles(FileName& fn_in)
{
	int file_id;
	std::vector<FileName> fns_list;
	MetaDataTable MD_combined, MD_in;
	FileName fn_root, fn_out;

	fns_list.clear();
	fn_root = "*" + fn_in + "*";

	if (fn_root.globFiles(fns_list) < 2)
		REPORT_ERROR("helix.cpp::combineStarFiles: Only 0 or 1 input file! There is no need to combine!");
	for (file_id = 0; file_id < fns_list.size(); file_id++)
	{
		if (fns_list[file_id].getExtension() != "star")
			REPORT_ERROR("helix.cpp::combineStarFiles: Input files should have STAR extension!");
	}

	std::cout << " Combining STAR files: " << std::endl;
	std::cout << " BEWARE: All STAR files should contain the same header!" << std::endl;
	for (file_id = 0; file_id < fns_list.size(); file_id++)
		std::cout << "  " << fns_list[file_id] << std::endl;
	std::cout << " Loading input files..." << std::endl;

	MD_combined.clear();
	for (file_id = 0; file_id < fns_list.size(); file_id++)
	{
		MD_in.clear();
		MD_in.read(fns_list[file_id]);
		FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD_in)
		{
			MD_combined.addObject(MD_in.getObject());
		}
		std::cout << "  " << MD_combined.numberOfObjects() << " objects loaded." << std::endl;
	}

	std::cout << " Writing the combined output file..." << std::endl;
	fn_out = fn_in + "_combined.star";
	MD_combined.write(fn_out);
	std::cout << " Done!" << std::endl;

	return;
}

void sortHelicalTubeID(MetaDataTable& MD)
{
	std::string str_particle_fullname, str_particle_name, str_comment, str_particle_id, str_tube_id;
	int int_tube_id;
	bool contain_helicalTubeID;

	if ( (!MD.containsLabel(EMDL_IMAGE_NAME)) || (MD.containsLabel(EMDL_COMMENT)) )
		REPORT_ERROR("helix.cpp::sortHelicalTubeID: MetaDataTable should contain rlnImageName but not rlnComment!");
	contain_helicalTubeID = MD.containsLabel(EMDL_PARTICLE_HELICAL_TUBE_ID);

	MD.addLabel(EMDL_COMMENT);
	int_tube_id = 0;
	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD)
	{
		MD.getValue(EMDL_IMAGE_NAME, str_particle_fullname);
		if (contain_helicalTubeID)
			MD.getValue(EMDL_PARTICLE_HELICAL_TUBE_ID, int_tube_id);

		str_particle_name = str_particle_fullname.substr(str_particle_fullname.find("@") + 1);
		str_particle_id = str_particle_fullname.substr(0, str_particle_fullname.find("@"));
		str_comment = str_particle_name + integerToString(int_tube_id, 10) + str_particle_id;

		// DEBUG
		//std::cout << str_comment << std::endl;

		MD.setValue(EMDL_COMMENT, str_comment);
	}
	MD.newSort(EMDL_COMMENT);
	MD.deactivateLabel(EMDL_COMMENT);

	return;
}

void simulateHelicalSegments(
		FileName& fn_out,
		int nr_subunits,
		int nr_asu,
		int nr_tubes,
		RFLOAT twist_deg,
		RFLOAT sigma_psi,
		RFLOAT sigma_tilt)
{
	int nr_segments, tube_id;
	RFLOAT rot, psi, tilt;
	MetaDataTable MD;
	FileName fn_mic;

	// TODO: raise error if nr_asu<0 or too big, n too small!
	if ( (nr_tubes < 2) || (nr_subunits < 100) || (nr_asu < 1)
			|| (((nr_subunits / nr_asu) / nr_tubes) < 5) || ((nr_subunits / nr_asu) > 999999) )
		REPORT_ERROR("helix.cpp::simulateHelicalSegments: Errors in the number of tubes, asymmetrical units or total subunits!");
	if ( (sigma_psi < 0.) || (sigma_psi > 10.) || (sigma_tilt < 0.) || (sigma_tilt > 10.) )
		REPORT_ERROR("helix.cpp::simulateHelicalSegments: Errors in sigma_psi or sigma_tilt!");
	if ( (fabs(twist_deg) < 0.001) || (fabs(twist_deg) > 180.))
		REPORT_ERROR("helix.cpp::simulateHelicalSegments: Error in helical twist!");

	nr_segments = nr_subunits / nr_asu;

	MD.clear();
    MD.addLabel(EMDL_ORIENT_ROT);
    MD.addLabel(EMDL_ORIENT_TILT);
    MD.addLabel(EMDL_ORIENT_PSI);
    MD.addLabel(EMDL_ORIENT_ORIGIN_X);
    MD.addLabel(EMDL_ORIENT_ORIGIN_Y);
    MD.addLabel(EMDL_PARTICLE_HELICAL_TUBE_ID);
    MD.addLabel(EMDL_IMAGE_NAME);
    MD.addLabel(EMDL_MICROGRAPH_NAME);

    tube_id = 0;
	for (int id = 0; id < nr_segments; id++)
	{
		if ( ( (id % (nr_segments / nr_tubes)) == 0 )
				&& ( (nr_segments - id) >= (nr_segments / nr_tubes) ) )
		{
			tube_id++;
			tilt = rnd_unif(85., 95.);
			rot = rnd_unif(0.01, 359.99);
			psi = rnd_unif(-179.99, 179.99);
		}

		rot += twist_deg * ((RFLOAT)(nr_asu));
		rot = realWRAP(rot, -180., 180.);

		MD.addObject();
    	MD.setValue(EMDL_ORIENT_ROT, rot);
    	MD.setValue(EMDL_ORIENT_TILT, realWRAP(tilt + rnd_gaus(0., sigma_tilt), 0., 180.) );
    	MD.setValue(EMDL_ORIENT_PSI, realWRAP(psi + rnd_gaus(0., sigma_psi), -180., 180.) );
    	MD.setValue(EMDL_ORIENT_ORIGIN_X, 0.);
    	MD.setValue(EMDL_ORIENT_ORIGIN_Y, 0.);
    	MD.setValue(EMDL_PARTICLE_HELICAL_TUBE_ID, tube_id);
    	fn_mic.compose((id + 1), "M.mrc");
    	MD.setValue(EMDL_IMAGE_NAME, fn_mic);
    	MD.setValue(EMDL_MICROGRAPH_NAME, (std::string)("M.mrc"));
	}
	MD.write(fn_out);
	return;
};

void outputHelicalSymmetryStatus(
		int iter,
		RFLOAT rise_initial_A,
		RFLOAT rise_min_A,
		RFLOAT rise_max_A,
		RFLOAT twist_initial_deg,
		RFLOAT twist_min_deg,
		RFLOAT twist_max_deg,
		bool do_local_search_helical_symmetry,
		std::vector<RFLOAT>& rise_A,
		std::vector<RFLOAT>& twist_deg,
		RFLOAT rise_A_half1,
		RFLOAT rise_A_half2,
		RFLOAT twist_deg_half1,
		RFLOAT twist_deg_half2,
		bool do_split_random_halves,
		std::ostream& out)
{
	if (iter < 1)
		REPORT_ERROR("helix.cpp::outputHelicalSymmetryStatus(): BUG iteration id cannot be less than 1!");
	if ( (do_local_search_helical_symmetry) && (iter > 1) )
	{
		out << " Local searches of helical twist from " << twist_min_deg << " to " << twist_max_deg << " degrees, rise from " << rise_min_A << " to " << rise_max_A << " Angstroms." << std::endl;
	}
	else
	{
		out << " For all classes, helical twist = " << twist_initial_deg << " degrees, rise = " << rise_initial_A << " Angstroms." << std::endl;
		return;
	}

	if (do_split_random_halves)
	{
		RFLOAT twist_avg_deg = (twist_deg_half1 + twist_deg_half2) / 2.;
		RFLOAT rise_avg_A = (rise_A_half1 + rise_A_half2) / 2.;

		// TODO: raise a warning if two sets of helical parameters are >1% apart?
		out << " (Half 1) Refined helical twist = " << twist_deg_half1 << " degrees, rise = " << rise_A_half1 << " Angstroms." << std::endl;
		out << " (Half 2) Refined helical twist = " << twist_deg_half2 << " degrees, rise = " << rise_A_half2 << " Angstroms." << std::endl;
		out << " Averaged helical twist = " << twist_avg_deg << " degrees, rise = " << rise_avg_A << " Angstroms." << std::endl;
		return;
	}
	else
	{
		if ( (rise_A.size() != twist_deg.size()) || (rise_A.size() < 1) )
			REPORT_ERROR("helix.cpp::outputHelicalSymmetryStatus(): BUG vectors rise_A and twist_deg are not of the same size!");
		for (int iclass = 0; iclass < rise_A.size(); iclass++)
		{
			out << " (Class " << (iclass + 1) << ") Refined helical twist = " << twist_deg[iclass] << " degrees, rise = " << rise_A[iclass] << " Angstroms." << std::endl;
		}
	}
}

void excludeLowCTFCCMicrographs(
		FileName& fn_in,
		FileName& fn_out,
		RFLOAT cc_min,
		RFLOAT EPA_lowest_res)
{
	EMDLabel EMDL_ctf_EPA_final_resolution = EMDL_POSTPROCESS_FINAL_RESOLUTION;
	bool contain_EPA_res;
	MetaDataTable MD_in, MD_out;
	int nr_mics_old, nr_mics_new;
	RFLOAT cc, EPA_res;
	if ( (fn_in.getFileFormat() != "star") || (fn_out.getFileFormat() != "star") )
		REPORT_ERROR( (std::string) "helix.cpp::excludeLowCTFCCMicrographs(): MetaDataTable should have .star extension.");
	if (fn_in == fn_out)
		REPORT_ERROR( (std::string) "helix.cpp::excludeLowCTFCCMicrographs(): File names must be different.");

	MD_in.clear();
	MD_in.read(fn_in);
	if ( (!MD_in.containsLabel(EMDL_CTF_DEFOCUSU))
			|| (!MD_in.containsLabel(EMDL_CTF_DEFOCUSV))
			|| (!MD_in.containsLabel(EMDL_CTF_DEFOCUS_ANGLE))
			|| (!MD_in.containsLabel(EMDL_CTF_VOLTAGE))
			|| (!MD_in.containsLabel(EMDL_CTF_CS))
			|| (!MD_in.containsLabel(EMDL_CTF_Q0))
			|| (!MD_in.containsLabel(EMDL_CTF_MAGNIFICATION))
			|| (!MD_in.containsLabel(EMDL_CTF_DETECTOR_PIXEL_SIZE))
			|| (!MD_in.containsLabel(EMDL_CTF_FOM)) )
		REPORT_ERROR( (std::string) "helix.cpp::removeBadTiltParticlesFromDataStar(): Input STAR file should contain CTF information.");

	contain_EPA_res = MD_in.containsLabel(EMDL_ctf_EPA_final_resolution);

	nr_mics_old = nr_mics_new = 0;
	MD_out.clear();
	FOR_ALL_OBJECTS_IN_METADATA_TABLE(MD_in)
	{
		nr_mics_old++;
		MD_in.getValue(EMDL_CTF_FOM, cc);
		MD_in.getValue(EMDL_ctf_EPA_final_resolution, EPA_res);
		if (cc > cc_min)
		{
			if ( (contain_EPA_res) && (EPA_res > EPA_lowest_res) )
			{}
			else
			{
				nr_mics_new++;
				MD_out.addObject(MD_in.getObject());
			}
		}
	}

	std::cout << " Number of micrographs (input / output) = " << nr_mics_old << " / " << nr_mics_new << std::endl;
	if (MD_out.numberOfObjects() < 1)
		std::cout << " No micrographs in output file!" << std::endl;
	else
		MD_out.write(fn_out);
	return;
}