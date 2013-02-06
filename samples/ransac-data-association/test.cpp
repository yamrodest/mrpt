/* +---------------------------------------------------------------------------+
   |                 The Mobile Robot Programming Toolkit (MRPT)               |
   |                                                                           |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2013, Individual contributors, see AUTHORS file        |
   | Copyright (c) 2005-2013, MAPIR group, University of Malaga                |
   | Copyright (c) 2012-2013, University of Almeria                            |
   | All rights reserved.                                                      |
   |                                                                           |
   | Redistribution and use in source and binary forms, with or without        |
   | modification, are permitted provided that the following conditions are    |
   | met:                                                                      |
   |    * Redistributions of source code must retain the above copyright       |
   |      notice, this list of conditions and the following disclaimer.        |
   |    * Redistributions in binary form must reproduce the above copyright    |
   |      notice, this list of conditions and the following disclaimer in the  |
   |      documentation and/or other materials provided with the distribution. |
   |    * Neither the name of the copyright holders nor the                    |
   |      names of its contributors may be used to endorse or promote products |
   |      derived from this software without specific prior written permission.|
   |                                                                           |
   | THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       |
   | 'AS IS' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED |
   | TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR|
   | PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE |
   | FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL|
   | DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR|
   |  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)       |
   | HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,       |
   | STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN  |
   | ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           |
   | POSSIBILITY OF SUCH DAMAGE.                                               |
   +---------------------------------------------------------------------------+ */

#include <mrpt/base.h>
#include <mrpt/scanmatching.h>
#include <mrpt/slam/CSimplePointsMap.h>
#include <mrpt/gui.h>
#include <mrpt/opengl.h>

// ============= PARAMETERS ===================

const size_t NUM_OBSERVATIONS_TO_SIMUL = 10; 
const size_t RANSAC_MINIMUM_INLIERS    = 9;  // Min. # of inliers to accept

#define LOAD_MAP_FROM_FILE  0  // 1: load from "sMAP_FILE", 0: random map.
#define SHOW_POINT_LABELS   0

const float normalizationStd = 0.15f; // 1 sigma noise (meters)
const float ransac_mahalanobisDistanceThreshold = 5.0f;
const size_t MINIMUM_RANSAC_ITERS = 100000;

#if !LOAD_MAP_FROM_FILE
	const size_t NUM_MAP_FEATS = 100;
	const double MAP_SIZE_X    = 50;
	const double MAP_SIZE_Y    = 25;
#else
	// Expected format of the 2D map is, for each line (one per landmark):
	//  ID X Y
	const std::string sMAP_FILE = string("./DLRMap.txt");
#endif

// ==============================================



using namespace mrpt;
using namespace mrpt::utils;
using namespace mrpt::math;
using namespace mrpt::random;
using namespace mrpt::slam;
using namespace std;


struct TObs
{
	size_t ID; // Ground truth ID
	double x,y;
};

// ------------------------------------------------------
//				TestRANSAC
// ------------------------------------------------------
void TestRANSAC()
{
	mrpt::gui::CDisplayWindow3D win("MRPT example: ransac-data-association",800,600);

	mrpt::utils::CTimeLogger timelog;  // For dumping stats at the end
	mrpt::utils::CTicTac     timer;


	randomGenerator.randomize(); // randomize with time

	// --------------------------------
	// Load feature map:
	// --------------------------------
	CSimplePointsMap  the_map;
#if LOAD_MAP_FROM_FILE
	{
		CMatrixDouble  M;
		M.loadFromTextFile(sMAP_FILE); // Launch except. on error
		ASSERT_(M.getColCount()==3 && M.getRowCount()>2)

		const size_t nPts = M.getRowCount();
		the_map.resize(nPts);
		for (size_t i=0;i<nPts;i++)
			the_map.setPoint(i,M(i,1),M(i,2));
	}
#else
	// Generate random MAP:
	the_map.resize(NUM_MAP_FEATS);
	for (size_t i=0;i<NUM_MAP_FEATS;i++)
	{
		the_map.setPoint(i, 
			randomGenerator.drawUniform(0,MAP_SIZE_X),
			randomGenerator.drawUniform(0,MAP_SIZE_Y)
			);
	}
#endif

	const size_t nMapPts = the_map.size();
	cout << "Loaded/generated map with " << nMapPts << " landmarks.\n";


	const size_t nObs=NUM_OBSERVATIONS_TO_SIMUL;

	mrpt::opengl::CPointCloudPtr gl_obs_map = mrpt::opengl::CPointCloud::Create();
	mrpt::opengl::CPointCloudPtr gl_result = mrpt::opengl::CPointCloud::Create();
	mrpt::opengl::CSetOfObjectsPtr gl_obs = mrpt::opengl::CSetOfObjects::Create();
	mrpt::opengl::CSetOfObjectsPtr gl_obs_txts = mrpt::opengl::CSetOfObjects::Create();
	mrpt::opengl::CSetOfLinesPtr gl_lines = mrpt::opengl::CSetOfLines::Create();
	{
		mrpt::opengl::COpenGLScenePtr &scene = win.get3DSceneAndLock();

		scene->getViewport("main")->setCustomBackgroundColor( TColorf(0.8f,0.8f,0.8f));
		win.setCameraPointingToPoint( MAP_SIZE_X*0.5, MAP_SIZE_Y*0.5, 0);
		win.setCameraZoom( 2*MAP_SIZE_X );

		//
		scene->insert( mrpt::opengl::stock_objects::CornerXYZ() );

		//
		mrpt::opengl::CPointCloudPtr gl_map = mrpt::opengl::CPointCloud::Create();
		gl_map->loadFromPointsMap(&the_map);
		gl_map->setColor(0,0,1);
		gl_map->setPointSize(3);

		scene->insert(gl_map);

#if SHOW_POINT_LABELS
		for (size_t i=0;i<the_map.size();i++)
		{
			mrpt::opengl::CTextPtr gl_txt = mrpt::opengl::CText::Create( mrpt::format("%u",static_cast<unsigned int>(i)) );
			double x,y;
			the_map.getPoint(i,x,y);
			gl_txt->setLocation(x+0.05,y+0.05,0.01);

			scene->insert(gl_txt);
		}
#endif

		//
		scene->insert(gl_lines);

		//
		gl_obs_map->setColor(1,0,0);
		gl_obs_map->setPointSize(5);

		gl_result->setColor(0,1,0);
		gl_result->setPointSize(4);

		//
		gl_obs->insert( mrpt::opengl::stock_objects::CornerXYZ(0.6) );
		gl_obs->insert(gl_obs_map);
		gl_obs->insert(gl_obs_txts);
		scene->insert(gl_obs);
		scene->insert(gl_result);

		win.unlockAccess3DScene();
		win.repaint();
	}


	// Repeat for each set of observations in the input file
	while (win.isOpen())
	{
		// Read the observations themselves:
		vector<TObs> observations;
		observations.resize(nObs);

		const mrpt::poses::CPose2D  GT_pose(
			mrpt::random::randomGenerator.drawUniform(-10,10+MAP_SIZE_X),
			mrpt::random::randomGenerator.drawUniform(-10,10+MAP_SIZE_Y),
			mrpt::random::randomGenerator.drawUniform(-M_PI,M_PI) );

		const mrpt::poses::CPose2D  GT_pose_inv = -GT_pose;

		std::vector<std::pair<size_t,float> > idxs;
		the_map.kdTreeRadiusSearch2D(GT_pose.x(),GT_pose.y(), 1000, idxs);
		ASSERT_(idxs.size()>=nObs)

		for (size_t i=0;i<nObs;i++)
		{
			double gx,gy;
			the_map.getPoint(idxs[i].first, gx,gy);

			double lx,ly;
			GT_pose_inv.composePoint(gx,gy, lx,ly);

			observations[i].ID = idxs[i].first;
			observations[i].x = lx + mrpt::random::randomGenerator.drawGaussian1D(0,normalizationStd);
			observations[i].y = ly + mrpt::random::randomGenerator.drawGaussian1D(0,normalizationStd);
		}

		// ----------------------------------------------------
		// Generate list of individual-compatible pairings
		// ----------------------------------------------------
		TMatchingPairList all_correspondences;

		all_correspondences.reserve(nMapPts*nObs);

		// ALL possibilities: 
		for (size_t j=0;j<nObs;j++)
		{
			TMatchingPair match;

			match.other_idx = j;
			match.other_x = observations[j].x;
			match.other_y = observations[j].y;

			for (size_t i=0;i<nMapPts;i++)
			{
				match.this_idx = i;
				the_map.getPoint(i, match.this_x, match.this_y );

				all_correspondences.push_back(match);
			}
		}
		cout << "Generated " << all_correspondences.size() << " potential pairings.\n";

		// ----------------------------------------------------
		//  Run RANSAC-based D-A
		// ----------------------------------------------------
		mrpt::poses::CPosePDFSOG  best_poses;
		TMatchingPairList         out_best_pairings;

		timelog.enter("robustRigidTransformation");
		timer.Tic();

		mrpt::scanmatching::robustRigidTransformation(
			all_correspondences, // In pairings
			best_poses, // Out pose(s)
			normalizationStd,
			RANSAC_MINIMUM_INLIERS,           // ransac_minSetSize (to add the solution to the SOG)
			all_correspondences.size(),        // ransac_maxSetSize: Test with all data points
			ransac_mahalanobisDistanceThreshold,
			0,           // ransac_nSimulations (0:auto)
			&out_best_pairings, // Out
			true,        // ransac_fuseByCorrsMatch
			0.01f,       // ransac_fuseMaxDiffXY
			DEG2RAD(0.1f),  //  ransac_fuseMaxDiffPhi
			true,        // ransac_algorithmForLandmarks
			0.999999,       // probability_find_good_model
			MINIMUM_RANSAC_ITERS,        // ransac_min_nSimulations (a lower limit to the auto-detected value of ransac_nSimulations)
			true         // verbose
			);

		timelog.leave("robustRigidTransformation");

		const double tim = timer.Tac();
		cout << "RANSAC time: " << mrpt::system::formatTimeInterval(tim) << endl;

		cout << "# of SOG modes: " << best_poses.size() << endl;
		cout << "Best match has " <<out_best_pairings.size() << " features:\n";
		for (size_t i=0;i<out_best_pairings.size();i++)
			cout << out_best_pairings[i].this_idx << " <-> " << out_best_pairings[i].other_idx << endl;
		cout << endl;

		// Generate "association vector":
		vector<int> obs2map_pairings(nObs,-1);
		for (size_t i=0;i<out_best_pairings.size();i++)
			obs2map_pairings[out_best_pairings[i].other_idx] = out_best_pairings[i].this_idx==((unsigned int)-1) ? -1 : out_best_pairings[i].this_idx;

		cout << "1\n";
		for (size_t i=0;i<nObs;i++)
			cout << obs2map_pairings[i] << " ";
		cout << endl;


		gl_result->clear();

		// Reconstruct the SE(2) transformation for these pairings:
		mrpt::poses::CPosePDFGaussian  solution_pose;
		mrpt::scanmatching::leastSquareErrorRigidTransformation(
			out_best_pairings,
			solution_pose.mean,
			&solution_pose.cov );
		// Normalized covariance: scale!
		solution_pose.cov *= square(normalizationStd);

		cout << "Solution pose: " << solution_pose.mean << endl;
		cout << "Ground truth pose: " << GT_pose << endl;


		{
			mrpt::opengl::COpenGLScenePtr &scene = win.get3DSceneAndLock();

			win.addTextMessage(
				5,5, "Blue: map landmarks | Red: Observations | White lines: Found correspondences",
				mrpt::utils::TColorf(0,0,0),"mono",12,mrpt::opengl::NICE, 0);

			//
			gl_obs_map->clear();
			for (size_t k=0;k<nObs;k++)
				gl_obs_map->insertPoint( observations[k].x,observations[k].y, 0.0 );

			gl_obs->setPose( solution_pose.mean );

#if SHOW_POINT_LABELS
			gl_obs_txts->clear();
			for (size_t i=0;i<nObs;i++)
			{
				mrpt::opengl::CTextPtr gl_txt = mrpt::opengl::CText::Create( mrpt::format("%u",static_cast<unsigned int>(i)) );
				const double x = observations[i].x;
				const double y = observations[i].y;
				gl_txt->setLocation(x+0.05,y+0.05,0.01);
				gl_obs_txts->insert(gl_txt);
			}
#endif


			//
			gl_lines->clear();
			double sqerr = 0;
			size_t nPairs = 0;
			for (size_t k=0;k<nObs;k++)
			{
				int map_idx = obs2map_pairings[k];
				if (map_idx<0) continue;
				nPairs++;

				double map_x,map_y;
				the_map.getPoint(map_idx,map_x,map_y);

				double obs_x,obs_y;
				solution_pose.mean.composePoint(
					observations[k].x,observations[k].y,
					obs_x, obs_y);

				const double z = 0;

				gl_lines->appendLine(
					map_x,map_y,0,
					obs_x,obs_y,z );

				sqerr+= mrpt::math::distanceSqrBetweenPoints<double>(map_x,map_y,obs_x,obs_y);
			}

			win.addTextMessage(
				5,20, "Ground truth pose    : " + GT_pose.asString(),
				mrpt::utils::TColorf(0,0,0),"mono",12,mrpt::opengl::NICE, 1);
			win.addTextMessage(
				5,35, "RANSAC estimated pose: " + solution_pose.mean.asString() + mrpt::format(" | RMSE=%f",(nPairs ? sqerr/nPairs : 0.0) ),
				mrpt::utils::TColorf(0,0,0),"mono",12,mrpt::opengl::NICE, 2);

			win.unlockAccess3DScene();
			win.repaint();

			cout << "nPairings: " << nPairs << " RMSE = " << (nPairs ? sqerr/nPairs : 0.0)<< endl;

			win.waitForKey();
		}


	} // end of for each set of observations


}

// ------------------------------------------------------
//						MAIN
// ------------------------------------------------------
int main()
{
	try
	{
		TestRANSAC();
		return 0;
	} catch (std::exception &e)
	{
		std::cout << "MRPT exception caught: " << e.what() << std::endl;
		return -1;
	}
	catch (...)
	{
		printf("Untyped exception!!");
		return -1;
	}
}