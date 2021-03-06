/*
    This file is part of ofxActiveScan.

    ofxActiveScan is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ofxActiveScan is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ofxActiveScan.  If not, see <http://www.gnu.org/licenses/>.

    Naoto Hieda <micuat@gmail.com> 2013
 */

#include "testApp.h"

using namespace ofxActiveScan;

void testApp::setup() {
	if( rootDir.size() > 0 ) {
		init();
		pathLoaded = true;
	} else {
		pathLoaded = false;
	}
}

void testApp::init() {
	ofSetLogLevel(OF_LOG_VERBOSE);
	
	/*
	// load correspondences estimated by decode program 
	Map2f horizontal(ofToDataPath(rootDir + "/h.map", true));
	Map2f vertical(ofToDataPath(rootDir + "/v.map", true));  
	ofImage mask;
	ofLoadImage(mask, ofToDataPath(rootDir + "/mask.bmp"));
	*/
	
	if( rootDir.size() != 2 ) {
		ofLogError() << "Wrong argument number";
		exit();
	}
	
	vector<ofImage> masks;
	ofImage mask;
	masks.resize(rootDir.size());
	ofLoadImage(masks[0], ofToDataPath(rootDir[0] + "/mask.bmp"));
	ofLoadImage(masks[1], ofToDataPath(rootDir[1] + "/mask.bmp"));
	
	mask = masks[0];
	
	// Find camera pixels shared by two projectors
	for (int y=0; y<mask.getHeight(); y++) {
		for (int x=0; x<mask.getWidth(); x++) {
			if( masks[0].getColor(x, y).getLightness() < 128
				|| masks[1].getColor(x, y).getLightness() < 128 ) {
				//cout << x << ' ' << y << endl;
				mask.setColor(x, y, 0);
			}
		}
	}
	
	vector<cv::Mat> ci, pi, ce, pe;
	vector<double> cd, pd;
	vector<Options> options;
	vector<Map2f> horizontals, verticals;
	
	for( int i = 0 ; i < rootDir.size() ; i++ ) {
		Options op;
		cv::Size camSize, proSize;
		
		cv::FileStorage fs(ofToDataPath(rootDir[i] + "/config.yml"), cv::FileStorage::READ);
		fs["proWidth"] >> op.projector_width;
		fs["proHeight"] >> op.projector_height;
		fs["camWidth"] >> cw;
		fs["camHeight"] >> ch;
		fs["vertical_center"] >> op.projector_horizontal_center;
		fs["nsamples"] >> op.nsamples;
		
		Map2f horizontal(ofToDataPath(rootDir[i] + "/h.map", true));
		Map2f vertical(ofToDataPath(rootDir[i] + "/v.map", true));  
		
		ofImage orgMask;
		ofLoadImage(orgMask, ofToDataPath(rootDir[i] + "/mask.bmp"));
		
		cv::Mat camIntrinsic, proIntrinsic, proExtrinsic;
		double camDist, proDist;
		
		cv::FileStorage cfs(ofToDataPath(rootDir[i] + "/calibration.yml"), cv::FileStorage::READ);
		cfs["camIntrinsic"] >> camIntrinsic;
		cfs["camDistortion"] >> camDist;
		cfs["proIntrinsic"] >> proIntrinsic;
		cfs["proDistortion"] >> proDist;
		cfs["proExtrinsic"] >> proExtrinsic;
		
		ci.push_back(camIntrinsic);
		pi.push_back(proIntrinsic);
		pe.push_back(proExtrinsic);
		cd.push_back(camDist);
		pd.push_back(proDist);
		
		overlapMesh.push_back(
			triangulate(op, horizontal, vertical, toAs(mask),
					toAs(camIntrinsic), camDist,
					toAs(proIntrinsic), proDist, toAs(proExtrinsic), mask)
		);
		
		mesh.push_back(
			triangulate(op, horizontal, vertical, toAs(orgMask),
					toAs(camIntrinsic), camDist,
					toAs(proIntrinsic), proDist, toAs(proExtrinsic), orgMask)
		);
		
		options.push_back(op);
		horizontals.push_back(horizontal);
		verticals.push_back(vertical);
	}
	
	
	for( int i = 0 ; i < mesh[0].getNumVertices() ; i++ ) {
		mesh[0].setColor(i, ofColor::red);
	}
	for( int i = 0 ; i < mesh[1].getNumVertices() ; i++ ) {
		mesh[1].setColor(i, ofColor::green);
	}
	
	ofMesh input, target;
	
	// Downsample to reduce complexity
	for( int i = 0 ; i < overlapMesh[0].getNumVertices() ; i+=100 ) {
		input.addVertex(overlapMesh[0].getVertex(i));
		target.addVertex(overlapMesh[1].getVertex(i));
	}
	
	ofLogNotice() << "Input points: " << input.getNumVertices();
	
	cv::Mat Rt = findTransform(input, target, 2000);
	
	ofLogNotice() << Rt << endl;
	
	mesh[0] = transformMesh(mesh[0], Rt);
	overlapMesh[0] = transformMesh(overlapMesh[0], Rt);
	
	// Average point cloud
	for( int i = 0 ; i < overlapMesh[0].getNumVertices() ; i++ ) {
		avg.addVertex((overlapMesh[0].getVertex(i) + overlapMesh[1].getVertex(i)) / 2);
		avg.addColor(ofColor::white);
	}
	
	// Re-calibration using averaged point cloud
	vector<vector<cv::Point3f> > objectPoints(1);
	for( int j = 0 ; j < avg.getNumVertices(); j++ ) {
		objectPoints[0].push_back(ofxCv::toCv(avg.getVertex(j)));
	}
	
	for( int i = 0 ; i < rootDir.size() ; i++ ) {
		ofLogNotice() << "Camera " << i;
		
		cv::Mat cameraMatrix = ci[i];
		vector<cv::Mat> rvecs, tvecs;
		cv::Mat distCoeffs = cv::Mat::zeros(1, 4, CV_32F);
		vector<vector<cv::Point2f> > imagePoints(1);
		cv::Size2i imageSize(mask.getWidth(), mask.getHeight());
		int flags =
			CV_CALIB_USE_INTRINSIC_GUESS |
			CV_CALIB_FIX_PRINCIPAL_POINT |
			CV_CALIB_FIX_ASPECT_RATIO |
			CV_CALIB_ZERO_TANGENT_DIST |
			CV_CALIB_FIX_K2 |
			CV_CALIB_FIX_K3;
		
		for (int y=0; y<mask.getHeight(); y++) {
			for (int x=0; x<mask.getWidth(); x++) {
				if( mask.getColor(x, y).getLightness() > 0 ) {
					imagePoints[0].push_back(cv::Point2f(x, y));
				}
			}
		}
		
		//distCoeffs.at<double>(0) = cd[i];
		
		ofLogNotice() << cameraMatrix;
		calibrateCamera(objectPoints, imagePoints, imageSize, cameraMatrix, distCoeffs, rvecs, tvecs, flags);
		ofLogNotice() << cameraMatrix;
		ofLogNotice() << distCoeffs;
		ofLogNotice() << rvecs[0];
		ofLogNotice() << tvecs[0];
		
		cv::FileStorage cfs(ofToDataPath(rootDir[i] + "/calibration2.yml"), cv::FileStorage::WRITE);
		cfs << "camIntrinsic"  << cameraMatrix;
		cfs << "camDistortion"  << distCoeffs.at<double>(0) / ci[i].at<double>(0,0) / ci[i].at<double>(0,0);
		
		cv::Mat r;
		cv::Rodrigues(rvecs[0], r);
		cv::Mat t = tvecs[0];
		cv::Mat Rt = (cv::Mat1d(4, 4) << r.at<double>(0,0), r.at<double>(0,1), r.at<double>(0,2), t.at<double>(0),
		      r.at<double>(1,0), r.at<double>(1,1), r.at<double>(1,2), t.at<double>(1),
		      r.at<double>(2,0), r.at<double>(2,1), r.at<double>(2,2), t.at<double>(2),
		      0, 0, 0, 1);
		ce.push_back(Rt);
	}
	
	for( int i = 0 ; i < rootDir.size() ; i++ ) {
		ofLogNotice() << "Projector " << i;
		
		cv::Mat cameraMatrix = pi[i];
		vector<cv::Mat> rvecs, tvecs;
		cv::Mat distCoeffs = cv::Mat::zeros(1, 4, CV_32F);
		vector<vector<cv::Point2f> > imagePoints(1);
		// take height margin in case pricincipal point is out of image
		cv::Size2i imageSize(options[i].projector_width, options[i].projector_height*2);
		int flags =
			CV_CALIB_USE_INTRINSIC_GUESS |
			CV_CALIB_FIX_PRINCIPAL_POINT |
			CV_CALIB_FIX_ASPECT_RATIO |
			CV_CALIB_ZERO_TANGENT_DIST |
			CV_CALIB_FIX_K2 |
			CV_CALIB_FIX_K3;
		
		for (int y=0; y<mask.getHeight(); y++) {
			for (int x=0; x<mask.getWidth(); x++) {
				if( mask.getColor(x, y).getLightness() > 0 ) {
					imagePoints[0].push_back(cv::Point2f(horizontals[i].cell(x, y), verticals[i].cell(x, y)));
				}
			}
		}
		
		//distCoeffs.at<double>(0) = pd[i];
		
		ofLogNotice() << cameraMatrix;
		calibrateCamera(objectPoints, imagePoints, imageSize, cameraMatrix, distCoeffs, rvecs, tvecs, flags);
		ofLogNotice() << cameraMatrix;
		ofLogNotice() << distCoeffs;
		ofLogNotice() << rvecs[0];
		ofLogNotice() << tvecs[0];
		
		cv::Mat r;
		cv::Rodrigues(rvecs[0], r);
		cv::Mat t = tvecs[0];
		cv::Mat Rt = (cv::Mat1d(4, 4) << r.at<double>(0,0), r.at<double>(0,1), r.at<double>(0,2), t.at<double>(0),
		      r.at<double>(1,0), r.at<double>(1,1), r.at<double>(1,2), t.at<double>(1),
		      r.at<double>(2,0), r.at<double>(2,1), r.at<double>(2,2), t.at<double>(2),
		      0, 0, 0, 1);
		Rt = Rt * ce[i].inv();
		
		cv::FileStorage cfs(ofToDataPath(rootDir[i] + "/calibration2.yml"), cv::FileStorage::APPEND);
		cfs << "proIntrinsic"  << cameraMatrix;
		cfs << "proExtrinsic"  << (cv::Mat1d(3, 4) << Rt.at<double>(0,0), Rt.at<double>(0,1), Rt.at<double>(0,2), Rt.at<double>(0,3),
		      Rt.at<double>(1,0), Rt.at<double>(1,1), Rt.at<double>(1,2), Rt.at<double>(1,3),
		      Rt.at<double>(2,0), Rt.at<double>(2,1), Rt.at<double>(2,2), Rt.at<double>(2,3));
		cfs << "proDistortion"  << distCoeffs.at<double>(0) / pi[i].at<double>(0,0) / pi[i].at<double>(0,0);
	}

	curMesh = mesh.begin();
	transformed = false;
	
	//cam.cacheMatrices(true);
}

void testApp::update() {
}

void testApp::draw() {
	ofBackground(0);
	
	if( pathLoaded ) {
		
		cam.begin();
		ofScale(1, -1, -1);
		ofScale(1000, 1000, 1000);
		ofTranslate(0, 0, -2);
		
		if( transformed ) {
			mesh[0].drawVertices();
			mesh[1].drawVertices();
			//avg.drawVertices();
		} else {
			curMesh->drawVertices();
		//	avg.drawVertices();
		}
		
		cam.end();
		
	}
}

void testApp::keyPressed(int key) {
	if( pathLoaded ) {
		
		if( key == '1' || key == '2' ) {
			curMesh = mesh.begin() + (key - '1');
			transformed = false;
		}
		if( key == '0' ) {
			transformed = true;
		}
		
	}
}

void testApp::dragEvent(ofDragInfo dragInfo){
	if( !pathLoaded ) {
		for( int i = 0 ; i < dragInfo.files.size() ; i++ ) {
			rootDir.push_back(dragInfo.files[i]);
		}
		init();
		pathLoaded = true;
	}
}
