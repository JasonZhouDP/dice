// @HEADER
// ************************************************************************
//
//               Digital Image Correlation Engine (DICe)
//                 Copyright 2015 National Technology & Engineering Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY NTESS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NTESS OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact: Dan Turner (dzturne@sandia.gov)
//
// ************************************************************************
// @HEADER

#include <DICe_CameraSystem.h>
#include <DICe_Parser.h>
#include <DICe_XMLUtils.h>

#include <fstream>
#include <Teuchos_XMLParameterListHelpers.hpp>
#include <Teuchos_ParameterList.hpp>
#include <Teuchos_Array.hpp>
#include <Teuchos_RCP.hpp>

namespace DICe{

Camera_System::Camera_System():
  max_num_cameras_allowed_(10),
  user_6x1_trans_(6, 0.0),
  sys_type_(UNKNOWN_SYSTEM),
  has_6_transform_(false),
  has_4x4_transform_(false)
// the 4x4 transform matrix is default initialized to 0 in the constructor
//  has_opencv_rot_trans_(false)
{};

Camera_System::Camera_System(const std::string & param_file_name):
  Camera_System(){
  read_calibration_file(param_file_name);
};

void
Camera_System::read_calibration_file(const std::string & cal_file) {
  DEBUG_MSG(" ");
  DEBUG_MSG("***************************** read calibration file **************************");

  const std::string xml("xml");
  const std::string txt("txt");
  bool valid_dice_xml = true;

  DEBUG_MSG("Camera_System::read_calibration_file(): Trying to read file with Teuchos XML parser: " << cal_file);
  Teuchos::RCP<Teuchos::ParameterList> sys_params = Teuchos::rcp(new Teuchos::ParameterList());
  Teuchos::Ptr<Teuchos::ParameterList> sys_params_ptr(sys_params.get());
  try {
    Teuchos::updateParametersFromXmlFile(cal_file, sys_params_ptr);
  }
  catch (...) {
    DEBUG_MSG("Camera_System::read_calibration_file(): Invalid DICe XML file: " << cal_file);
    DEBUG_MSG("Camera_System::read_calibration_file(): Assuming another XML format (possibly VIC3d, etc)");
    valid_dice_xml = false;
  }

  if (valid_dice_xml) {  //file read into parameters
    DEBUG_MSG("Camera_System::read_calibration_file(): valid XML file for Teuchos parser");
    const bool is_dice_xml_cal_file = sys_params->isParameter(DICe_XML_Calibration_File);
    if (!is_dice_xml_cal_file) {
      TEUCHOS_TEST_FOR_EXCEPTION(true,std::runtime_error,
        "Camera_System::read_calibration_file(): DICe XML calibration file not valid");
    }
    DEBUG_MSG("Camera_System::read_calibration_file(): DICe XML cal file format");
    TEUCHOS_TEST_FOR_EXCEPTION(!sys_params->isParameter(system_type_3D),std::runtime_error,
      "calibration file missing " << system_type_3D);
    std::string sys_type_str = sys_params->get<std::string>(system_type_3D);
    DEBUG_MSG("Camera_System::read_calibration_file(): " << system_type_3D << " = " << sys_type_str);
    sys_type_ = string_to_system_type_3d(sys_type_str);

    //cycle through all the cameras to see if they are assigned
    for(size_t i=0;i<max_num_cameras_allowed_;++i){
      TEUCHOS_TEST_FOR_EXCEPTION(i==max_num_cameras_allowed_-1,std::runtime_error,"too many cameras defined in the xml calibration file");
      std::stringstream camera_sublist_id;
      camera_sublist_id << "CAMERA " << i << std::endl;
      if (sys_params->isSublist(camera_sublist_id.str())) {
        DEBUG_MSG("Camera_System::read_calibration_file(): reading " << camera_sublist_id.str());
        Teuchos::ParameterList camParams = sys_params->sublist(camera_sublist_id.str());
        // check that the camera parameters have everything needed
        TEUCHOS_TEST_FOR_EXCEPTION(!camParams.isParameter("IMAGE_HEIGHT_WIDTH"),std::runtime_error,
          "calibration file missing IMAGE_HEIGHT_WIDTH");
        TEUCHOS_TEST_FOR_EXCEPTION(!camParams.isParameter("LENS_DISTORTION_MODEL"),std::runtime_error,
          "calibration file missing LENS_DISTORITION_MODEL");
        Camera::Camera_Info camera_info;
        //the lens distorion model is handled here
        camera_info.lens_distortion_model_ = Camera::string_to_lens_distortion_model(camParams.get<std::string>("LENS_DISTORTION_MODEL"));
        DEBUG_MSG("Camera_System::read_calibration_file(): found lens distortion model " << Camera::to_string(camera_info.lens_distortion_model_));
        std::string height_width_text = camParams.get<std::string>("IMAGE_HEIGHT_WIDTH");
        Teuchos::Array<int_t>  tArray;
        tArray = Teuchos::fromStringToArray<int_t>(height_width_text);
        camera_info.image_height_ = tArray[0];
        camera_info.image_width_ = tArray[1];
        DEBUG_MSG("Camera_System::read_calibration_file(): found image height: " << camera_info.image_height_);
        DEBUG_MSG("Camera_System::read_calibration_file(): found image width: " << camera_info.image_width_);
        //fill the array with any intrinsic parameters
        for (int_t i = 0; i < Camera::MAX_CAM_INTRINSIC_PARAM; i++) {
          if (camParams.isParameter(Camera::to_string(static_cast<Camera::Cam_Intrinsic_Param>(i)))) {
            camera_info.intrinsics_[i] = camParams.get<std::double_t>(Camera::to_string(static_cast<Camera::Cam_Intrinsic_Param>(i)));
            DEBUG_MSG("Camera_System::read_calibration_file(): found " << Camera::to_string(static_cast<Camera::Cam_Intrinsic_Param>(i)) <<
              " value: " << camera_info.intrinsics_[i]);
          }
        }
        // read the extrinsic translations
        if (camParams.isParameter("TX")) {
          camera_info.tx_ = camParams.get<double>("TX");
          DEBUG_MSG("Camera_System::read_calibration_file(): found extrinsic x translation: " << camera_info.tx_);
        }
        if (camParams.isParameter("TY")) {
          camera_info.ty_ = camParams.get<double>("TY");
          DEBUG_MSG("Camera_System::read_calibration_file(): found extrinsic y translation: " << camera_info.ty_);
        }
        if (camParams.isParameter("TZ")) {
          camera_info.tz_ = camParams.get<double>("TZ");
          DEBUG_MSG("Camera_System::read_calibration_file(): found extrinsic z translation: " << camera_info.tz_);
        }
        scalar_t alpha = 0;
        scalar_t beta = 0;
        scalar_t gamma = 0;
        bool has_eulers = false;
        if (camParams.isParameter("ALPHA")) {
          alpha = camParams.get<double>("ALPHA");
          has_eulers = true;
          DEBUG_MSG("Camera_System::read_calibration_file(): found euler angle alpha: " << alpha);
        }
        if (camParams.isParameter("BETA")) {
          beta = camParams.get<double>("BETA");
          has_eulers = true;
          DEBUG_MSG("Camera_System::read_calibration_file(): found euler angle beta: " << beta);
        }
        if (camParams.isParameter("GAMMA")) {
          gamma = camParams.get<double>("GAMMA");
          has_eulers = true;
          DEBUG_MSG("Camera_System::read_calibration_file(): found euler angle gamma: " << gamma);
        }
        if(has_eulers){
          camera_info.set_rotation_matrix(alpha,beta,gamma);
        }
        //Get the camera ID if it exists
        std::string camera_id;
        if (camParams.isParameter("CAMERA_ID")) {
          camera_id = camParams.get<std::string>("CAMERA_ID");
          DEBUG_MSG("Camera_System::read_calibration_file(): found CAMERA_ID: " << camera_id);
        } else { //If not use the camera and number by default
          std::stringstream camera_id_ss;
          camera_id_ss << "CAMERA " << i;
          camera_id = camera_id_ss.str();
          DEBUG_MSG("Camera_System::read_calibration_file(): CAMERA_ID not found using default: " << camera_id);
        }
        camera_info.id_ = camera_id;
        //Get the pixel depth if it exists
        if (camParams.isParameter("PIXEL_DEPTH")) {
          camera_info.pixel_depth_ = camParams.get<int_t>("PIXEL_DEPTH");
          DEBUG_MSG("Camera_System::read_calibration_file(): found PIXEL_DEPTH: " << camera_info.pixel_depth_);
        }
        //get the lens comment if it exists
        if (camParams.isParameter("LENS")) {
          camera_info.lens_ = camParams.get<std::string>("LENS");
          DEBUG_MSG("Camera_System::read_calibration_file(): found LENS: " << camera_info.lens_);
        }
        //get the general comments if they exists
        if (camParams.isParameter("COMMENTS")) {
          camera_info.comments_ = camParams.get<std::string>("COMMENTS");
          DEBUG_MSG("Camera_System::read_calibration_file(): found COMMENTS: " << camera_info.comments_);
        }
        //does the camera have a 3x3 rotation transformation matrix?
        if (camParams.isSublist(rotation_3x3_matrix)) {
          DEBUG_MSG("Camera_System::read_calibration_file(): found " << rotation_3x3_matrix);
          TEUCHOS_TEST_FOR_EXCEPTION(has_eulers,std::runtime_error,"cannot specify euler angles and rotation matrix");
          Teuchos::ParameterList camRot = camParams.sublist(rotation_3x3_matrix);
          Teuchos::Array<scalar_t>  tArray;
          for (int_t j = 0; j < 3; j++) {
            std::stringstream row_param;
            row_param << "ROW " << j;
            TEUCHOS_TEST_FOR_EXCEPTION(camRot.isParameter(row_param.str()),std::runtime_error,
              "cal file missing row for camera 3x3 rotation matrix");
            std::string row_text = camRot.get<std::string>(row_param.str());
            tArray = Teuchos::fromStringToArray<scalar_t>(row_text);
            for (int_t i = 0; i < 3; i++)
              camera_info.rotation_matrix_(j,i) = tArray[i];
          }
        }
        Teuchos::RCP<DICe::Camera> camera_ptr = Teuchos::rcp(new DICe::Camera(camera_info));
        cameras_.push_back(camera_ptr);
        DEBUG_MSG("Camera_System::read_calibration_file(): successfully loaded camera " << camera_id);
      }else{
        break;
      }
    } // end camera i loop
    // the new DICe XML format is the only format where the user can specify custom transforms
    // does the file have a 6 parameter transform?
    if (sys_params->isParameter(user_6_param_transform)) {
      DEBUG_MSG("Camera_System::read_calibration_file(): found " << user_6_param_transform);
      has_6_transform_ = true;
      std::string param_text = sys_params->get<std::string>(user_6_param_transform);
      Teuchos::Array<scalar_t>  tArray;
      tArray = Teuchos::fromStringToArray<scalar_t>(param_text);
      assert(tArray.size()==6);
      for (int_t i = 0; i < 6; i++)
        user_6x1_trans_[i] = tArray[i];
    } // end 6 param transform
    // does the file have a 4x4 parameter transform?
    if (sys_params->isSublist(user_4x4_param_transform)) {
      DEBUG_MSG("Camera_System::read_calibration_file(): found " << user_4x4_param_transform);
      has_4x4_transform_ = true;
      Teuchos::ParameterList camRow = sys_params->sublist(user_4x4_param_transform);
      Teuchos::Array<scalar_t>  tArray;
      for (int_t j = 0; j < 4; j++) {
        std::stringstream row_param;
        row_param << "ROW " << j;
        std::string row_text = camRow.get<std::string>(row_param.str());
        tArray = Teuchos::fromStringToArray<scalar_t>(row_text);
        assert(tArray.size()==4);
        for (int_t i = 0; i < 4; i++)
          user_4x4_trans_(j,i) = tArray[i];
      }
    } // end 4x4 transform
//    //does the file have a 4x3 opencv parameter transform?
//    if (sys_params->isSublist(opencv_3x4_param_transform)) {
//      DEBUG_MSG("Camera_System::read_calibration_file(): found " << opencv_3x4_param_transform);
//      has_opencv_rot_trans_ = true;
//      Teuchos::ParameterList camRow = sys_params->sublist(opencv_3x4_param_transform);
//      Teuchos::Array<scalar_t>  tArray;
//      for (int_t j = 0; j < 3; j++) {
//        std::stringstream row_param;
//        row_param << "ROW " << j;
//        std::string row_text = camRow.get<std::string>(row_param.str());
//        tArray = Teuchos::fromStringToArray<scalar_t>(row_text);
//        assert(tArray.size()==4);
//        for (int_t i = 0; i < 4; i++)
//          opencv_3x4_trans_(j,i) = tArray[i];
//      }
//    } // end opencv transform
  } // end valid DICe XML
  else { // must be an xml file from VIC3d or text file
    DEBUG_MSG("Camera_System::read_calibration_file(): Parsing calibration parameters from file: " << cal_file);
    std::fstream dataFile(cal_file.c_str(), std::ios_base::in);
    if (!dataFile.good()) {
      TEUCHOS_TEST_FOR_EXCEPTION(!dataFile.good(), std::runtime_error,
        "Error, the calibration file does not exist or is corrupt: " << cal_file);
    }
    //check the file extension for xml or txt
    if (cal_file.find(xml) != std::string::npos) {
      DEBUG_MSG("Camera_System::read_calibration_file(): assuming calibration file is vic3D xml format");
      // cal.xml file can't be ready by Teuchos parser because it has a !DOCTYPE
      // have to manually read the file here, lots of assumptions in how the file is formatted
      // camera orientation for each camera in vic3d is in terms of the world to camera
      // orientation and the order of variables is alpha beta gamma tx ty tz (the Cardan Bryant angles + translations)
      // read each line of the file
      sys_type_ = VIC3D;
      std::vector<Camera::Camera_Info> camera_infos(2);
      int_t current_camera = 0;
      int_t xml_img_height = 0;
      int_t xml_img_width = 0;
      std::vector<int_t> param_order_int = { Camera::CX, Camera::CY, Camera::FX, Camera::FY, Camera::FS, Camera::K1, Camera::K2, Camera::K3 };
      while (!dataFile.eof()) {
        std::vector<std::string> tokens = tokenize_line(dataFile, " \t<>\"");
        if (tokens.size() == 0) continue;
        if(tokens[0]=="POLYGONMASK"){
          TEUCHOS_TEST_FOR_EXCEPTION(tokens[1]!="WIDTH=",std::runtime_error,"");
          TEUCHOS_TEST_FOR_EXCEPTION(tokens[3]!="HEIGHT=",std::runtime_error,"");
          xml_img_width = std::atoi(tokens[2].c_str());
          xml_img_height = std::atoi(tokens[4].c_str());
          continue;
        }
        if (tokens[0] != "CAMERA") continue;
        TEUCHOS_TEST_FOR_EXCEPTION(current_camera>1,std::runtime_error,"");// only allow 2 cameras
        const int_t camera_index = std::atoi(tokens[2].c_str());
        std::stringstream camera_title;
        camera_title << "CAMERA " << camera_index;
        camera_infos[current_camera].id_ = camera_title.str();
        DEBUG_MSG("Camera_System::read_calibration_file(): found " << camera_infos[current_camera].id_);
        TEUCHOS_TEST_FOR_EXCEPTION(camera_index >= 10,std::runtime_error,"");
        TEUCHOS_TEST_FOR_EXCEPTION(tokens.size() <= 18,std::runtime_error,"");
        //Store the intrinsic parameters
        for (int_t i = 0; i < (int_t)param_order_int.size(); ++i)
          camera_infos[current_camera].intrinsics_[param_order_int[i]] = strtod(tokens[i + 3].c_str(), NULL);
        camera_infos[current_camera].lens_distortion_model_ = Camera::K1R1_K2R2_K3R3;
        //Store the extrinsic parameters
        assert(tokens.size()>=18);
        TEUCHOS_TEST_FOR_EXCEPTION(tokens[11] != "ORIENTATION",std::runtime_error,"");
        const scalar_t alpha = strtod(tokens[12].c_str(), NULL);
        const scalar_t beta = strtod(tokens[13].c_str(), NULL);
        const scalar_t gamma = strtod(tokens[14].c_str(), NULL);
        camera_infos[current_camera].set_rotation_matrix(alpha,beta,gamma);
        camera_infos[current_camera].tx_ = strtod(tokens[15].c_str(), NULL);
        camera_infos[current_camera].ty_ = strtod(tokens[16].c_str(), NULL);
        camera_infos[current_camera].tz_ = strtod(tokens[17].c_str(), NULL);
        // the camera constructor will check if it is valid
        current_camera++;
        DEBUG_MSG("Camera_System::read_calibration_file(): successfully loaded VIC3D camera " << camera_index);
      } // end file read
      TEUCHOS_TEST_FOR_EXCEPTION(xml_img_height<=0||xml_img_width<=0,std::runtime_error,"");
      for(size_t i=0;i<camera_infos.size();++i){
        camera_infos[i].image_height_ = xml_img_height;
        camera_infos[i].image_width_ = xml_img_width;
        Teuchos::RCP<DICe::Camera> camera_ptr = Teuchos::rcp(new DICe::Camera(camera_infos[i]));
        cameras_.push_back(camera_ptr);
      }
      TEUCHOS_TEST_FOR_EXCEPTION(cameras_.size()!=2,std::runtime_error,"");
    }
    // old DICe text format, kept around so that the GUI generated files from long ago will still work without having to
    // re-run the calibrations
    else if (cal_file.find(txt) != std::string::npos) {
      DEBUG_MSG("Camera_System::read_calibration_file(): calibration file is generic txt format");
      //may want to modify this file format to allow more than 2 cameras in the future
      sys_type_ = GENERIC_SYSTEM;
      std::vector<int_t> param_order_int = { Camera::CX, Camera::CY, Camera::FX, Camera::FY, Camera::FS, Camera::K1, Camera::K2, Camera::K3 };
      // the extrinsics are in either an order of
      // R11 R12 R13 R21 R22 R23 R31 R32 R33 TX TY TZ or
      // alpha beta gamma TX TY TZ or
      // custom transforms are no longer supported from a text file format
      const int_t num_values_with_eulers = 24; // 8 intrinsics each for camera 0 and camera 1 + 3 extrinsic translations + the euler angles + img_height + img_width
      const int_t num_values_with_R = 30; // 8 intrinsics for each camera 0 and camera 1 + 3 extrinsic translations + the 9 rotation matrix components + img_height + img_width
      int_t total_num_values = 0;
      Camera::Camera_Info camera_info_0; // always two cameras per file in the txt format
      Camera::Camera_Info camera_info_1;
      camera_info_0.id_ = "CAMERA 0";
      camera_info_1.id_ = "CAMERA 1";
      //Run through the file to determine the format
      while (!dataFile.eof()){
        std::vector<std::string>tokens = tokenize_line(dataFile, " \t<>");
        if (tokens.size() == 0) continue;
        if (tokens[0] == "#") continue;
        if (tokens[0] == "TRANSFORM") {
          TEUCHOS_TEST_FOR_EXCEPTION(true,std::runtime_error,"Error, custom transforms are no longer supported in the txt calibration file format");
        }
        total_num_values++;
      }
      TEUCHOS_TEST_FOR_EXCEPTION(total_num_values!=num_values_with_eulers&&total_num_values!=num_values_with_R,std::runtime_error,
        "Error, invalid number of paramers in txt calibration file.\n"
        "    This is likely due to the text file format changing to now\n"
        "    require the image height and width to be specified in the file" << cal_file);
      const bool has_eulers = total_num_values==num_values_with_eulers;
      // return to start of file:
      dataFile.clear();
      dataFile.seekg(0, std::ios::beg);
      // default lens distortion model
      camera_info_0.lens_distortion_model_ = Camera::K1R1_K2R2_K3R3;
      camera_info_1.lens_distortion_model_ = Camera::K1R1_K2R2_K3R3;
      int_t current_line = 0;
      std::vector<scalar_t> ext_values(total_num_values - 18,0);
      int_t txt_img_height = -1;
      int_t txt_img_width = -1;
      while (!dataFile.eof())
      {
        std::vector<std::string> tokens = tokenize_line(dataFile, " \t<>");
        if (tokens.size() == 0) continue;
        if (tokens[0] == "#") continue;
        if (tokens.size() > 1) {
          assert(tokens[1] == "#"); // only one entry per line plus comments
        }
        if (current_line < 8)
          camera_info_0.intrinsics_[param_order_int[current_line]] = strtod(tokens[0].c_str(), NULL);
        else if (current_line < 16)
          camera_info_1.intrinsics_[param_order_int[current_line - 8]] = strtod(tokens[0].c_str(), NULL);
        else if (current_line < total_num_values - 2){
          assert(current_line - 16 < (int_t)ext_values.size());
          ext_values[current_line - 16] = strtod(tokens[0].c_str(), NULL);
        }
        else if (current_line < total_num_values -1){
          txt_img_height = strtod(tokens[0].c_str(), NULL);
        }
        else{
          txt_img_width = strtod(tokens[0].c_str(), NULL);
        }
        current_line++;
      }
      TEUCHOS_TEST_FOR_EXCEPTION(txt_img_height<0,std::runtime_error,"");
      TEUCHOS_TEST_FOR_EXCEPTION(txt_img_width<0,std::runtime_error,"");
      camera_info_1.tx_ = ext_values[ext_values.size()-3];
      camera_info_1.ty_ = ext_values[ext_values.size()-2];
      camera_info_1.tz_ = ext_values[ext_values.size()-1];
      if(has_eulers){
        const scalar_t alpha = ext_values[0];
        const scalar_t beta = ext_values[1];
        const scalar_t gamma = ext_values[2];
        camera_info_1.set_rotation_matrix(alpha,beta,gamma);
      }else{
        assert(ext_values.size()>=12);
        camera_info_1.rotation_matrix_(0,0) = ext_values[0];
        camera_info_1.rotation_matrix_(0,1) = ext_values[1];
        camera_info_1.rotation_matrix_(0,2) = ext_values[2];
        camera_info_1.rotation_matrix_(1,0) = ext_values[3];
        camera_info_1.rotation_matrix_(1,1) = ext_values[4];
        camera_info_1.rotation_matrix_(1,2) = ext_values[5];
        camera_info_1.rotation_matrix_(2,0) = ext_values[6];
        camera_info_1.rotation_matrix_(2,1) = ext_values[7];
        camera_info_1.rotation_matrix_(2,2) = ext_values[8];
      }
      // dummy image sizes
      camera_info_0.image_height_ = txt_img_height;
      camera_info_0.image_width_ = txt_img_width;
      // dummy image sizes
      camera_info_1.image_height_ = txt_img_height;
      camera_info_1.image_width_ = txt_img_width;
      DEBUG_MSG("image height: " << txt_img_height << " image width: " << txt_img_width);
      // the camera constructor will check if it is valid
      // create two cameras and push them into the vector
      Teuchos::RCP<DICe::Camera> camera_0_ptr = Teuchos::rcp(new DICe::Camera(camera_info_0));
      cameras_.push_back(camera_0_ptr);
      Teuchos::RCP<DICe::Camera> camera_1_ptr = Teuchos::rcp(new DICe::Camera(camera_info_1));
      cameras_.push_back(camera_1_ptr);
      DEBUG_MSG("Camera_System::read_calibration_file(): successfully loaded cameras from text file");
    }
    else {
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::runtime_error,
        "Error, unrecognized calibration parameters file format: " << cal_file);
    }
  }

  // display some information for debugging
  DEBUG_MSG("************************************************************************");
  DEBUG_MSG("System type: " << to_string(sys_type_));
  DEBUG_MSG("Number of Cams: " << num_cameras());
  DEBUG_MSG(" ");

  for (size_t i = 0; i < cameras_.size(); i++) {
    DEBUG_MSG("******************* CAMERA: " << i << " ******************************");
    DEBUG_MSG("Camera_System::read_calibration_file(): identifier: " << cameras_[i]->id());
    for (size_t j = 0; j < cameras_[i]->intrinsics()->size(); j++)
      DEBUG_MSG("Camera_System::read_calibration_file(): " << Camera::to_string(static_cast<Camera::Cam_Intrinsic_Param>(j)) << ": " << (*cameras_[i]->intrinsics())[j]);
    DEBUG_MSG("Camera_System::read_calibration_file(): lens distortion model: " << Camera::to_string(cameras_[i]->lens_distortion_model()));
    DEBUG_MSG("Camera_System::read_calibration_file(): tx: " << cameras_[i]->tx());
    DEBUG_MSG("Camera_System::read_calibration_file(): ty: " << cameras_[i]->ty());
    DEBUG_MSG("Camera_System::read_calibration_file(): tz: " << cameras_[i]->tz());
    DEBUG_MSG("Camera_System::read_calibration_file(): R11: " << (*cameras_[i]->rotation_matrix())(0,0));
    DEBUG_MSG("Camera_System::read_calibration_file(): R12: " << (*cameras_[i]->rotation_matrix())(0,1));
    DEBUG_MSG("Camera_System::read_calibration_file(): R13: " << (*cameras_[i]->rotation_matrix())(0,2));
    DEBUG_MSG("Camera_System::read_calibration_file(): R21: " << (*cameras_[i]->rotation_matrix())(1,0));
    DEBUG_MSG("Camera_System::read_calibration_file(): R22: " << (*cameras_[i]->rotation_matrix())(1,1));
    DEBUG_MSG("Camera_System::read_calibration_file(): R23: " << (*cameras_[i]->rotation_matrix())(1,2));
    DEBUG_MSG("Camera_System::read_calibration_file(): R31: " << (*cameras_[i]->rotation_matrix())(2,0));
    DEBUG_MSG("Camera_System::read_calibration_file(): R32: " << (*cameras_[i]->rotation_matrix())(2,1));
    DEBUG_MSG("Camera_System::read_calibration_file(): R33: " << (*cameras_[i]->rotation_matrix())(2,2));
    DEBUG_MSG("Camera_System::read_calibration_file(): image height: " << cameras_[i]->image_height());
    DEBUG_MSG("Camera_System::read_calibration_file(): image width: " << cameras_[i]->image_width());
    DEBUG_MSG("Camera_System::read_calibration_file(): pixel depth: " << cameras_[i]->pixel_depth());
    DEBUG_MSG("Camera_System::read_calibration_file(): lens: " << cameras_[i]->lens());
    DEBUG_MSG("Camera_System::read_calibration_file(): comments: " << cameras_[i]->comments());
    DEBUG_MSG(" ");
  }
  // 4x4 independent transformation
  if (has_4x4_transform_) {
    DEBUG_MSG("Camera_System::read_calibration_file(): 4x4 user transformation");
    for (int_t i = 0; i < 4; ++i)
      DEBUG_MSG("Camera_System::read_calibration_file(): " << user_4x4_trans_(i,0) <<
        " " << user_4x4_trans_(i,1) << " " << user_4x4_trans_(i,2) << " " << user_4x4_trans_(i,3));
  }
  // 6 param independent transformation
  if (has_6_transform_) {
    DEBUG_MSG("Camera_System::read_calibration_file(): 6 parameter user transformation");
    DEBUG_MSG("Camera_System::read_calibration_file(): " << user_6x1_trans_[0] <<
      " " << user_6x1_trans_[1] << " " << user_6x1_trans_[2] << " " << user_6x1_trans_[3] <<
      " " << user_6x1_trans_[4] << " " << user_6x1_trans_[5]);
  }
  DEBUG_MSG("Camera_System::read_calibration_file(): end");
}

void
Camera_System::write_calibration_file(const std::string & cal_file){
  DEBUG_MSG(" ");
  DEBUG_MSG("*****************************  write calibration file **************************");
  DEBUG_MSG("Camera_System::write_calibration_file(): output file: " << cal_file);
  TEUCHOS_TEST_FOR_EXCEPTION(sys_type_==UNKNOWN_SYSTEM,std::runtime_error,"write_calibration_file() called for unknown system type");
  std::stringstream param_title;
  std::stringstream param_val;
  std::stringstream valid_fields;

  // clear the files if they exist
  initialize_xml_file(cal_file);

  // write the header
  DEBUG_MSG("Camera_System::write_calibration_file(): writing header");
  write_xml_comment(cal_file, "DICe formatted calibration file");
  write_xml_comment(cal_file, "DICe_XML_Calibration_File parameter with a value of true "
      "denotes that this file is a DICe XML formatted calibration file");
  write_xml_bool_param(cal_file, DICe_XML_Calibration_File, "true", false);

  // system type
  valid_fields=std::stringstream();
  valid_fields << "type of 3D system valid field values are: ";
  for (int_t n = 1; n < MAX_SYSTEM_TYPE_3D; n++) valid_fields << " " << to_string(static_cast<System_Type_3D>(n));
  write_xml_comment(cal_file, valid_fields.str());
  write_xml_string_param(cal_file, system_type_3D, to_string(sys_type_), false);

  // camera intrinsic parameters
  write_xml_comment(cal_file, "camera intrinsic parameters (zero valued parameters don't need to be specified)");
  write_xml_comment(cal_file, "the file supports up to (max_num_cameras_allowed_) cameras, 0...max_num");
  write_xml_comment(cal_file, "each camera is a seperate sublist of parameters");
  write_xml_comment(cal_file, "the sublist must be named CAMERA <#> with # the integer id of the camera starting at 0");
  valid_fields = std::stringstream();
  valid_fields << "valid camera intrinsic parameter field names are: ";
  for (int_t n = 0; n < Camera::MAX_CAM_INTRINSIC_PARAM; n++) valid_fields << " " << Camera::to_string(static_cast<Camera::Cam_Intrinsic_Param>(n));
  write_xml_comment(cal_file, valid_fields.str());
  write_xml_comment(cal_file, "CX,CY-image center (pix), FX,FY-pin hole distances (pix), FS-skew (deg)");
  write_xml_comment(cal_file, "K1-K6-lens distortion coefficients, P1-P2-tangential distortion(openCV), S1-S4 thin prism distortion(openCV), T1,T2-Scheimpfug correction (openCV)");
  write_xml_comment(cal_file, "be aware that openCV gives the values in the following order: (K1,K2,P1,P2[,K3[,K4,K5,K6[,S1,S2,S3,S4[,TX,TY]]]])");
  valid_fields = std::stringstream();
  valid_fields << "valid values for the LENS_DISTORTION_MODEL are: ";
  //for (int_t n = 0; n < Camera::MAX_LENS_DISTORTION_MODEL; n++) valid_fields << " " << Camera::to_string(static_cast<Camera::Lens_Distortion_Model>(n));
  write_xml_comment(cal_file, valid_fields.str());
  write_xml_comment(cal_file, "NONE no distortion model");
  write_xml_comment(cal_file, "OPENCV_DIS uses the model defined in openCV 3.4.1");
  write_xml_comment(cal_file, "VIC3D_DIS uses the model defined for VIC3D");
  write_xml_comment(cal_file, "K1R1_K2R2_K3R3 -> K1*R + K2*R^2 + K3*R^3");
  write_xml_comment(cal_file, "K1R2_K2R4_K3R6 -> K1*R^2 + K2*R^4 + K3*R^6");
  write_xml_comment(cal_file, "K1R3_K2R5_K3R7 -> K1*R^3 + K2*R^5 + K3*R^7");

  // extrinsic parameters
  write_xml_comment(cal_file, "camera extrinsic parameters (zero valued parameters don't need to be specified)");
  write_xml_comment(cal_file, "extrinsic translations TX TY and TZ can be specified as separate parameters");
  write_xml_comment(cal_file, "extrinsic rotations can be specified through a rotation matrix R, or the three euler angles, but not both");
  write_xml_comment(cal_file, "if no matrix or euler angles are given the rotation matrix is set to the identity matrix");
  valid_fields = std::stringstream();
  valid_fields << "valid camera extrinsic parameter field names are: ";
  valid_fields << "TX TY TZ and \n";
  valid_fields << "the eulers: ALPHA BETA GAMMA or a rotation matrix:\n";
  valid_fields << "<ParameterList name=\"rotation_3x3_matrix\">\n";
  valid_fields << "<Parameter name=\"ROW 0\" type=\"string\" value=\"{R11,R12,R13}\" />\n";
  valid_fields << "<Parameter name=\"ROW 1\" type=\"string\" value=\"{R21,R22,R23}\" />\n";
  valid_fields << "<Parameter name=\"ROW 2\" type=\"string\" value=\"{R31,R32,R33}\" />\n";
  valid_fields << "</ParameterList>\n";

  write_xml_comment(cal_file, "additional camera fields:");
  write_xml_comment(cal_file, "CAMERA_ID: unique camera descripter, if not supplied CAMERA {#} is used");
  write_xml_comment(cal_file, "IMAGE_HEIGHT_WIDTH {h, w}");
  write_xml_comment(cal_file, "PIXEL_DEPTH");
  write_xml_comment(cal_file, "LENS");
  write_xml_comment(cal_file, "COMMENTS");
  write_xml_comment(cal_file, "any parameter with a value of 0 may simply be omitted from the calibration file");

  for (size_t camera_index = 0; camera_index < cameras_.size(); camera_index++) {
    param_title = std::stringstream();
    param_title << "CAMERA " << camera_index;
    DEBUG_MSG("Camera_System::write_calibration_file(): writing camera parameters:" << param_title.str());
    write_xml_param_list_open(cal_file, param_title.str(), false);

    param_title = std::stringstream();
    param_title << "CAMERA_ID";
    param_val = std::stringstream();
    param_val << cameras_[camera_index]->id();
    write_xml_string_param(cal_file, param_title.str(), param_val.str(), false);

    std::vector<scalar_t> & intrinsics = *cameras_[camera_index]->intrinsics();
    for (int_t j = 0; j < Camera::MAX_CAM_INTRINSIC_PARAM; j++) {
      if (intrinsics[j] != 0) {
        param_val = std::stringstream();
        param_val << intrinsics[j];
        write_xml_real_param(cal_file, Camera::to_string(static_cast<Camera::Cam_Intrinsic_Param>(j)), param_val.str(), false);
      }
    }
    write_xml_string_param(cal_file, "LENS_DISTORTION_MODEL", Camera::to_string(cameras_[camera_index]->lens_distortion_model()), false);
    DEBUG_MSG("Camera_System::write_calibration_file(): writing the extrinsic translations");
    if(cameras_[camera_index]->tx()!=0){
      param_val = std::stringstream();
      param_val << cameras_[camera_index]->tx();
      write_xml_real_param(cal_file, "TX", param_val.str(), false);
    }
    if(cameras_[camera_index]->ty()!=0){
      param_val = std::stringstream();
      param_val << cameras_[camera_index]->ty();
      write_xml_real_param(cal_file, "TY", param_val.str(), false);
    }
    if(cameras_[camera_index]->tz()!=0){
      param_val = std::stringstream();
      param_val << cameras_[camera_index]->tz();
      write_xml_real_param(cal_file, "TZ", param_val.str(), false);
    }
    // always write out the rotation matrix (euler angles aren't saved)
    DEBUG_MSG("Camera_System::write_calibration_file(): writing 3x3 rotation matrix");
    write_xml_comment(cal_file, "3x3 camera rotation matrix (world to cam transformation)");
    write_xml_comment(cal_file, "this is a 3x3 matrix that combined with TX, TY and TZ transform world coodinates to this camera's coordinates");
    write_xml_param_list_open(cal_file, rotation_3x3_matrix, false);
    for (size_t i = 0; i < 3; i++) {
      param_val = std::stringstream();
      param_title = std::stringstream();
      param_title << "ROW " << i;
      param_val << "{ ";
      for (size_t j = 0; j < 2; j++)
        param_val << (*cameras_[camera_index]->rotation_matrix())(i,j) << ", ";
      param_val << (*cameras_[camera_index]->rotation_matrix())(i,2) << " }";
      write_xml_string_param(cal_file, param_title.str(), param_val.str(), false);
      if (i==0) write_xml_comment(cal_file, "R11 R12 R13");
      if (i==1) write_xml_comment(cal_file, "R21 R22 R23");
      if (i==2) write_xml_comment(cal_file, "R31 R32 R33");
    }
    write_xml_param_list_close(cal_file, false);

    const int_t img_width = cameras_[camera_index]->image_width();
    const int_t img_height = cameras_[camera_index]->image_height();
    param_title = std::stringstream();
    param_val = std::stringstream();
    if ((img_width != 0 && img_height != 0)) {
      param_title << "IMAGE_HEIGHT_WIDTH";
      param_val << "{ " << img_height << ", " << img_width << " }";
      write_xml_string_param(cal_file, param_title.str(), param_val.str(), false);
    }

    const int_t pixel_depth = cameras_[camera_index]->pixel_depth();
    param_title = std::stringstream();
    param_val = std::stringstream();
    if (pixel_depth != 0) {
      param_title << "PIXEL_DEPTH";
      param_val << pixel_depth;
      write_xml_size_param(cal_file, param_title.str(), param_val.str(), false);
    }

    param_title = std::stringstream();
    param_val = std::stringstream();
    param_val << cameras_[camera_index]->lens();
    if (param_val.str() != "") {
      param_title << "LENS";
      write_xml_string_param(cal_file, param_title.str(), param_val.str(), false);
    }

    param_title = std::stringstream();
    param_val = std::stringstream();
    param_val << cameras_[camera_index]->comments();
    if (param_val.str() != "") {
      param_title << "COMMENTS";
      write_xml_string_param(cal_file, param_title.str(), param_val.str(), false);
    }

    write_xml_param_list_close(cal_file, false);
  }

  if (has_6_transform_) {
    DEBUG_MSG("Camera_System::write_calibration_file(): writing user supplied 6 parameter transform");
    write_xml_comment(cal_file, "user supplied 6 parameter transform");
    write_xml_comment(cal_file, "this is a user supplied transform with 6 parameters seperate from the transforms determined by the camera parameters");
    write_xml_comment(cal_file, "can used for non-projection transformations between images - optional");
    param_val = std::stringstream();
    param_val << "{ ";
    for (int_t j = 0; j < 5; j++)
      param_val << user_6x1_trans_[j] << ", ";
    param_val << user_6x1_trans_[5] << " }";
    write_xml_string_param(cal_file, user_6_param_transform, param_val.str(), false);
  }

  if (has_4x4_transform_) {
    DEBUG_MSG("Camera_System::write_calibration_file(): writing user supplied 4x4 parameter transform");
    write_xml_comment(cal_file, "user supplied 4x4 parameter transform");
    write_xml_comment(cal_file, "this is a user supplied 4x4 array transform seperate from the transforms determined by the camera parameters");
    write_xml_comment(cal_file, "typically includes a combined rotation and translation array  - optional");
    write_xml_param_list_open(cal_file, user_4x4_param_transform, false);
    for (int_t i = 0; i < 4; i++) {
      param_val = std::stringstream();
      param_title = std::stringstream();
      param_title << "ROW " << i;
      param_val << "{ ";
      for (int_t j = 0; j < 3; j++)
        param_val << user_4x4_trans_(i,j) << ", ";
      param_val << user_4x4_trans_(i,3) << " }";
      write_xml_string_param(cal_file, param_title.str(), param_val.str(), false);
      if (i == 0) write_xml_comment(cal_file, "R11 R12 R13 TX");
      if (i == 1) write_xml_comment(cal_file, "R21 R22 R23 TY");
      if (i == 2) write_xml_comment(cal_file, "R31 R32 R33 TZ");
      if (i == 3) write_xml_comment(cal_file, "0   0   0   1");
    }
    write_xml_param_list_close(cal_file, false);
  }

//  if (has_opencv_rot_trans_ || all_fields) {
//    DEBUG_MSG("Camera_System::write_calibration_file(): writing openCV rotation matrix");
//    write_xml_comment(cal_file, "openCV rotation translation matrix from from cam0 to cam 1");
//    if (all_fields) write_xml_comment(cal_file, "this is the [R|t] rotation translation matrix from openCV");
//    if (all_fields) write_xml_comment(cal_file, "this is the basic output from openCV stereo calibration for R and t");
//    write_xml_param_list_open(cal_file, opencv_3x4_param_transform, false);
//    for (int_t i = 0; i < 3; i++) {
//      param_val = std::stringstream();
//      param_title = std::stringstream();
//      param_title << "ROW " << i;
//      param_val << "{ ";
//      for (int_t j = 0; j < 3; j++)
//        param_val << opencv_3x4_trans_(i,j) << ", ";
//      param_val << opencv_3x4_trans_(i,3) << "}";
//      write_xml_string_param(cal_file, param_title.str(), param_val.str(), false);
//      if (all_fields && i == 0) write_xml_comment(cal_file, "R11 R12 R13 TX");
//      if (all_fields && i == 1) write_xml_comment(cal_file, "R21 R22 R23 TY");
//      if (all_fields && i == 2) write_xml_comment(cal_file, "R31 R32 R33 TZ");
//    }
//    write_xml_param_list_close(cal_file, false);
//  }
  finalize_xml_file(cal_file);
}

void
Camera_System::camera_to_camera_projection(
  const size_t source_id,
  const size_t target_id,
  const std::vector<scalar_t> & img_source_x,
  const std::vector<scalar_t> & img_source_y,
  std::vector<scalar_t> & img_target_x,
  std::vector<scalar_t> & img_target_y,
  const std::vector<scalar_t> & params,
  std::vector<std::vector<scalar_t> > & img_target_dx,
  std::vector<std::vector<scalar_t> > & img_target_dy,
  const std::vector<scalar_t> & rigid_body_params) {
  TEUCHOS_TEST_FOR_EXCEPTION(params.size()!=3,std::runtime_error,"");
  TEUCHOS_TEST_FOR_EXCEPTION(source_id<0||source_id>=num_cameras(),std::runtime_error,"invalid source id");
  //TEUCHOS_TEST_FOR_EXCEPTION(source_id==target_id,std::runtime_error,"source and target id are the same");
  TEUCHOS_TEST_FOR_EXCEPTION(target_id<0||target_id>=num_cameras(),std::runtime_error,"invalid target id");
  const size_t vec_size = img_source_x.size();
  TEUCHOS_TEST_FOR_EXCEPTION(img_source_y.size()!=vec_size,std::runtime_error,"");
  TEUCHOS_TEST_FOR_EXCEPTION(img_target_x.size()!=vec_size,std::runtime_error,"");
  TEUCHOS_TEST_FOR_EXCEPTION(img_target_y.size()!=vec_size,std::runtime_error,"");
  const size_t num_params = img_target_dx.size();
  const bool has_derivatives = num_params>0;
  const bool has_rigid_body = rigid_body_params.size()>0;
  if(has_derivatives){
    if(has_rigid_body){
      TEUCHOS_TEST_FOR_EXCEPTION(num_params!=6,std::runtime_error,"");
    }else{
      TEUCHOS_TEST_FOR_EXCEPTION(num_params!=3,std::runtime_error,"");
    }
    TEUCHOS_TEST_FOR_EXCEPTION(img_target_dy.size()!=num_params,std::runtime_error,"");
    for(size_t i=0;i<num_params;++i){
      TEUCHOS_TEST_FOR_EXCEPTION(img_target_dx[i].size()!=vec_size,std::runtime_error,"");
      TEUCHOS_TEST_FOR_EXCEPTION(img_target_dy[i].size()!=vec_size,std::runtime_error,"");
    }
  }
  if(has_rigid_body){
    TEUCHOS_TEST_FOR_EXCEPTION(rigid_body_params.size()!=6,std::runtime_error,"invalid rigid body parameter vector size");
  }
  // temporary vectors for traversing the projections
  std::vector<scalar_t> tmp_sensor_x(vec_size,0);
  std::vector<scalar_t> tmp_sensor_y(vec_size,0);
  std::vector<scalar_t> tmp_cam_x(vec_size,0);
  std::vector<scalar_t> tmp_cam_y(vec_size,0);
  std::vector<scalar_t> tmp_cam_z(vec_size,0);
  std::vector<scalar_t> tmp_world_x(vec_size,0);
  std::vector<scalar_t> tmp_world_y(vec_size,0);
  std::vector<scalar_t> tmp_world_z(vec_size,0);
  std::vector<scalar_t> tmp_rb_world_x(vec_size,0);
  std::vector<scalar_t> tmp_rb_world_y(vec_size,0);
  std::vector<scalar_t> tmp_rb_world_z(vec_size,0);
  std::vector<std::vector<scalar_t> > tmp_cam_dx, tmp_dx;
  std::vector<std::vector<scalar_t> > tmp_cam_dy, tmp_dy;
  std::vector<std::vector<scalar_t> > tmp_cam_dz, tmp_dz;
  if(has_derivatives){
    // these vectors either have 3 or 6 rows depending on if this is a rigid body motion projection or projection shape function based
    tmp_cam_dx = std::vector<std::vector<scalar_t> >(num_params,std::vector<scalar_t>(vec_size,0));
    tmp_cam_dy = std::vector<std::vector<scalar_t> >(num_params,std::vector<scalar_t>(vec_size,0));
    tmp_cam_dz = std::vector<std::vector<scalar_t> >(num_params,std::vector<scalar_t>(vec_size,0));
    tmp_dx = std::vector<std::vector<scalar_t> >(num_params,std::vector<scalar_t>(vec_size,0));
    tmp_dy = std::vector<std::vector<scalar_t> >(num_params,std::vector<scalar_t>(vec_size,0));
    tmp_dz = std::vector<std::vector<scalar_t> >(num_params,std::vector<scalar_t>(vec_size,0));
  }

  // traverse the projections from image to world for the source camera ...
  cameras_[source_id]->image_to_sensor(img_source_x,img_source_y,tmp_sensor_x,tmp_sensor_y);
  if(has_derivatives){
    if(has_rigid_body){
      cameras_[source_id]->sensor_to_cam(tmp_sensor_x,tmp_sensor_y,tmp_cam_x,tmp_cam_y,tmp_cam_z,params);
      cameras_[source_id]->cam_to_world(tmp_cam_x,tmp_cam_y,tmp_cam_z,tmp_rb_world_x,tmp_rb_world_y,tmp_rb_world_z);
      // no derivatives come into play until the rotation and translation from the next call
      rot_trans_3D(tmp_rb_world_x,tmp_rb_world_y,tmp_rb_world_z,tmp_world_x,tmp_world_y,tmp_world_z,rigid_body_params,
        tmp_dx,tmp_dy,tmp_dz);
    }else{
      cameras_[source_id]->sensor_to_cam(tmp_sensor_x,tmp_sensor_y,tmp_cam_x,tmp_cam_y,tmp_cam_z,params,tmp_cam_dx,tmp_cam_dy,tmp_cam_dz);
      cameras_[source_id]->cam_to_world(tmp_cam_x,tmp_cam_y,tmp_cam_z,tmp_world_x,tmp_world_y,tmp_world_z,
        tmp_cam_dx,tmp_cam_dy,tmp_cam_dz,tmp_dx,tmp_dy,tmp_dz);
    }
    // traverse back through the projections from world source camera to image in the target camera
    cameras_[target_id]->world_to_cam(tmp_world_x,tmp_world_y,tmp_world_z,tmp_cam_x,tmp_cam_y,tmp_cam_z,
      tmp_dx,tmp_dy,tmp_dz,tmp_cam_dx,tmp_cam_dy,tmp_cam_dz);
    cameras_[target_id]->cam_to_sensor(tmp_cam_x,tmp_cam_y,tmp_cam_z,tmp_sensor_x,tmp_sensor_y,tmp_cam_dx,tmp_cam_dy,tmp_cam_dz,tmp_dx,tmp_dy);
    cameras_[target_id]->sensor_to_image(tmp_sensor_x,tmp_sensor_y,img_target_x,img_target_y,tmp_dx,tmp_dy,img_target_dx,img_target_dy);
  }else{
    cameras_[source_id]->sensor_to_cam(tmp_sensor_x,tmp_sensor_y,tmp_cam_x,tmp_cam_y,tmp_cam_z,params);
    if(has_rigid_body){
      cameras_[source_id]->cam_to_world(tmp_cam_x,tmp_cam_y,tmp_cam_z,tmp_rb_world_x,tmp_rb_world_y,tmp_rb_world_z);
      rot_trans_3D(tmp_rb_world_x,tmp_rb_world_y,tmp_rb_world_z,tmp_world_x,tmp_world_y,tmp_world_z,rigid_body_params);
    }else{
      cameras_[source_id]->cam_to_world(tmp_cam_x,tmp_cam_y,tmp_cam_z,tmp_world_x,tmp_world_y,tmp_world_z);
    }
    // traverse back through the projections from world source camera to image in the target camera
    cameras_[target_id]->world_to_cam(tmp_world_x,tmp_world_y,tmp_world_z,tmp_cam_x,tmp_cam_y,tmp_cam_z);
    cameras_[target_id]->cam_to_sensor(tmp_cam_x,tmp_cam_y,tmp_cam_z,tmp_sensor_x,tmp_sensor_y);
    cameras_[target_id]->sensor_to_image(tmp_sensor_x,tmp_sensor_y,img_target_x,img_target_y);
  }
}

void
Camera_System::initialize_rot_trans_3D(const std::vector<scalar_t> & rigid_body_params,
  const bool partials) {
  //if this is the first call to pre_rot_trans_3D assign the vectors
  if (rot_trans_3D_x_.size() != 4) {
    rot_trans_3D_x_.assign(4, 0.0);
    rot_trans_3D_y_.assign(4, 0.0);
    rot_trans_3D_z_.assign(4, 0.0);
    for (int_t i = 0; i < 6; i++) {
      rot_trans_3D_dx_[i].assign(4, 0.0);
      rot_trans_3D_dy_[i].assign(4, 0.0);
      rot_trans_3D_dz_[i].assign(4, 0.0);
    }
  }
  TEUCHOS_TEST_FOR_EXCEPTION(rigid_body_params.size()!=6,std::runtime_error,"");
  scalar_t cx, cy, cz, sx, sy, sz, tx, ty, tz;
  cx = cos(rigid_body_params[ANGLE_X]);
  cy = cos(rigid_body_params[ANGLE_Y]);
  cz = cos(rigid_body_params[ANGLE_Z]);
  sx = sin(rigid_body_params[ANGLE_X]);
  sy = sin(rigid_body_params[ANGLE_Y]);
  sz = sin(rigid_body_params[ANGLE_Z]);
  tx = rigid_body_params[TRANSLATION_X];
  ty = rigid_body_params[TRANSLATION_Y];
  tz = rigid_body_params[TRANSLATION_Z];

  rot_trans_3D_x_[0] = cy * cz;
  rot_trans_3D_x_[1] = sx * sy * cz - cx * sz;
  rot_trans_3D_x_[2] = cx * sy * cz + sx * sz;
  rot_trans_3D_x_[3] = tx;
  rot_trans_3D_y_[0] = cy * sz;
  rot_trans_3D_y_[1] = sx * sy * sz + cx * cz;
  rot_trans_3D_y_[2] = cx * sy * sz - sx * cz;
  rot_trans_3D_y_[3] = ty;
  rot_trans_3D_z_[0] = -sy;
  rot_trans_3D_z_[1] = sx * cy;
  rot_trans_3D_z_[2] = cx * cy;
  rot_trans_3D_z_[3] = tz;

  if (partials) {
    rot_trans_3D_dx_[ANGLE_X][0] = 0;
    rot_trans_3D_dx_[ANGLE_X][1] = cx * sy * cz + sx * sz;
    rot_trans_3D_dx_[ANGLE_X][2] = -sx * sy * cz + cx * sz;
    rot_trans_3D_dx_[ANGLE_X][3] = 0;
    rot_trans_3D_dy_[ANGLE_X][0] = 0;
    rot_trans_3D_dy_[ANGLE_X][1] = cx * sy * sz - sx * cz;
    rot_trans_3D_dy_[ANGLE_X][2] = - sx * sy * sz - cx * cz;
    rot_trans_3D_dy_[ANGLE_X][3] = 0;
    rot_trans_3D_dz_[ANGLE_X][0] = 0;
    rot_trans_3D_dz_[ANGLE_X][1] = cx * cy;
    rot_trans_3D_dz_[ANGLE_X][2] = -sx * cy;
    rot_trans_3D_dz_[ANGLE_X][3] = 0;

    rot_trans_3D_dx_[ANGLE_Y][0] = -sy * cz;
    rot_trans_3D_dx_[ANGLE_Y][1] = sx * cy * cz;
    rot_trans_3D_dx_[ANGLE_Y][2] = cx * cy * cz;
    rot_trans_3D_dx_[ANGLE_Y][3] = 0;
    rot_trans_3D_dy_[ANGLE_Y][0] = -sy * sz;
    rot_trans_3D_dy_[ANGLE_Y][1] = sx * cy * sz;
    rot_trans_3D_dy_[ANGLE_Y][2] = cx * cy * sz;
    rot_trans_3D_dy_[ANGLE_Y][3] = 0;
    rot_trans_3D_dz_[ANGLE_Y][0] = -cy;
    rot_trans_3D_dz_[ANGLE_Y][1] = -sx * sy;
    rot_trans_3D_dz_[ANGLE_Y][2] = -cx * sy;
    rot_trans_3D_dz_[ANGLE_Y][3] = 0;

    rot_trans_3D_dx_[ANGLE_Z][0] = -cy * sz;
    rot_trans_3D_dx_[ANGLE_Z][1] = -sx * sy * sz - cx * cz;
    rot_trans_3D_dx_[ANGLE_Z][2] = -cx * sy * sz + sx * cz;
    rot_trans_3D_dx_[ANGLE_Z][3] = 0;
    rot_trans_3D_dy_[ANGLE_Z][0] = cy * cz;
    rot_trans_3D_dy_[ANGLE_Z][1] = sx * sy * cz - cx * sz;
    rot_trans_3D_dy_[ANGLE_Z][2] = cx * sy * cz + sx * sz;
    rot_trans_3D_dy_[ANGLE_Z][3] = 0;
    rot_trans_3D_dz_[ANGLE_Z][0] = 0;
    rot_trans_3D_dz_[ANGLE_Z][1] = 0;
    rot_trans_3D_dz_[ANGLE_Z][2] = 0;
    rot_trans_3D_dz_[ANGLE_Z][3] = 0;
  }
}

void
Camera_System::rot_trans_3D(const std::vector<scalar_t> & source_x,
  const std::vector<scalar_t> & source_y,
  const std::vector<scalar_t> & source_z,
  std::vector<scalar_t> & target_x,
  std::vector<scalar_t> & target_y,
  std::vector<scalar_t> & target_z,
  const std::vector<scalar_t> & params,
  std::vector < std::vector<scalar_t> > & target_dx,
  std::vector < std::vector<scalar_t> > & target_dy,
  std::vector < std::vector<scalar_t> > & target_dz) {
  TEUCHOS_TEST_FOR_EXCEPTION(source_x.size()!=0,std::runtime_error,"");
  const size_t vec_size = source_x.size();
  TEUCHOS_TEST_FOR_EXCEPTION(source_y.size()!=vec_size,std::runtime_error,"");
  TEUCHOS_TEST_FOR_EXCEPTION(source_z.size()!=vec_size,std::runtime_error,"");
  TEUCHOS_TEST_FOR_EXCEPTION(target_x.size()!=vec_size,std::runtime_error,"");
  TEUCHOS_TEST_FOR_EXCEPTION(target_y.size()!=vec_size,std::runtime_error,"");
  TEUCHOS_TEST_FOR_EXCEPTION(target_z.size()!=vec_size,std::runtime_error,"");
  const bool has_derivatives = target_dx.size()>0;
  if(has_derivatives){
    // since this method is meant for the rigid body motions which have 6 parameters,
    // enforce that here to prevent this method being called with the shape function parameters (which has 3)
    TEUCHOS_TEST_FOR_EXCEPTION(target_dx.size()!=6,std::runtime_error,"");
    TEUCHOS_TEST_FOR_EXCEPTION(target_dy.size()!=6,std::runtime_error,"");
    TEUCHOS_TEST_FOR_EXCEPTION(target_dz.size()!=6,std::runtime_error,"");
    for(size_t i=0;i<target_dx.size();++i){
      TEUCHOS_TEST_FOR_EXCEPTION(target_dx[i].size()!=vec_size,std::runtime_error,"");
      TEUCHOS_TEST_FOR_EXCEPTION(target_dy[i].size()!=vec_size,std::runtime_error,"");
      TEUCHOS_TEST_FOR_EXCEPTION(target_dz[i].size()!=vec_size,std::runtime_error,"");
    }
  }
  // this transformation assumes all shape function related partials coming into the function are 0
  // prep the rotation coefficients
  initialize_rot_trans_3D(params, has_derivatives);
  // transform the coordinates
  for (size_t i = 0; i < vec_size; i++) {
    target_x[i] = rot_trans_3D_x_[0] * source_x[i] + rot_trans_3D_x_[1] * source_y[i] + rot_trans_3D_x_[2] * source_z[i] + rot_trans_3D_x_[3];
    target_y[i] = rot_trans_3D_y_[0] * source_x[i] + rot_trans_3D_y_[1] * source_y[i] + rot_trans_3D_y_[2] * source_z[i] + rot_trans_3D_y_[3];
    target_z[i] = rot_trans_3D_z_[0] * source_x[i] + rot_trans_3D_z_[1] * source_y[i] + rot_trans_3D_z_[2] * source_z[i] + rot_trans_3D_z_[3];
  }
  if(has_derivatives){
    //calculate the partials
    for (size_t j = 0; j < 3; j++) {
      for (size_t i = 0; i < vec_size; i++) {
        target_dx[j][i] = rot_trans_3D_dx_[j][0] * source_x[i] + rot_trans_3D_dx_[j][1] * source_y[i] + rot_trans_3D_dx_[j][2] * source_z[i];
        target_dy[j][i] = rot_trans_3D_dy_[j][0] * source_x[i] + rot_trans_3D_dy_[j][1] * source_y[i] + rot_trans_3D_dy_[j][2] * source_z[i];
        target_dz[j][i] = rot_trans_3D_dz_[j][0] * source_x[i] + rot_trans_3D_dz_[j][1] * source_y[i] + rot_trans_3D_dz_[j][2] * source_z[i];
      }
      std::fill(target_dx[j+3].begin(),target_dx[j+3].end(),0);
      std::fill(target_dy[j+3].begin(),target_dy[j+3].end(),0);
      std::fill(target_dz[j+3].begin(),target_dz[j+3].end(),0);
    }
    std::fill(target_dx[3].begin(),target_dx[3].end(),1.0);
    std::fill(target_dy[4].begin(),target_dy[4].end(),1.0);
    std::fill(target_dz[5].begin(),target_dz[5].end(),1.0);
  }
}

//void
//Camera_System::rot_trans_3D(const std::vector<scalar_t> & wld0_x,
//  const std::vector<scalar_t> & wld0_y,
//  const std::vector<scalar_t> & wld0_z,
//  std::vector<scalar_t> & wld1_x,
//  std::vector<scalar_t> & wld1_y,
//  std::vector<scalar_t> & wld1_z,
//  const std::vector<scalar_t> & params,
//  const std::vector < std::vector<scalar_t> > & wld0_dx,
//  const std::vector < std::vector<scalar_t> > & wld0_dy,
//  const std::vector < std::vector<scalar_t> > & wld0_dz,
//  std::vector < std::vector<scalar_t> > & wld1_dx,
//  std::vector < std::vector<scalar_t> > & wld1_dy,
//  std::vector < std::vector<scalar_t> > & wld1_dz) {
//  //This transformation assumes non zero incoming partials order is Zp, Theata, Phi, Ang X, Ang Y, Ang Z
//  //prep the rotation coefficients
//  initialize_rot_trans_3D(params, true);
//  //transform the coordinates
//  for (int_t i = 0; i < (int_t)wld0_x.size(); i++) {
//    wld1_x[i] = rot_trans_3D_x_[0] * wld0_x[i] + rot_trans_3D_x_[1] * wld0_y[i] + rot_trans_3D_x_[2] * wld0_z[i] + rot_trans_3D_x_[3];
//    wld1_y[i] = rot_trans_3D_y_[0] * wld0_x[i] + rot_trans_3D_y_[1] * wld0_y[i] + rot_trans_3D_y_[2] * wld0_z[i] + rot_trans_3D_y_[3];
//    wld1_z[i] = rot_trans_3D_z_[0] * wld0_x[i] + rot_trans_3D_z_[1] * wld0_y[i] + rot_trans_3D_z_[2] * wld0_z[i] + rot_trans_3D_z_[3];
//  }
//  //calculate the partials
//  for (int_t j = 0; j < 3; j++) {
//    for (int_t i = 0; i < (int_t)wld0_x.size(); i++) {
//      wld1_dx[j][i] = rot_trans_3D_x_[0] * wld0_dx[j][i] + rot_trans_3D_x_[1] * wld0_dy[j][i] + rot_trans_3D_x_[2] * wld0_dz[j][i];
//      wld1_dy[j][i] = rot_trans_3D_y_[0] * wld0_dx[j][i] + rot_trans_3D_y_[1] * wld0_dy[j][i] + rot_trans_3D_y_[2] * wld0_dz[j][i];
//      wld1_dz[j][i] = rot_trans_3D_z_[0] * wld0_dx[j][i] + rot_trans_3D_z_[1] * wld0_dy[j][i] + rot_trans_3D_z_[2] * wld0_dz[j][i];
//      wld1_dx[j+3][i] = rot_trans_3D_dx_[j][0] * wld0_x[i] + rot_trans_3D_dx_[j][1] * wld0_y[i] + rot_trans_3D_dx_[j][2] * wld0_z[i];
//      wld1_dy[j+3][i] = rot_trans_3D_dy_[j][0] * wld0_x[i] + rot_trans_3D_dy_[j][1] * wld0_y[i] + rot_trans_3D_dy_[j][2] * wld0_z[i];
//      wld1_dz[j+3][i] = rot_trans_3D_dz_[j][0] * wld0_x[i] + rot_trans_3D_dz_[j][1] * wld0_y[i] + rot_trans_3D_dz_[j][2] * wld0_z[i];
//    }
//    wld1_dx[j + 6].assign(wld0_x.size(), 0.0);
//    wld1_dy[j + 6].assign(wld0_x.size(), 0.0);
//    wld1_dz[j + 6].assign(wld0_x.size(), 0.0);
//  }
//  wld1_dx[6].assign(wld0_x.size(), 1.0);
//  wld1_dy[7].assign(wld0_x.size(), 1.0);
//  wld1_dz[8].assign(wld0_x.size(), 1.0);
//}

} //end DICe namespace

