/*---------------------------------------------------------------------------*
 |                             HOMe-toolkit                                  |
 |       A toolkit for working with the HOME Environment dataset (HOMe)      |
 |                                                                           |
 |              Copyright (C) 2015 Jose Raul Ruiz Sarmiento                  |
 |                 University of Malaga <jotaraul@uma.es>                    |
 |             MAPIR Group: <http://http://mapir.isa.uma.es/>                |
 |                                                                           |
 |   This program is free software: you can redistribute it and/or modify    |
 |   it under the terms of the GNU General Public License as published by    |
 |   the Free Software Foundation, either version 3 of the License, or       |
 |   (at your option) any later version.                                     |
 |                                                                           |
 |   This program is distributed in the hope that it will be useful,         |
 |   but WITHOUT ANY WARRANTY; without even the implied warranty of          |
 |   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            |
 |   GNU General Public License for more details.                            |
 |   <http://www.gnu.org/licenses/>                                          |
 |                                                                           |
 *---------------------------------------------------------------------------*/

#include <mrpt/maps/CSimplePointsMap.h>
#include <mrpt/obs/CObservation2DRangeScan.h>
#include <mrpt/obs/CObservation3DRangeScan.h>
#include <mrpt/slam/CICP.h>
#include <mrpt/obs/CRawlog.h>
#include <mrpt/poses/CPose2D.h>
#include <mrpt/poses/CPosePDF.h>
#include <mrpt/poses/CPosePDFGaussian.h>
#include <mrpt/gui.h>
#include <mrpt/math/utils.h>
#include <mrpt/system/threads.h>
#include <mrpt/utils/CTicTac.h>

#include <mrpt/slam/CICP.h>

// ICP 3D MRPT

#include <mrpt/poses/CPose3DPDF.h>
#include <mrpt/system/threads.h>
#include <mrpt/gui/CDisplayWindow3D.h>
#include <mrpt/opengl/CGridPlaneXY.h>
#include <mrpt/opengl/CSphere.h>
#include <mrpt/opengl/CAngularObservationMesh.h>
#include <mrpt/opengl/CDisk.h>
#include <mrpt/opengl/stock_objects.h>

// ICP 3D PCL

#include <pcl/point_types.h>
#include <pcl/point_cloud.h>
#include <pcl/registration/gicp.h>
#include <pcl/registration/warp_point_rigid_3d.h>

#include <pcl/filters/voxel_grid.h>

#include <iostream>
#include <fstream>

#include <numeric> // std::accumulate

using namespace mrpt;
using namespace mrpt::utils;
using namespace mrpt::maps;
using namespace mrpt::slam;
using namespace mrpt::gui;
using namespace mrpt::opengl;
using namespace mrpt::poses;
using namespace mrpt::obs;
using namespace mrpt::system;
using namespace std;
using namespace mrpt::math;

using namespace pcl;

//
// Configuration
//

bool initialGuessICP2D  = true;
bool refineWithICP3D    = false;
bool accumulatePast     = false;
string ICP3D_method;

CPose3D lastGoodICP3Dpose;
bool oneGoodICP3DPose = false;

bool skip_window=false;
int  ICP_method = (int) icpClassic;
CPose2D		initialPose(0.8f,0.0f,(float)DEG2RAD(0.0f));
gui::CDisplayWindowPlots	win("ICP results");

ofstream trajectoryFile("trajectory.txt",ios::trunc);

CDisplayWindow3D window("ICP-3D demo: scene",500,500);
CDisplayWindow3D window2("ICP-3D demo: UNALIGNED scans",500,500);
CDisplayWindow3D window3("ICP-3D demo: ICP-ALIGNED scans",500,500);

//Increase this values to get more precision. It will also increase run time.
const size_t HOW_MANY_YAWS=360;
const size_t HOW_MANY_PITCHS=360;

struct TRobotPose
{
    TTimeStamp  time;
    CPose2D     pose;
};

vector< TRobotPose > v_robotPoses;
vector< CObservation3DRangeScanPtr > v_3DRangeScans;
vector< CObservation3DRangeScanPtr > v_pending3DRangeScans;
vector< double > v_goodness;


//-----------------------------------------------------------
//                      trajectoryICP2D
//-----------------------------------------------------------

void trajectoryICP2D( string &simpleMapFile, CRawlog &rawlog, CObservation2DRangeScanPtr obs2D )
{
    CSimplePointsMap		m1,m2;
    float					runningTime;
    CICP::TReturnInfo		info;
    CICP					ICP;

    m1.load2D_from_text_file(simpleMapFile);
    //cout << "M1 size: " << m1.size();

    m2.insertObservation( obs2D.pointer() );
    //cout << "M2 size: " << m2.size();

    // -----------------------------------------------------
//	ICP.options.ICP_algorithm = icpLevenbergMarquardt;
//	ICP.options.ICP_algorithm = icpClassic;
    ICP.options.ICP_algorithm = (TICPAlgorithm)ICP_method;

    ICP.options.maxIterations			= 100;
    ICP.options.thresholdAng			= DEG2RAD(10.0f);
    ICP.options.thresholdDist			= 0.75f;
    ICP.options.ALFA					= 0.5f;
    ICP.options.smallestThresholdDist	= 0.05f;
    ICP.options.doRANSAC = false;

//    ICP.options.dumpToConsole();
    // -----------------------------------------------------

    CPosePDFPtr pdf = ICP.Align(
        &m1,
        &m2,
        initialPose,
        &runningTime,
        (void*)&info);

    printf("ICP run in %.02fms, %d iterations (%.02fms/iter), %.01f%% goodness\n -> ",
            runningTime*1000,
            info.nIterations,
            runningTime*1000.0f/info.nIterations,
            info.goodness*100 );

    cout << "Mean of estimation: " << pdf->getMeanVal() << endl<< endl;
    initialPose = pdf->getMeanVal();

    CPosePDFGaussian  gPdf;
    gPdf.copyFrom(*pdf);

//    cout << "Covariance of estimation: " << endl << gPdf.cov << endl;

//    cout << " std(x): " << sqrt( gPdf.cov(0,0) ) << endl;
//    cout << " std(y): " << sqrt( gPdf.cov(1,1) ) << endl;
//    cout << " std(phi): " << RAD2DEG(sqrt( gPdf.cov(2,2) )) << " (deg)" << endl;

//    cout << "-> Saving reference map as scan1.txt" << endl;
//    m1.save2D_to_text_file("scan1.txt");

//    cout << "-> Saving map to align as scan2.txt" << endl;
//    m2.save2D_to_text_file("scan2.txt");

//    cout << "-> Saving transformed map to align as scan2_trans.txt" << endl;
    CSimplePointsMap m2_trans = m2;
    m2_trans.changeCoordinatesReference( gPdf.mean );
//    m2_trans.save2D_to_text_file("scan2_trans.txt");


    trajectoryFile << initialPose << endl;


    if (!skip_window)
    {
        CMatrixFloat COV22 =  CMatrixFloat( CMatrixDouble( gPdf.cov ));
        COV22.setSize(2,2);
        Eigen::Vector2f MEAN2D(2);
        MEAN2D(0) = gPdf.mean.x();
        MEAN2D(1) = gPdf.mean.y();

        // Reference map:
        vector<float>   map1_xs, map1_ys, map1_zs;
        m1.getAllPoints(map1_xs,map1_ys,map1_zs);
        win.plot( map1_xs, map1_ys, "b.3", "map1");

        // Translated map:
        vector<float>   map2_xs, map2_ys, map2_zs;
        m2_trans.getAllPoints(map2_xs,map2_ys,map2_zs);
        win.plot( map2_xs, map2_ys, "r.3", "map2");

        // Uncertainty
        win.plotEllipse(MEAN2D(0),MEAN2D(1),COV22,3.0,"b2", "cov");

        win.axis(-1,10,-6,6);
        win.axis_equal();

        /*cout << "Close the window to exit" << endl;
        win.waitForKey();*/
        mrpt::system::sleep(0);
    }

}


//-----------------------------------------------------------
//                 processPending3DRangeScans
//-----------------------------------------------------------

void processPending3DRangeScans()
{
    size_t N_robotPoses = v_robotPoses.size();

    // More than one hokuyo observation?
    if ( N_robotPoses > 1 )
    {
        TRobotPose &rp1 = v_robotPoses[N_robotPoses-2];
        TRobotPose &rp2 = v_robotPoses[N_robotPoses-1];

        double tdPositions = timeDifference(rp1.time,rp2.time);

        // Okey, set pose of all the pending scans
        for ( size_t obs_index = 0;
              obs_index < v_pending3DRangeScans.size();
              obs_index++ )
        {
            CPose3D pose;
            v_pending3DRangeScans[obs_index]->getSensorPose(pose);

            TTimeStamp &obsTime = v_pending3DRangeScans[obs_index]->timestamp;

            double tdPos1Obs = timeDifference(rp1.time,obsTime);

            // Get an approximation of where was gathered the 3D range scan
            double interpolationFactor = ( tdPos1Obs / tdPositions );

            CPose2D posPoseDif = rp2.pose - rp1.pose;

            CVectorDouble v_coords;
            posPoseDif.getAsVector(v_coords);
            CPose2D intermediatePose(v_coords[0]*interpolationFactor,
                                           v_coords[1]*interpolationFactor,
                                           v_coords[2]*interpolationFactor);

            CVectorDouble v_coordsIntermediatePose;
            intermediatePose.getAsVector( v_coordsIntermediatePose );

            CVectorDouble v_rp1Pose;
            rp1.pose.getAsVector( v_rp1Pose );

            CPose2D poseToSum = rp1.pose + intermediatePose;

            CPose3D finalPose = poseToSum + pose;

            CVectorDouble coords;
            finalPose.getAsVector(coords);
            //finalPose.setFromValues(coords[0],coords[1],coords[2],coords[3],coords[4]-DEG2RAD(90),coords[5]-DEG2RAD(90));
            finalPose.setFromValues(coords[0],coords[1],coords[2],coords[3],coords[4],coords[5]);

            /*cout << "Obs initial pose:       " << pose << endl;
            cout << "interpolationFactor:    " << interpolationFactor << endl;
            cout << "rp1.pose:               " << rp1.pose << endl;
            cout << "rp2.pose:               " << rp2.pose << endl;
            cout << "intermediatePose:       " << intermediatePose << endl;
            cout << "poseToSum:              " << poseToSum << endl;
            cout << "finalPose:              " << finalPose << endl;*/

            v_pending3DRangeScans[obs_index]->setSensorPose( finalPose );

            v_3DRangeScans.push_back( v_pending3DRangeScans[obs_index] );
        }
    }

    v_pending3DRangeScans.clear();

}


//-----------------------------------------------------------
//                      refineLocationPCL
//-----------------------------------------------------------

void refineLocationPCL( vector<CObservation3DRangeScanPtr> &v_obs, vector<CObservation3DRangeScanPtr> &v_obs2 )
{
    if (!initialGuessICP2D)
    {
        for (size_t i=0; i < v_obs2.size(); i++)
        {
            CPose3D pose;
            v_obs[v_obs.size()-v_obs2.size()+i]->getSensorPose(pose);
            v_obs2[i]->setSensorPose(pose);
        }
    }

    // Show the scanned points:
    CSimplePointsMap	M1,M2;

    for ( size_t i = 0; i < v_obs.size(); i++ )
        M1.insertObservationPtr( v_obs[i] );

    for ( size_t i = 0; i < v_obs2.size(); i++ )
        M2.insertObservationPtr( v_obs2[i] );

    cout << "Getting points... points 1: ";

    CVectorDouble xs, ys, zs, xs2, ys2, zs2;
    M1.getAllPoints(xs,ys,zs);
    M2.getAllPoints(xs2,ys2,zs2);

    cout << xs.size() << " points 2: " << xs2.size() << " ... done" << endl;

    //cout << "Inserting points...";

    PointCloud<PointXYZ>::Ptr	cloud_old   (new PointCloud<PointXYZ>());
    PointCloud<PointXYZ>::Ptr	cloud_new   (new PointCloud<PointXYZ>());
    PointCloud<PointXYZ>::Ptr   cloud_trans (new PointCloud<PointXYZ>());

    cloud_old->clear();
    cloud_new->clear();
    cloud_trans->clear();

    for ( size_t i = 0; i < xs.size(); i+= ( ( accumulatePast ) ? 1 : 4) )
    {
        PointXYZ point(xs[i],ys[i],zs[i]);

        //if ( point.x && point.y && point.z )
            cloud_old->push_back(point);
    }

    for ( size_t i = 0; i < xs2.size(); i+= ( ( accumulatePast ) ? 1 : 4) )
    {
        PointXYZ point(xs2[i],ys2[i],zs2[i]);

        //if ( point.x && point.y && point.z )
            cloud_new->push_back(point);
    }

    if ( true )
    {
        // Create the filtering object
        pcl::VoxelGrid<pcl::PointXYZ> sor;
        sor.setInputCloud (cloud_old);
        sor.setLeafSize (0.05,
                         0.05,
                         0.05);
        sor.filter (*cloud_old);
    }

    /*for ( size_t i = 0; i < xs.size(); i+=4 )
            cloud_old->push_back(PointXYZ(xs[i],ys[i],0));

        for ( size_t i = 0; i < xs2.size(); i+=4 )
            cloud_new->push_back(PointXYZ(xs2[i],ys2[i],0));*/

    //cout << "done" << endl;

    // Esa para GICP, si quieres el normal solo le tienes que quitar lo de Generalized
    GeneralizedIterativeClosestPoint<PointXYZ, PointXYZ> gicp;

    //cout << "Setting input clouds...";

    gicp.setInputSource(cloud_new);
    gicp.setInputTarget(cloud_old);

    //cout << "done"  << endl;
    //cout << "Setting parameters...";

    //ICP options
    gicp.setMaxCorrespondenceDistance (0.2); // 0.5
    // Set the maximum number of iterations (criterion 1)
    gicp.setMaximumIterations (10); // 10
    // Set the transformation tras epsilon (criterion 2)
    gicp.setTransformationEpsilon (1e-5); // 1e-5
    // Set the transformation rot epsilon (criterion 3)
    gicp.setRotationEpsilon (1e-5); // 1e-5

    //cout << "done"  << endl;

    cout << "Doing ICP...";

    gicp.align(*cloud_trans);

    double score;
    score = gicp.getFitnessScore(); // Returns the squared average error between the aligned input and target

    v_goodness.push_back(score);

    cout << "Done! Average error: " << sqrt(score) << " meters" << endl;

    // Obtain the transformation that aligned cloud_source to cloud_source_registered
    Eigen::Matrix4f transformation = gicp.getFinalTransformation();

    CPose3D estimated_pose;

    //cout << "Transformation" << endl;
    //cout << transformation << endl;

    CMatrixDouble33 rot_matrix;
        for (unsigned int i=0; i<3; i++)
            for (unsigned int j=0; j<3; j++)
                rot_matrix(i,j) = transformation(i,j);

    estimated_pose.setRotationMatrix(rot_matrix);
    estimated_pose.x(transformation(0,3));
    estimated_pose.y(transformation(1,3));
    estimated_pose.z(transformation(2,3));

    //cout << "Pose set done" << endl;

    for ( size_t i = 0; i < v_obs2.size(); i++ )
    {
        CObservation3DRangeScanPtr obs = v_obs2[i];

        CPose3D pose;
        obs->getSensorPose( pose );

        CPose3D finalPose = estimated_pose + pose;
        obs->setSensorPose(finalPose);
    }

    //cout << "Location set done" << endl;
}

//-----------------------------------------------------------
//                      refineLocationPCL
//-----------------------------------------------------------

void refineLocationPCLBis( vector<CObservation3DRangeScanPtr> &v_obs, vector<CObservation3DRangeScanPtr> &v_obs2 )
{
    CDisplayWindow3D window2("ICP-3D demo: UNALIGNED scans",500,500);
    CDisplayWindow3D window3("ICP-3D demo: ICP-ALIGNED scans",500,500);

    COpenGLScenePtr scene2=COpenGLScene::Create();
    COpenGLScenePtr scene3=COpenGLScene::Create();

    window2.get3DSceneAndLock()=scene2;
    window3.get3DSceneAndLock()=scene3;

    opengl::CGridPlaneXYPtr plane1=CGridPlaneXY::Create(-20,20,-20,20,0,1);
    plane1->setColor(0.3,0.3,0.3);

    scene2->insert(plane1);
    scene3->insert(plane1);

    // Show in Windows:

    window.setCameraElevationDeg(15);
    window.setCameraAzimuthDeg(90);
    window.setCameraZoom(15);

    window2.setCameraElevationDeg(15);
    window2.setCameraAzimuthDeg(90);
    window2.setCameraZoom(15);

    window3.setCameraElevationDeg(15);
    window3.setCameraAzimuthDeg(90);
    window3.setCameraZoom(15);

    // Show the scanned points:
    CSimplePointsMap	M1,M2,M3;

    for ( size_t i = 0; i < v_obs.size(); i++ )
        M1.insertObservationPtr( v_obs[i] );

    for ( size_t i = 0; i < v_obs2.size(); i++ )
        M2.insertObservationPtr( v_obs2[i] );

    cout << "Getting points... points 1: ";

    CVectorDouble xs, ys, zs, xs2, ys2, zs2;
    M1.getAllPoints(xs,ys,zs);
    M2.getAllPoints(xs2,ys2,zs2);

    cout << xs.size() << " points 2: " << xs2.size() << endl;


    CSetOfObjectsPtr  PTNS1 = CSetOfObjects::Create();
    CSetOfObjectsPtr  PTNS2 = CSetOfObjects::Create();
    CSetOfObjectsPtr  PTNS2_ALIGN = CSetOfObjects::Create();

    CPointsMap::COLOR_3DSCENE_R = 1;
    CPointsMap::COLOR_3DSCENE_G = 0;
    CPointsMap::COLOR_3DSCENE_B = 0;
    M1.getAs3DObject(PTNS1);

    CPointsMap::COLOR_3DSCENE_R = 0;
    CPointsMap::COLOR_3DSCENE_G = 0;
    CPointsMap::COLOR_3DSCENE_B = 1;
    M2.getAs3DObject(PTNS2);

    scene2->insert( PTNS1 );
    scene2->insert( PTNS2 );


    scene3->insert( PTNS1 );
    scene3->insert( PTNS2_ALIGN );

    window2.unlockAccess3DScene();
    window2.forceRepaint();
    window3.unlockAccess3DScene();
    window3.forceRepaint();

    //cout << "Inserting points...";

    PointCloud<PointXYZ>::Ptr	cloud_old   (new PointCloud<PointXYZ>());
    PointCloud<PointXYZ>::Ptr	cloud_new   (new PointCloud<PointXYZ>());
    PointCloud<PointXYZ>::Ptr   cloud_trans (new PointCloud<PointXYZ>());

    cloud_old->clear();
    cloud_new->clear();
    cloud_trans->clear();

    for ( size_t i = 0; i < xs.size(); i+=10 )
        cloud_old->push_back(PointXYZ(xs[i],ys[i],zs[i]));

    for ( size_t i = 0; i < xs2.size(); i+=10 )
        cloud_new->push_back(PointXYZ(xs2[i],ys2[i],zs2[i]));

    //cout << "done" << endl;

    // Esa para GICP, si quieres el normal solo le tienes que quitar lo de Generalized
    GeneralizedIterativeClosestPoint<PointXYZ, PointXYZ> gicp;

    //cout << "Setting input clouds...";

    gicp.setInputTarget(cloud_new);
    gicp.setInputTarget(cloud_old);

    //cout << "done"  << endl;
    //cout << "Setting parameters...";

    //ICP options
    gicp.setMaxCorrespondenceDistance (0.5);
    // Set the maximum number of iterations (criterion 1)
    gicp.setMaximumIterations (100);
    // Set the transformation tras epsilon (criterion 2)
    gicp.setTransformationEpsilon (1e-6);
    // Set the transformation rot epsilon (criterion 3)
    gicp.setRotationEpsilon (1e-6);

    //cout << "done"  << endl;

    cout << "Doing ICP...";

    gicp.align(*cloud_trans);

    double score;
    score = gicp.getFitnessScore(); // Returns the squared average error between the aligned input and target

    v_goodness.push_back(score);

    cout << "Done! Average error: " << sqrt(score) << " meters" << endl;

    // Obtain the transformation that aligned cloud_source to cloud_source_registered
    Eigen::Matrix4f transformation = gicp.getFinalTransformation();

    CPose3D estimated_pose;

    //cout << "Transformation" << endl;
    //cout << transformation << endl;

    CMatrixDouble33 rot_matrix;
        for (unsigned int i=0; i<3; i++)
            for (unsigned int j=0; j<3; j++)
                rot_matrix(i,j) = transformation(i,j);

    estimated_pose.setRotationMatrix(rot_matrix);
    estimated_pose.x(transformation(0,3));
    estimated_pose.y(transformation(1,3));
    estimated_pose.z(transformation(2,3));

    CVectorDouble v_pose;
    estimated_pose.getAsVector(v_pose);

    cout << "Pose: " << v_pose << endl;

    //cout << "Pose set done" << endl;

    for ( size_t i = 0; i < v_obs2.size(); i++ )
    {
        CObservation3DRangeScanPtr obs = v_obs2[i];

        CPose3D pose;
        obs->getSensorPose( pose );

        CPose3D finalPose = estimated_pose + pose;
        obs->setSensorPose(finalPose);

        M3.insertObservationPtr( obs );
    }

    CPointsMap::COLOR_3DSCENE_R = 0;
    CPointsMap::COLOR_3DSCENE_G = 1;
    CPointsMap::COLOR_3DSCENE_B = 0;
    M3.getAs3DObject(PTNS2_ALIGN);

    //window2.waitForKey();

    //cout << "Location set done" << endl;
}


//-----------------------------------------------------------
//                      refineLocation
//-----------------------------------------------------------

void refineLocation( vector<CObservation3DRangeScanPtr> &v_obs, vector<CObservation3DRangeScanPtr> &v_obs2)
{

    if (!initialGuessICP2D)
    {
        for (size_t i=0; i < v_obs2.size(); i++)
        {
            CPose3D pose;
            v_obs[v_obs.size()-v_obs2.size()+i]->getSensorPose(pose);
            v_obs2[i]->setSensorPose(pose);
        }
    }

    // Show the scanned points:
    CSimplePointsMap	M1,M2;

    cout << "Getting points, points 1: ";

    for ( size_t i = 0; i < v_obs.size(); i++ )
        M1.insertObservationPtr( v_obs[i] );

    for ( size_t i = 0; i < v_obs2.size(); i++ )
        M2.insertObservationPtr( v_obs2[i] );

    cout << M1.size() << ", points 2: " << M2.size() << " done" << endl;

    COpenGLScenePtr scene1=COpenGLScene::Create();
    COpenGLScenePtr scene2=COpenGLScene::Create();
    COpenGLScenePtr scene3=COpenGLScene::Create();

    opengl::CGridPlaneXYPtr plane1=CGridPlaneXY::Create(-20,20,-20,20,0,1);
    plane1->setColor(0.3,0.3,0.3);
    scene1->insert(plane1);
    scene2->insert(plane1);
    scene3->insert(plane1);

    CSetOfObjectsPtr  PTNS1 = CSetOfObjects::Create();
    CSetOfObjectsPtr  PTNS2 = CSetOfObjects::Create();

    CPointsMap::COLOR_3DSCENE_R = 1;
    CPointsMap::COLOR_3DSCENE_G = 0;
    CPointsMap::COLOR_3DSCENE_B = 0;
    M1.getAs3DObject(PTNS1);

    CPointsMap::COLOR_3DSCENE_R = 0;
    CPointsMap::COLOR_3DSCENE_G = 0;
    CPointsMap::COLOR_3DSCENE_B = 1;
    M2.getAs3DObject(PTNS2);

    scene2->insert( PTNS1 );
    scene2->insert( PTNS2 );

    // --------------------------------------
    // Do the ICP-3D
    // --------------------------------------
    float run_time;
    CICP	icp;
    CICP::TReturnInfo	icp_info;

    icp.options.thresholdDist = 0.40;
    icp.options.thresholdAng = 0;

    CPose3DPDFPtr pdf= icp.Align3D(
                &M2,          // Map to align
                &M1,          // Reference map
                CPose3D() ,   // Initial gross estimate
                &run_time,
                &icp_info);

    CPose3D  mean = pdf->getMeanVal();

    cout << "ICP run took " << run_time << " secs." << endl;
    cout << "Goodness: " << 100*icp_info.goodness << "% , # of iterations= " << icp_info.nIterations << endl;
    cout << "ICP output: mean= " << mean << endl;

    // Aligned maps:
    CSetOfObjectsPtr  PTNS2_ALIGN = CSetOfObjects::Create();

    M2.changeCoordinatesReference( CPose3D()-mean );
    M2.getAs3DObject(PTNS2_ALIGN);

    scene3->insert( PTNS1 );
    scene3->insert( PTNS2_ALIGN );

    // Show in Windows:

    window2.get3DSceneAndLock()=scene2;
    window2.unlockAccess3DScene();

    window3.get3DSceneAndLock()=scene3;
    window3.unlockAccess3DScene();


    mrpt::system::sleep(20);
    window2.forceRepaint();

    window2.setCameraElevationDeg(15);
    window2.setCameraAzimuthDeg(90);
    window2.setCameraZoom(15);

    window3.setCameraElevationDeg(15);
    window3.setCameraAzimuthDeg(90);
    window3.setCameraZoom(15);

    //cout << "Press any key to exit..." << endl;
    //window.waitForKey();

    double goodness = 100*icp_info.goodness;
    v_goodness.push_back( goodness );

    if ( goodness < 96 )
    {
        //return;
        if ( oneGoodICP3DPose )
            mean = lastGoodICP3Dpose;
        else
            mean = CPose3D(0,0,0,0,0,0);

    }

    for ( size_t i = 0; i < v_obs2.size(); i++ )
    {
        CObservation3DRangeScanPtr obs = v_obs2[i];

        CPose3D pose;
        obs->getSensorPose( pose );

        CPose3D finalPose = mean + pose;
        obs->setSensorPose(finalPose);
    }

    oneGoodICP3DPose = true;
    lastGoodICP3Dpose = mean;

}

//-----------------------------------------------------------
//                          main
//-----------------------------------------------------------

int main(int argc, char **argv)
{
    try
    {
        string simpleMapFile;
        CRawlog i_rawlog, o_rawlog;
        CTicTac clock;
        double time_icp2D = 0, time_icp3D = 0;

        string rawlogFilename;
        string o_rawlogFile;

        //
        // Load parameters
        //

        if ( argc >= 3 )
        {
            rawlogFilename = argv[1];
            simpleMapFile = argv[2];

            o_rawlogFile = rawlogFilename.substr(0,rawlogFilename.size()-7);

            for ( size_t arg = 3; arg < argc; arg++ )
            {
                if ( !strcmp(argv[arg],"-disable_ICP2D") )
                {
                    initialGuessICP2D = false;
                    cout << "[INFO] Disabled ICP2D to guess the robot localization."  << endl;
                }
                else if ( !strcmp(argv[arg], "-enable_ICP3D") )
                {
                    refineWithICP3D = true;
                    ICP3D_method    = "ICP";  // MRPT
                    cout << "[INFO] Enabled ICP3D."  << endl;
                }
                else if ( !strcmp(argv[arg], "-enable_GICP3D") )
                {
                    refineWithICP3D = true;
                    ICP3D_method     = "GICP"; // PCL
                    cout << "[INFO] Enabled GICP3D."  << endl;
                }
                else if ( !strcmp(argv[arg], "-enable_memory") )
                {
                    accumulatePast   = true;
                    cout << "[INFO] Enabled (G)ICP3D memory."  << endl;
                }
                else
                {
                    cout << "[Error] " << argv[arg] << "unknown paramter" << endl;
                    return -1;
                }

            }
        }
        else
        {
            cout << "Usage information. Two expected arguments: " << endl <<
                    " \t (1) Rawlog file." << endl <<
                    " \t (2) Points map file." << endl;
            cout << "Then, optional parameters:" << endl <<
                    " \t -disable_ICP2D : Disable ICP2D as an initial guess for robot localization." << endl <<
                    " \t -enable_ICP3D  : Enable ICP3D to refine the RGBD-sensors location." << endl <<
                    " \t -enable_GICP3D : Enable GICP3D to refine the RGBD-sensors location." << endl <<
                    " \t -enable_memory: Accumulate 3D point clouds already registered." << endl;

            return 0;
        }

        if ( refineWithICP3D && ICP3D_method == "GICP" )
            o_rawlogFile += "_located-GICP";
        else if ( refineWithICP3D && ICP3D_method == "ICP" )
            o_rawlogFile += "_located-ICP";
        else
            o_rawlogFile += "_located";

        if ( refineWithICP3D && accumulatePast )
            o_rawlogFile += "-memory";

        o_rawlogFile += ".rawlog";


        if (!i_rawlog.loadFromRawLogFile(rawlogFilename))
            throw std::runtime_error("Couldn't open rawlog dataset file for input...");

        cout << "Working with " << rawlogFilename << endl;

        // Create the reference objects:

        if ( refineWithICP3D )
        {
            if ( ICP3D_method == "GICP")
            {
                window.setPos(-500,-500);
                window2.setPos(-500,-500);
                window3.setPos(-500,-500);
            }
            else
            {
                window.setPos(10,10);
                window2.setPos(530,10);
                window3.setPos(10,520);
            }
        }

        if ( initialGuessICP2D )
        {
            clock.Tic();

            for ( size_t obsIndex = 0; obsIndex < i_rawlog.size(); obsIndex++ )
            {
                CObservationPtr obs = i_rawlog.getAsObservation(obsIndex);

                if ( IS_CLASS(obs, CObservation2DRangeScan) )
                {
                    CObservation2DRangeScanPtr obs2D = CObservation2DRangeScanPtr(obs);
                    obs2D->load();

                    trajectoryICP2D(simpleMapFile,i_rawlog,obs2D);

                    TRobotPose robotPose;
                    robotPose.pose = initialPose;
                    robotPose.time = obs2D->timestamp;

                    v_robotPoses.push_back( robotPose );

                    // Process pending 3D range scans, if any
                    if ( !v_pending3DRangeScans.empty() )
                        processPending3DRangeScans();
                }
                else //if ( obs->sensorLabel == RGBD_sensor )
                {
                    CObservation3DRangeScanPtr obs3D = CObservation3DRangeScanPtr(obs);
                    obs3D->load();

                    v_pending3DRangeScans.push_back( obs3D );
                }
                /*else
                    continue;*/
            }

            time_icp2D = clock.Tac();

            trajectoryFile.close();
        }
        else
        {
            for ( size_t obsIndex = 0; obsIndex < i_rawlog.size(); obsIndex++ )
            {
                CObservationPtr obs = i_rawlog.getAsObservation(obsIndex);

                if ( IS_CLASS(obs, CObservation2DRangeScan) )
                {

                }
                else
                {
                    CObservation3DRangeScanPtr obs3D = CObservation3DRangeScanPtr(obs);
                    obs3D->load();

                    v_3DRangeScans.push_back( obs3D );
                }
            }
        }

        //win.waitForKey();

        //
        // Refine using ICP3D
        //

        if ( refineWithICP3D )
        {
            clock.Tic();

            size_t N_scans = v_3DRangeScans.size();

            /*vector< vector <CObservation3DRangeScanPtr> > v_allObs(4);  // Past set of obs

            for ( size_t obsIndex = 0; obsIndex < N_scans; obsIndex++ )
            {
                CObservation3DRangeScanPtr obs = v_3DRangeScans[obsIndex];

                if ( obs->sensorLabel == "RGBD_1" )
                {
                    v_allObs[0].push_back(obs);
                }
                else if ( obs->sensorLabel == "RGBD_2" )
                {
                    v_allObs[1].push_back(obs);
                }
                else if ( obs->sensorLabel == "RGBD_3" )
                {
                    v_allObs[2].push_back(obs);
                }
                else if ( obs->sensorLabel == "RGBD_4" )
                {
                    v_allObs[3].push_back(obs);
                }
            }

            refineLocationPCLBis( v_allObs[0], v_allObs[1] );
            refineLocationPCLBis( v_allObs[0], v_allObs[2] );
            refineLocationPCLBis( v_allObs[0], v_allObs[3] );*/

            cout << "[INFO] Refining sensor poses using ICP3D..." << endl;

            vector<CObservation3DRangeScanPtr> v_obs;  // Past set of obs
            vector<CObservation3DRangeScanPtr> v_obs2(4); // Current set of obs
            vector<bool> v_obs_loaded(4,false);

            bool first = true;
            size_t set_index = 0;

            for ( size_t obsIndex = 0; obsIndex < N_scans; obsIndex++ )
            {
                CTicTac clock;
                clock.Tic();

                CObservation3DRangeScanPtr obs = v_3DRangeScans[obsIndex];
                //cout << obs->sensorLabel << endl;

                if ( obs->sensorLabel == "RGBD_1" )
                {
                    v_obs2[0] = obs;
                    v_obs_loaded[0] = true;
                }
                else if ( obs->sensorLabel == "RGBD_2" )
                {
                    v_obs2[1] = obs;
                    v_obs_loaded[1] = true;
                }
                else if ( obs->sensorLabel == "RGBD_3" )
                {
                    v_obs2[2] = obs;
                    v_obs_loaded[2] = true;
                }
                else if ( obs->sensorLabel == "RGBD_4" )
                {
                    v_obs2[3] = obs;
                    v_obs_loaded[3] = true;
                }

                double sum;

                sum = v_obs_loaded[0] + v_obs_loaded[1] + v_obs_loaded[2] + v_obs_loaded[3];

                if ( sum == 4 )
                {
                    v_obs_loaded.clear();
                    v_obs_loaded.resize(4,false);

                    if ( first )
                    {
                        v_obs = v_obs2;
                        first = false;
                        continue;
                    }

                    cout << "Working set of obs index... " << set_index++ << " of approx. " << (double)N_scans/4 << endl;

                    vector<CObservation3DRangeScanPtr>  v1(1,v_obs2[0]);
                    vector<CObservation3DRangeScanPtr>  v2(1,v_obs2[1]);
                    vector<CObservation3DRangeScanPtr>  v3(1,v_obs2[2]);
                    vector<CObservation3DRangeScanPtr>  v4(1,v_obs2[3]);

                    if ( ICP3D_method == "GICP" )
                    {
                        if ( accumulatePast )
                        {
                            refineLocationPCL( v_obs, v1  );
                            refineLocationPCL( v_obs, v2  );
                            refineLocationPCL( v_obs, v3  );
                            refineLocationPCL( v_obs, v4  );
                        }
                        else
                            refineLocationPCL( v_obs, v_obs2 );
                    }
                    else
                    {
                        if ( accumulatePast )
                        {
                            refineLocation( v_obs, v1  );
                            refineLocation( v_obs, v2  );
                            refineLocation( v_obs, v3  );
                            refineLocation( v_obs, v4  );
                        }
                        else
                            refineLocation( v_obs, v_obs2 );
                    }

                    if ( accumulatePast )
                        v_obs.insert(v_obs.end(), v_obs2.begin(), v_obs2.end() );
                    else
                        v_obs = v_obs2;

                    cout << "Time ellapsed: " << clock.Tac() << "s" << endl;
                }                
            }

            cout << "Mean goodness: "
                 << accumulate(v_goodness.begin(), v_goodness.end(), 0.0 ) / v_goodness.size()
                 << endl;

            cout << " completed" << endl;

            time_icp3D = clock.Tac();

        }

        cout << "[INFO] time spent by the icp2D process: " << time_icp2D << " sec." << endl;
        cout << "[INFO] time spent by the icp3D process: " << time_icp3D << " sec." << endl;

        cout << "[INFO] Saving obs to rawlog file " << o_rawlogFile << " ...";

        for ( size_t obs_index = 0; obs_index < v_3DRangeScans.size(); obs_index++ )
        {
            CSensoryFrame SF;
            SF.insert( v_3DRangeScans[obs_index] );
            //o_rawlog.addObservations( SF );
            o_rawlog.addObservationMemoryReference( v_3DRangeScans[obs_index] );
        }

        o_rawlog.saveToRawLogFile( o_rawlogFile );

        cout << " completed." << endl;

        win.hold_on();

        CVectorDouble coord_x, coord_y;
        for ( size_t pos_index = 0; pos_index < v_robotPoses.size(); pos_index++ )
        {
            CVectorDouble v_coords;
            v_robotPoses[pos_index].pose.getAsVector(v_coords);
            coord_x.push_back(v_coords[0]);
            coord_y.push_back(v_coords[1]);
        }

        win.plot(coord_x,coord_y,"k.5");

        CVectorDouble coord_x2, coord_y2;

        for ( size_t pos_index = 0; pos_index < v_3DRangeScans.size(); pos_index++ )
        {
            CPose3D pose;
            CVectorDouble v_coords;
            v_3DRangeScans[pos_index]->getSensorPose(pose);
            pose.getAsVector(v_coords);
            coord_x2.push_back(v_coords[0]);
            coord_y2.push_back(v_coords[1]);
        }

        win.plot(coord_x2,coord_y2,"g.5");

        win.waitForKey();

        return 0;

    } catch (exception &e)
    {
        cout << "MRPT exception caught: " << e.what() << endl;
        return -1;
    }
    catch (...)
    {
        printf("Another exception!!");
        return -1;
    }
}
