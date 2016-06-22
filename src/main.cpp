/*
 * main.cpp
 *
 *  Created on: 21 cze 2016
 *      Author: jachu
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <random>

#include "3rdParty/Eigen/StdVector"

#include "3rdParty/g2o/g2o/types/slam3d/se3quat.h"
#include "3rdParty/g2o/g2o/types/sba/types_six_dof_expmap.h"
#include "3rdParty/g2o/g2o/types/slam3d/isometry3d_mappings.h"
#include "3rdParty/g2o/g2o/types/slam3d/isometry3d_gradients.h"
#include "3rdParty/g2o/g2o/core/block_solver.h"
#include "3rdParty/g2o/g2o/core/optimization_algorithm_levenberg.h"
#include "3rdParty/g2o/g2o/solvers/eigen/linear_solver_eigen.h"


using namespace std;

int main(){
	try{
		ifstream trajFile("../res/groundtruth.txt");

		if(!trajFile.is_open()){
			throw "Error - groundtruth file not open";

		}

		vector<g2o::Vector7d> gtPoses;
		while(trajFile.good()){
			g2o::Vector7d curPose;
			int id;
			trajFile >> id;
			for(int v = 0; v < 7; ++v){
				double val;
				trajFile >> val;
				if(trajFile.good()){
					curPose[v] = val;
				}
			}
			if(trajFile.good()){
//				cout << "curPose = " << curPose << endl;
				gtPoses.push_back(curPose);
			}
		}

		default_random_engine gen;
		normal_distribution<double> distT(0.0, 0.005);
		normal_distribution<double> distR(0.0, 0.005);
		g2o::Vector7d initPose = gtPoses[0];
		vector<g2o::Vector7d> odomPoses{initPose};
		for(int po = 1; po < gtPoses.size(); ++po){
			g2o::Isometry3D prevPose = g2o::internal::fromVectorQT(gtPoses[po - 1]);
			g2o::Isometry3D nextPose = g2o::internal::fromVectorQT(gtPoses[po]);
			g2o::Isometry3D diff = prevPose.inverse() * nextPose;
			g2o::Vector7d diffQT = g2o::internal::toVectorQT(diff);
			for(int i = 0; i < 3; ++i){
				diffQT[i] += distT(gen);
			}
			for(int i = 3; i < 7; ++i){
				diffQT[i] += distR(gen);
			}
			// normalize quaternion
			Eigen::Quaterniond diffQuat(diffQT[6], diffQT[3], diffQT[4], diffQT[5]);
			diffQuat.normalize();
			diffQT[3] = diffQuat.x();
			diffQT[4] = diffQuat.y();
			diffQT[5] = diffQuat.z();
			diffQT[6] = diffQuat.w();

			g2o::Isometry3D diffNoise = g2o::internal::fromVectorQT(diffQT);
			g2o::Isometry3D prevOdomPose = g2o::internal::fromVectorQT(odomPoses[po - 1]);
			g2o::Isometry3D curDiffPose = prevOdomPose * diffNoise;

			odomPoses.push_back(g2o::internal::toVectorQT(curDiffPose));
		}

		ofstream diffTrajFile("../res/odomTraj.txt");
		for(int po = 0; po < odomPoses.size(); ++po){
			diffTrajFile << (po + 1);
			for(int i = 0; i < 7; ++i){
				diffTrajFile << " " << odomPoses[po][i];
			}
			diffTrajFile << endl;
		}

		std::vector<std::vector<Eigen::Vector3d>> planesPoints;

		ifstream planesFile("../res/planes.txt");

		if(!planesFile.is_open()){
			throw "Error - planes file not open";

		}

		while(planesFile.good()){
			vector<Eigen::Vector3d> curPlane;
			int id;
			planesFile >> id;
			for(int v = 0; v < 4; ++v){
				Eigen::Vector3d curPoint;
				for(int c = 0; c < 3; ++c){
					double val;
					planesFile >> val;
					if(planesFile.good()){
						curPoint[c] = val;
					}
				}
				if(planesFile.good()){
//					cout << "curPoint = " << curPoint << endl;
					curPlane.push_back(curPoint);
				}
			}
			if(planesFile.good()){
//				cout << "curPlane.size() = " << curPlane.size() << endl;
				planesPoints.push_back(curPlane);
			}
		}

		// x, y axis on the plane, z normal, position in the middle of the plane
		vector<g2o::Vector7d> planesPoses;
		for(int pl = 0; pl < planesPoints.size(); ++pl){

			Eigen::Vector3d center = planesPoints[pl][0];
			center += (planesPoints[pl][1] - planesPoints[pl][0]) / 2;
			center += (planesPoints[pl][2] - planesPoints[pl][1]) / 2;

			Eigen::Vector3d xAxis = planesPoints[pl][1] - planesPoints[pl][0];
			xAxis.normalize();
			Eigen::Vector3d yAxis = planesPoints[pl][2] - planesPoints[pl][1];
			yAxis.normalize();
			Eigen::Vector3d zAxis = xAxis.cross(yAxis);

			Eigen::Matrix3d curPlaneRot;
			curPlaneRot.block<3, 1>(0, 0) = xAxis;
			curPlaneRot.block<3, 1>(0, 1) = yAxis;
			curPlaneRot.block<3, 1>(0, 2) = zAxis;

			g2o::Isometry3D curPlanePosM;
			curPlanePosM = curPlaneRot;
			curPlanePosM.translation() = center;
			g2o::Vector7d curPlanePosQT = g2o::internal::toVectorQT(curPlanePosM);

			planesPoses.push_back(curPlanePosQT);

//			cout << "planesPoses[" << pl << "] rotation = " << g2o::internal::fromVectorQT(planesPoses[pl]).rotation() << endl;
//			cout << "planesPoses[" << pl << "] translation = " << g2o::internal::fromVectorQT(planesPoses[pl]).translation() << endl;
		}


	    g2o::SparseOptimizer optimizer;
	    g2o::BlockSolver_6_3::LinearSolverType * linearSolver;

	    linearSolver = new g2o::LinearSolverEigen<g2o::BlockSolver_6_3::PoseMatrixType>();

	    g2o::BlockSolver_6_3 * solver_ptr = new g2o::BlockSolver_6_3(linearSolver);

	    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
	    optimizer.setAlgorithm(solver);

	    for(int po = 0; po < odomPoses.size(); ++po){
	    	g2o::VertexSE3Expmap* curV = new g2o::VertexSE3Expmap();
	    	g2o::SE3Quat poseSE3Quat;
			poseSE3Quat.fromVector(odomPoses[po]);
	    	curV->setEstimate(poseSE3Quat);
	    	curV->setId(po);
	    	if(po == 0){
	    		curV->setFixed(true);
	    	}
	    	optimizer.addVertex(curV);
	    }
	    for(int pl = 0; pl < planesPoses.size(); ++pl){
	    	g2o::VertexSE3Expmap* curV = new g2o::VertexSE3Expmap();
	    	g2o::SE3Quat planePoseSE3Quat;
	    	planePoseSE3Quat.fromVector(planesPoses[pl]);
	    	curV->setEstimate(planePoseSE3Quat);
	    	curV->setId(odomPoses.size() + pl);
	    	optimizer.addVertex(curV);
	    }

	    for(int po = 0; po < odomPoses.size(); ++po){
	    	g2o::SE3Quat poseSE3Quat;
	    	poseSE3Quat.fromVector(odomPoses[po]);
	    	for(int pl = 0; pl < planesPoses.size(); ++pl){
	    		g2o::SE3Quat planePoseSE3Quat;
	    		planePoseSE3Quat.fromVector(planesPoses[pl]);

	    		Eigen::Matrix<double, 6, 6> covarPlane;
	    		covarPlane << 	1.0, 0.0, 0.0, 0.0, 0.0, 0.0,
								0.0, 1.0, 0.0, 0.0, 0.0, 0.0,
								0.0, 0.0, 0.001, 0.0, 0.0, 0.0,
								0.0, 0.0, 0.0, 0.001, 0.0, 0.0,
								0.0, 0.0, 0.0, 0.0, 0.001, 0.0,
								0.0, 0.0, 0.0, 0.0, 0.0, 1.0;
	    		Eigen::Matrix<double, 6, 6> jacob;
	    		g2o::SE3Quat measSE3Quat = poseSE3Quat.inverse() * planePoseSE3Quat;
	    		g2o::Isometry3D meas = g2o::internal::fromSE3Quat(measSE3Quat);
	    		g2o::Isometry3D E, X, P;
	    		X.setIdentity();
	    		P.setIdentity();
	    		g2o::internal::computeEdgeSE3PriorGradient(E, jacob, meas, X, P);

	    		Eigen::Matrix<double, 6, 6> covarPose = jacob * covarPlane * jacob.transpose();

	    		if(po == 0){
	    			cout << "pl = " << pl << endl;
	    			cout << "meas.rotation() = " << meas.rotation() << endl;
	    			cout << "meas.translation() = " << meas.translation() << endl;
					cout << "meas.inverse().rotation() = " << meas.inverse().rotation() << endl;
					cout << "meas.inverse().translation() = " << meas.inverse().translation() << endl;
	    			cout << "covarPlane = " << covarPlane << endl;
//	    			cout << "X.rotation() = " << X.rotation() << endl;
//	    			cout << "X.translation() = " << X.translation() << endl;
//					cout << "P.rotation() = " << P.rotation() << endl;
//	    			cout << "P.translation() = " << P.translation() << endl;
//	    			cout << "(meas.inverse() * X).rotation() = " << (meas.inverse() * X).rotation() << endl;
//	    			cout << "(meas.inverse() * X).translation() = " << (meas.inverse() * X).translation() << endl;
//	    			cout << "E.rotation() = " << E.rotation() << endl;
//	    			cout << "E.translation() = " << E.translation() << endl;
	    			cout << "jacob = " << jacob << endl;
	    			cout << "covarPose = " << covarPose << endl;
	    			cout << "information matrix = " << covarPose.inverse() << endl << endl << endl;
	    		}
	    		g2o::EdgeSE3Expmap* curEdge = new g2o::EdgeSE3Expmap();
	    		curEdge->setVertex(0, optimizer.vertex(po));
	    		curEdge->setVertex(1, optimizer.vertex(odomPoses.size() + pl));
	    		curEdge->setMeasurement(measSE3Quat);
	    		curEdge->setInformation(covarPose.inverse());

	    		optimizer.addEdge(curEdge);
	    	}
	    }

	    // Optimize!
	    static constexpr int maxIter = 1000;

	    cout << "optimizer.vertices().size() = " << optimizer.vertices().size() << endl;
	    cout << "optimizer.edges.size() = " << optimizer.edges().size() << endl;
		optimizer.initializeOptimization();
		optimizer.optimize(maxIter);

		ofstream optTrajFile("../res/optTraj.txt");
		for(int po = 0; po < odomPoses.size(); ++po){
			g2o::VertexSE3Expmap* curPoseVert = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(po));
			g2o::SE3Quat poseSE3Quat = curPoseVert->estimate();
			g2o::Vector7d poseVect = poseSE3Quat.toVector();

			optTrajFile << (po + 1);
			for(int i = 0; i < 7; ++i){
				optTrajFile << " " << poseVect[i];
			}
			optTrajFile << endl;
		}
	}
	catch(char const *str){
		cout << "Catch const char* in main(): " << str << endl;
		return -1;
	}
	catch(std::exception& e){
		cout << "Catch std exception in main(): " << e.what() << endl;
	}
	catch(...){
		cout << "Catch ... in main()" << endl;
		return -1;
	}
}
