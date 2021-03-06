/*
 * Apr. 4, 2017, He Zhang, hxzhang1@ualr.edu
 *
 * Generate 3D map: 
 *     input:  a trajectory.log and img dir where the images are stored 
 *     output: a .ply file, shown the 3d point cloud 
 *
 * */

#include <ros/ros.h>
#include <iostream>
#include <string>
#include <fstream>
#include <cmath>
// #include "std_msgs/Float32MultiArray.h"
#include <vector>
#include <gtsam/geometry/Pose3.h>
#include "sparse_feature_vo.h"
#include "cam_model.h"
#include "SR_reader_cv.h"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"

using namespace gtsam; 
using namespace std; 

#define D2R(d) (((d)*M_PI)/180)

string fname("/home/david/work/data/sr4k/imu_bdat/etas_f5/imu_v100.log"); 

struct trajItem
{
  // int id; int sid; // sequence id 
  double timestamp; 
  // string timestamp; 
  float px, py, pz; // euler angle roll, pitch, yaw; 
  float qx, qy, qz, qw; // quaternion
};

void setTu2c(Pose3& Tu2c); 
void headerPLY(std::ofstream& ouf, int vertex_number);
bool mapPLY(std::string f, std::string img_dir, std::string outPLY, int skip, int t_skip, float depth_scale, CSparseFeatureVO* pSF, int* rect = 0);
bool readTraj(std::string f, vector<struct trajItem>& );
void LoadImages(string fAsso, vector<string> & vRGB, vector<string>& vDPT, vector<double>& vTime); 
int  findIndex(double t, vector<double>& vTime); 

int main(int argc, char* argv[])
{
  ros::init(argc, argv, "mapping_PLY_rs"); 
  ros::NodeHandle n; 
  // euler_pub = n.advertise<std_msgs::Float32MultiArray>("/euler_msg", 10); \

  // CamModel sr4k(250.5773, 250.5773, 90, 70, -0.8466, 0.5370); 
  CamModel rs_F200(621.94176, 625.3561, 318.77554, 236.21507, 0.08011, -0.56891); 
  rs_F200.m_rows = 480; // height
  rs_F200.m_cols = 640; // width
  // sr4k.z_offset = 0.015;  // this is only for sr4k 
  float depth_scale = 0.001;
  CSparseFeatureVO spr_vo(rs_F200);

  // parameters 
  ros::NodeHandle np("~"); 
  int skip = 2; 
  int t_skip = 1; 
  string trajFile;  // the trajectory log file 
  string imgDir;    // images dir 
  string outPLY;    // the output PLY file 
  
  int su, sv, eu, ev ; 

  np.param("trajectory_file", trajFile, trajFile); 
  np.param("img_directory", imgDir, imgDir); 
  np.param("output_PLY_file", outPLY, outPLY); 
  np.param("downsample_skip", skip, skip); 
  np.param("trajectory_skip", t_skip, t_skip); 
  np.param("top_left_u", su, 0); 
  np.param("top_left_v", sv, 0); 
  np.param("bot_right_u", eu, rs_F200.m_cols); 
  np.param("bot_right_v", ev, rs_F200.m_rows); 
  
  int rect[4] = {su, sv, eu, ev}; 

  cout <<"rect "<<rect[0]<<" "<<rect[1]<<" "<<rect[2]<<" "<<rect[3]<<endl;

  mapPLY(trajFile, imgDir, outPLY, skip, t_skip, depth_scale, &spr_vo, &rect[0]);

  ROS_INFO("Finish mapping into %s!", outPLY.c_str());

  return 0; 
}

bool mapPLY(std::string f, std::string img_dir, std::string outPLY, int skip, int t_skip, float depth_scale, CSparseFeatureVO* pSF, int* rect)
{
  // output file 
  ofstream ouf(outPLY.c_str()); 
  if(!ouf.is_open())
  {
    cout <<" failed to open output PLY file : "<<outPLY<<endl; 
    return false; 
  }

  // generate a global point cloud 
  vector<trajItem> t; 
  if(!readTraj(f, t))
  {
    return false; 
  }
  
  ROS_INFO("succeed to load %d pose ", t.size()); 

  vector<float> pts_loc; 
  vector<unsigned char> pts_col;
  cv::Mat i_img, d_img; 
  cv::Mat d_filter; 
  // CSReadCV r4k; 

  ros::NodeHandle np("~"); 
  string record_file(""); 
  np.param("record_file", record_file, record_file); 
  
  vector<string> vRGBs; vector<string> vDPTs; vector<double> vTimes; 
  LoadImages(record_file, vRGBs, vDPTs, vTimes); 

  // transform point from camera to imu 
  // Pose3 Tu2c ; 
  // setTu2c(Tu2c); 

  for(int i=0; i<t.size(); i+= t_skip)
  {
    trajItem& ti = t[i]; 

    // construct pose3
    // Rot3 R = Rot3::RzRyRx(ti.roll, ti.pitch, ti.yaw); 
    
    // This is a bug in GTSAM, where the correct sequence is R(w, x, y, z) not R(x, y, z, w)
    Rot3 R(ti.qw, ti.qx, ti.qy, ti.qz); 
    // Rot3 R(ti.qx, ti.qy, ti.qz, ti.qw); 
    Point3 t; t << ti.px, ti.py, ti.pz; 
    Pose3 p = Pose3(R, t); 

    // get image 
    stringstream ss_rgb, ss_dpt; 
    // ss<<img_dir<<"/d1_"<<setfill('0')<<setw(7)<<ti.sid<<".bdat";  // setw(4)
    // r4k.readOneFrameCV(ss.str(), i_img, d_img);
  
    // 
    int index = findIndex(ti.timestamp, vTimes); 
    if(index < 0)
    {
      cout <<"mapping_PLY_rs.cpp: find to find timestamp: "<<ti.timestamp<<endl;
      break; 
    }

    // read image 
    // ss_rgb << img_dir <<"/color/"<<ti.timestamp<<".png"; 
    // ss_dpt << img_dir <<"/depth/"<<ti.timestamp<<".png"; 
    ss_rgb << img_dir <<"/"<<vRGBs[index]; 
    ss_dpt << img_dir <<"/"<<vDPTs[index]; 

    i_img = cv::imread(ss_rgb.str().c_str(), -1); 
    d_img = cv::imread(ss_dpt.str().c_str(), -1); 
    if(i_img.data == NULL || d_img.data == NULL)
    {
      ROS_ERROR("failed to load camera data at dir %s with timestamp = %lf", img_dir.c_str(), ti.timestamp); 
      ROS_WARN("ss_rgb = %s ss_dpt = %s", ss_rgb.str().c_str(), ss_dpt.str().c_str());
      break; 
    }

    cv::medianBlur(d_img, d_filter, 5);  // median filter for depth data 
    
    // compute local point cloud 
    vector<float> p_loc; 
    vector<unsigned char> p_col; 
    pSF->generatePointCloud(i_img, d_filter, skip, depth_scale, p_loc, p_col, rect); 

    // transform into global 
    Point3 fo, to, to_imu; 
    for(int i=0; i<p_loc.size(); i+=3)
    {
      fo << p_loc[i], p_loc[i+1], p_loc[i+2]; 
      // to << p_loc[i], p_loc[i+1], p_loc[i+2]; 
      // to_imu = Tu2c.transform_from(fo);
      to = p.transform_from(fo); 
      p_loc[i] = to(0); p_loc[i+1] = to(1); p_loc[i+2] = to(2); 
    }
    
    pts_loc.insert(pts_loc.end(), p_loc.begin(), p_loc.end()); 
    pts_col.insert(pts_col.end(), p_col.begin(), p_col.end());

    // ouf<<p.x()<<" "<<p.y()<<" "<<p.z()<<" "<<g_color[c][0]<<" "<<g_color[c][1]<<" "<<g_color[c][2]<<endl;
    ROS_INFO("handle frame at timestamp %lf, add %d pts", ti.timestamp, p_loc.size());
  }
  
  // output into file 

  // first, add header 
  
  int vertex_number = pts_loc.size()/3; 
  headerPLY(ouf, vertex_number);

  for(int i=0; i<pts_loc.size(); i+=3)
  {
    ouf<< pts_loc[i]<<" "<<pts_loc[i+1]<<" "<<pts_loc[i+2]<<" "<<(int)pts_col[i]<<" "<<(int)pts_col[i+1]<<" "<<(int)pts_col[i+2]<<endl;
  }
  ouf.flush(); 
  ouf.close(); 
return true;  
}
  
void headerPLY(std::ofstream& ouf, int vertex_number)
{
  ouf << "ply"<<endl
    <<"format ascii 1.0"<<endl
    <<"element vertex "<<vertex_number<<endl
    <<"property float x"<<endl
    <<"property float y"<<endl
    <<"property float z"<<endl
    <<"property uchar red"<<endl
    <<"property uchar green"<<endl
    <<"property uchar blue"<<endl
    <<"end_header"<<endl;
}


bool readTraj(std::string f, vector<struct trajItem>& t)
{

  ifstream inf(f.c_str()); 
  if(!inf.is_open())
  {
    cout<<"failed to open file : "<<f<<endl;
    return false;
  }

  char buf[4096]; 
  char b[21]={0}; 
  while(inf.getline(buf, 4096))
  {
    trajItem ti; 
    // sscanf(buf, "%d %f %f %f %f %f %f %d", &ti.id, &ti.px, &ti.py, &ti.pz, &ti.roll, &ti.pitch, &ti.yaw, &ti.sid); 
    // sscanf(buf, "%d %f %f %f %f %f %f %f %d", &ti.id, &ti.px, &ti.py, &ti.pz, &ti.qx, &ti.qy, &ti.qz, &ti.qw, &ti.sid); 
    sscanf(buf, "%lf %f %f %f %f %f %f %f", &ti.timestamp, &ti.px, &ti.py, &ti.pz, &ti.qx, &ti.qy, &ti.qz, &ti.qw); 
    // ti.timestamp = string(b); 
    t.push_back(ti); 
    // if(t.size() < 10)
    {
      // printf("read %lf %f %f %f %f %f %f %f\n", ti.timestamp.c_str(), ti.px, ti.py, ti.pz, ti.qx, ti.qy, ti.qz, ti.qw);
    }
  }

  cout << " read "<<t.size()<<" trajectory items"<<endl;
  return true;

}

int findIndex(double t, vector<double>& vTime)
{
  for(int i=0; i<vTime.size(); i++)
  {
    if(vTime[i] == t)
      return i; 
    if(vTime[i] > t)
      return -1; 
  }
  return -1; 
}


void LoadImages(string fAsso, vector<string> & vRGB, vector<string>& vDPT, vector<double>& vTime)
{
  ifstream inf(fAsso.c_str()); 
  double t; 
  string sRGB, sDPT; 
  while(!inf.eof())
  {
    string s; 
    getline(inf, s); 
    if(!s.empty())
    {
      stringstream ss; 
      ss << s; 
      ss >> t;
      vTime.push_back(t); 
      ss >> sRGB; 
      vRGB.push_back(sRGB); 
      ss >> t; 
      ss >> sDPT; 
      vDPT.push_back(sDPT); 
    }
  }
  cout <<"mapping_PLY_rs.cpp: load "<<vTime.size()<<" records"<<endl;
}


void setTu2c(Pose3& Tu2c)
{
  //// Body/IMU Coordinate System 
  //          
  //             X
  //            /
  //           /
  //          /_____ Y
  //          |
  //          |
  //        Z | 
  //
  //        
  //   CAMERA Coordinate System    
  //           Z 
  //          /
  //         / 
  //        /  
  //        ------- X
  //        |
  //        |
  //        |Y
  //

  float p = 0; 
  Rot3 R_g2b = Rot3::RzRyRx( M_PI/2., 0. , M_PI/2.); // roll, pitch, yaw, in BODY coordinate system 
  Rot3 R_b2o = Rot3::RzRyRx(p ,0 , 0); 
  Rot3 R_g2o = R_g2b * R_b2o; 
  // Rot3 R_g2o = R_b2o * R_g2b; 

  Point3 t = Point3::Zero(); 
  // (*mp_u2c) = Pose3::Create(R_g2o, t);
  Tu2c = Pose3::Create(R_g2o, t); 
   t<< 1, 3, 2; 
   cout << t<< " after rotation "<<endl <<Tu2c*t<<endl;
   cout << "rotation pitch: "<< R_b2o * t<<endl; 
   cout << "rotation all: "<< R_g2b * R_b2o * t<<endl; 

  return ; 
}
