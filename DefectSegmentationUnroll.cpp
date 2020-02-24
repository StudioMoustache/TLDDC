#include <iostream>
#include <algorithm>
#include <fstream>
#include <utility>
#include <cmath>
#include <thread>
//debug
#include <stdlib.h>
#include <time.h>


#include "DGtal/base/Common.h"
#include "DGtal/helpers/StdDefs.h"
#include "DGtal/io/Color.h"
#include "DGtal/io/boards/Board2D.h"
#include "DGtal/io/colormaps/GradientColorMap.h"
#include "DGtal/io/colormaps/HueShadeColorMap.h"

#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Eigen>
#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Geometry>
#include <eigen3/Eigen/Householder>

#include "DefectSegmentationUnroll.h"
#include "Regression.h"
#include "Statistic.h"
#include "IOHelper.h"
#include "MultiThreadHelper.h"


#include <opencv2/opencv.hpp>

using namespace DGtal;


void
DefectSegmentationUnroll::init(){
    allocate();
    computeBeginOfSegment();
    computeVectorMarks();
    computePlaneNormals();
	  convertToCcs();
    computeDiscretisation();
    allocateExtra();
}


void
DefectSegmentationUnroll::allocateExtra(){

  unrolled_surface.resize(height_div);
  for (int i = 0; i < height_div; ++i)
    unrolled_surface[i].resize(angle_div);
}

void
DefectSegmentationUnroll::computeEquations(){}

void
DefectSegmentationUnroll::computeDistances(){}



std::vector<unsigned int >
DefectSegmentationUnroll::getPointfromSegmentId(unsigned int id){
  std::vector<unsigned int> output;
  for(unsigned int i = 0; i < myPoints.size(); i++){

      if(myPoints.at(i).segmentId==id){

        output.push_back(i);
      }
  }
  return output;
}

void
DefectSegmentationUnroll::computeDiscretisation(){
  //compute mean circumference (approx by circle) => cricumference is two times Pi times radius
  double radius_average=0.;
  double circum;
  CylindricalPoint mpCurrent;
  for(unsigned int i = 0; i < myPoints.size(); i++){
    mpCurrent=myPoints.at(i);
    radius_average+=mpCurrent.radius;
  }
  radius_average/=myPoints.size();

  circum= 2*M_PI*radius_average;
  //compute height
  double height;
  CylindricalPointOrder heightOrder;
  auto minMaxElem = std::minmax_element(myPoints.begin(), myPoints.end(), heightOrder);
  double minHeight = (*minMaxElem.first).height;
  double maxHeight = (*minMaxElem.second).height;
  height=maxHeight-minHeight;
  //Discretisation of height and circum
  height_div=roundf(height);
  angle_div=roundf(circum);
}

void
DefectSegmentationUnroll::unrollSurface() {
  trace.info()<<"height :"<<height_div<<std::endl;
  trace.info()<<"angle  :"<<angle_div<<std::endl;

  CylindricalPointOrder heightOrder;
  auto minMaxElem = std::minmax_element(myPoints.begin(), myPoints.end(), heightOrder);
  double minHeight = (*minMaxElem.first).height;
  double maxHeight = (*minMaxElem.second).height;

  CylindricalPoint mpCurrent;
  int posAngle, posHeight;
  for(unsigned int i = 0; i < myPoints.size(); i++){
    mpCurrent=myPoints.at(i);
    //change range [0,2Pi] to [0,angle_div-1]
    posAngle=roundf((((angle_div-1)/(2*M_PI))*(mpCurrent.angle-(2*M_PI)))+(angle_div-1));
    //change range [minHeight,maxHeight] to [0,height_div-1]
    posHeight=roundf((((height_div-1)/(maxHeight-minHeight))*(mpCurrent.height-maxHeight))+(height_div-1));
    //add index point to the unrolled_surface
    unrolled_surface[posHeight][posAngle].push_back(i);
  }
  /*Uncomment to test mesh reconstruction from unrolled surface
  Mesh<Z3i::RealPoint> meshTest;
  for(unsigned int i = 0; i < height_div; i++){
    for(unsigned int j = 0; j < angle_div; j++){
      for(std::vector<unsigned int>::iterator it = std::begin(two_d[i][j]); it != std::end(two_d[i][j]); ++it) {
          meshTest.addVertex(pointCloud.at(*it));
      }
    }
  }
  IOHelper::export2OFF(meshTest, "segmentPoints.off");*/
}
void
DefectSegmentationUnroll::createVisuImage(std::string s) {
  cv::Mat grayscalemap(height_div,angle_div,CV_8UC1,cv::Scalar(0));
  cv::Mat reliefPictures(height_div, angle_div, CV_8UC3, cv::Scalar(110, 110, 110));
  CylindricalPointOrderRadius radiusOrder;
  auto minMaxElem2 = std::minmax_element(myPoints.begin(), myPoints.end(), radiusOrder);
  double minRadius = (*minMaxElem2.first).radius;
  double maxRadius = (*minMaxElem2.second).radius;

  int grayscaleValue;
  double normalizedValue;
  for(unsigned int i = 0; i < height_div; i++){
    for(unsigned int j = 0; j < angle_div; j++){
      normalizedValue=normalizedMap.at<double>(i, j);
      grayscaleValue=((255/1)*(normalizedValue-1))+255;
      grayscalemap.at<uchar>(i, j) = grayscaleValue;
    }
  }
  cv::applyColorMap(grayscalemap, reliefPictures, cv::COLORMAP_JET);
  imwrite( "unrollSurfacePictures/"+s, reliefPictures);
}
void
DefectSegmentationUnroll::computeNormalizedImage() {
  trace.info()<<"start compute normalized image..."<<std::endl;
  CylindricalPointOrderRadius radiusOrder;
  auto minMaxElem2 = std::minmax_element(myPoints.begin(), myPoints.end(), radiusOrder);
  double minRadius = (*minMaxElem2.first).radius;
  double maxRadius = (*minMaxElem2.second).radius;
  normalizedMap=cv::Mat::zeros(height_div,angle_div,CV_32FC4);
  double moyenneRadius;
  double normalizedValue;
  for(unsigned int i = 0; i < height_div; i++){
    for(unsigned int j = 0; j < angle_div; j++){
      moyenneRadius=getMaxVector(unrolled_surface[i][j]);
      normalizedValue=((1/(maxRadius-minRadius))*(moyenneRadius-maxRadius))+1;
      normalizedMap.at<double>(i, j) = normalizedValue;
      //trace.info()<<moyenneRadius<<std::endl;
    }
  }
}

double
DefectSegmentationUnroll::getMeansVector(std::vector<unsigned int > v) {
  double moyenneRadius=0;
  for(std::vector<unsigned int>::iterator it = std::begin(v); it != std::end(v); ++it) {
      CylindricalPoint mpCurrent = myPoints.at(*it);
      moyenneRadius+=mpCurrent.radius;
  }
  moyenneRadius/=v.size();
  return moyenneRadius;
}

double
DefectSegmentationUnroll::getMaxVector(std::vector<unsigned int > v) {
  double maxRadius=INT_MIN;
  for(std::vector<unsigned int>::iterator it = std::begin(v); it != std::end(v); ++it) {
      CylindricalPoint mpCurrent = myPoints.at(*it);
      if(mpCurrent.radius >maxRadius){
        maxRadius=mpCurrent.radius;
      }
  }
  return maxRadius;
}
double
DefectSegmentationUnroll::getMedianVector(std::vector<unsigned int > v) {
  std::vector<double> vectorOfRadius;
  unsigned int size = v.size();
  double median=0.0;
  if (size == 0)
  {
    median=0.;  // Undefined, really.
  }else{
    for(auto it = v.begin(); it != v.end(); it++) {
      double radiusCurrent = myPoints.at(*it).radius;
      vectorOfRadius.push_back(radiusCurrent);
    }

    std::sort(vectorOfRadius.begin(), vectorOfRadius.end());
    if (size % 2 == 0){
      //CylindricalPoint mpCurrent = *it;
      //trace.info()<<vectorOfRadius.at(size / 2 - 1)<<std::endl;
      median=(vectorOfRadius.at(size / 2 - 1) + vectorOfRadius.at(size / 2)) / 2;
    }else{
      //trace.info()<<vectorOfRadius.at(size / 2)<<std::endl;
      median=vectorOfRadius.at(size / 2);
    }
    //for(auto it = vTemp.begin(); it != vTemp.end(); it++) {
      //CylindricalPoint mpCurrent = *it;
      //trace.info()<<mpCurrent.radius<<std::endl;
    //}
    //trace.info()<<"----------------------------------------"<<std::endl;
  }
  return median;
}

void
DefectSegmentationUnroll::computeNormalizedImageMultiScale() {
  trace.info()<<"start compute Multi-Scale image..."<<std::endl;
  CylindricalPointOrderRadius radiusOrder;
  auto minMaxElem2 = std::minmax_element(myPoints.begin(), myPoints.end(), radiusOrder);
  double minRadius = (*minMaxElem2.first).radius;
  double maxRadius = (*minMaxElem2.second).radius;

  int decreaseHit;
  int current_resX,current_resY;
  double moyenneRadius;
  int topLeftCornerHeight,topLeftCornerTheta;

  normalizedMap=cv::Mat::zeros(height_div,angle_div,CV_32FC4);
  double normalizedValue;
  std::vector<unsigned int > t;
  //loop on all cells of unrolled surface
  for(unsigned int i = 0; i < height_div; i++){
    for(unsigned int j = 0; j < angle_div; j++){
      decreaseHit=2;
      moyenneRadius=0.;
      current_resX=angle_div;
      current_resY=height_div;
      //get the vector of point in cells i,j
      t=unrolled_surface[i][j];
      //while this ector is empty find a little resolution where the corresponding i,j cells is not empty
      while(t.empty()&& decreaseHit<64){
        //get the top left corner of the region containing P
        topLeftCornerHeight=(i/decreaseHit)*decreaseHit;
        topLeftCornerTheta=(j/decreaseHit)*decreaseHit;
        //loop on cells (of unrolledSurace) in region containing P
        for( int k = topLeftCornerHeight; k < (topLeftCornerHeight+decreaseHit); k++){
          for( int l = topLeftCornerTheta; l < (topLeftCornerTheta+decreaseHit); l++){
            //check if cells of region is in unrolled surface -> check no segmentation fault
            if((k<height_div)&&(l<angle_div)){
              //unlarge  vector size
              t.reserve(t.size() + unrolled_surface[k][l].size());
              //concat vector
              t.insert(t.end(), unrolled_surface[k][l].begin(),  unrolled_surface[k][l].end());
            }
          }
        }
        decreaseHit*=decreaseHit;
      }
      //if t is empty, thats means that even with a multi scale resolution( to 1/(4^5)) we can't find info
      if(!t.empty()){
        //moyenneRadius=getMeansVector(t);
        //moyenneRadius=getMaxVector(t);
        moyenneRadius=getMedianVector(t);
        normalizedValue=((1/(maxRadius-minRadius))*(moyenneRadius-maxRadius))+1;
        normalizedMap.at<double>(i, j) = normalizedValue;
      }

    }
  }
}