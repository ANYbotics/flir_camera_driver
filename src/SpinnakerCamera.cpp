/*
This code was developed by the National Robotics Engineering Center (NREC), part of the Robotics Institute at Carnegie
Mellon University.
Its development was funded by DARPA under the LS3 program and submitted for public release on June 7th, 2012.
Release was granted on August, 21st 2012 with Distribution Statement "A" (Approved for Public Release, Distribution
Unlimited).

This software is released under a BSD license:

Copyright (c) 2012, Carnegie Mellon University. All rights reserved.
Copyright (c) 2018, Clearpath Robotics, Inc., All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

Redistributions of source code must retain the above copyright notice, this list of conditions and the following
disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided with the distribution.
Neither the name of the Carnegie Mellon University nor the names of its contributors may be used to endorse or promote
products derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*-*-C++-*-*/
/**
   @file SpinnakerCamera.cpp
   @author Chad Rockey
   @date July 11, 2011
   @brief Interface to Point Grey cameras

   @attention Copyright (C) 2011
   @attention National Robotics Engineering Center
   @attention Carnegie Mellon University
*/

#include "any_spinnaker_camera_driver/SpinnakerCamera.h"

#include <iostream>
#include <sstream>
#include <typeinfo>
#include <string>

#include <ros/ros.h>

namespace any_spinnaker_camera_driver
{
SpinnakerCamera::SpinnakerCamera()
  : serial_(0)
  , system_(Spinnaker::System::GetInstance())
  , camList_(system_->GetCameras())
  , pCam_(static_cast<int>(NULL))  // Hack to suppress compiler warning. Spinnaker has only one contructor which takes
                                   // an int
  , camera_(static_cast<int>(NULL))
  , captureRunning_(false)
{
  unsigned int num_cameras = camList_.GetSize();
  ROS_DEBUG_STREAM_ONCE("[SpinnakerCamera]: Number of cameras detected: " << num_cameras);
}

SpinnakerCamera::~SpinnakerCamera()
{
  // @note ebretl Destructors of camList_ and system_ handle teardown
}

void SpinnakerCamera::setNewConfiguration(const any_spinnaker_camera_driver::SpinnakerConfig& config, const uint32_t& level)
{
  // Check if camera is connected
  if (!pCam_)
  {
    SpinnakerCamera::connect();
  }

  // Activate mutex to prevent us from grabbing images during this time
  std::lock_guard<std::mutex> scopedLock(mutex_);

  if (level >= LEVEL_RECONFIGURE_STOP)
  {
    ROS_DEBUG("SpinnakerCamera::setNewConfiguration: Reconfigure Stop.");
    bool capture_was_running = captureRunning_;

    // For some reason some params only work after acquisition has been started once.
    try {
      // When the camera is held by another application, we need to catch the runtime error.
      start();
      stop();
    } catch (const std::runtime_error& e) {
      throw std::runtime_error("Failed to restart the camera: " + std::string(e.what()));
    }

    camera_->setNewConfiguration(config, level);
    if (capture_was_running)
      start();
  }
  else
  {
    camera_->setNewConfiguration(config, level);
  }
}  // end setNewConfiguration

void SpinnakerCamera::setGain(const float& gain)
{
  if (camera_)
    camera_->setGain(gain);
}

int SpinnakerCamera::getHeightMax()
{
  if (camera_)
    return camera_->getHeightMax();
  else
    return 0;
}

int SpinnakerCamera::getWidthMax()
{
  if (camera_)
    return camera_->getWidthMax();
  else
    return 0;
}

Spinnaker::GenApi::CNodePtr SpinnakerCamera::readProperty(const Spinnaker::GenICam::gcstring property_name)
{
  if (camera_)
  {
    return camera_->readProperty(property_name);
  }
  else
  {
    return 0;
  }
}

bool SpinnakerCamera::obtainCameraPtr(double sleep_time){
  const ros::Time currTime{ros::Time::now()};
  while ((!pCam_ || !pCam_->IsValid()) && ros::ok()){
    // Without the following line, camList_ will be always empty if we start the ROS driver before the cameras are powered on.
    camList_ = system_->GetCameras();
    // If we have a specific camera to connect to (specified by a serial number)
    if (serial_ != 0)
    {
      const auto serial_string = std::to_string(serial_);

      try
      {
        pCam_ = camList_.GetBySerial(serial_string);
        if (!pCam_ || !pCam_->IsValid()){
          // This can happen when the robot is still on but the sensor power is cut off.
          ROS_INFO_STREAM_THROTTLE(10, "Could not find camera with serial number " +
              serial_string + ". Is that camera plugged in? (Throttled: 10s)");
          continue;
        }
        else{
          break;
        }
      }
      catch (const Spinnaker::Exception& e)
      {
        // This can happen when the robot is still on but the sensor power is cut off.
        ROS_INFO_STREAM_THROTTLE(10, "Could not find camera with serial number " +
                         serial_string + ". Is that camera plugged in (Throttled: 10s)? Info: " + std::string(e.what()));
      }
    }
    else
    {
      // Connect to any camera (the first)
      try
      {
        pCam_ = camList_.GetByIndex(0);
        if (!pCam_ || !pCam_->IsValid()){
          ROS_INFO_STREAM_THROTTLE(10, "Failed to get first connected camera. Is that camera plugged in? (Throttled: 10s)");
          continue;
        } else{
          break;
        }
      }
      catch (const Spinnaker::Exception& e)
      {
        // This exception is not captured in general.
        ROS_INFO_STREAM_THROTTLE(10, "Failed to get first connected camera. Is that camera plugged in? (Throttled: 10s) Info: " +
                         std::string(e.what()));
        continue;
      }
    }
    // Allow some time to sleep before querying the camera again.
    ros::Duration(sleep_time).sleep();

    if ((ros::Time::now() - currTime).toSec() > deviceConnectionTimeout_) {
      ROS_ERROR("Cannot connect to the device within %f seconds", deviceConnectionTimeout_);
      return false;
    }
  }
  return true;
}

bool SpinnakerCamera::connect()
{
  if (!pCam_)
  {
    if(!obtainCameraPtr(1.0)){
      return false;
    }

    try
    {
      // Check Device type and save serial for reconnecting
      Spinnaker::GenApi::INodeMap& genTLNodeMap = pCam_->GetTLDeviceNodeMap();

      if (serial_ == 0)
      {
        Spinnaker::GenApi::CStringPtr serial_ptr =
            static_cast<Spinnaker::GenApi::CStringPtr>(genTLNodeMap.GetNode("DeviceSerialNumber"));
        if (IsAvailable(serial_ptr) && IsReadable(serial_ptr))
        {
          serial_ = atoi(serial_ptr->GetValue().c_str());
          ROS_DEBUG("[SpinnakerCamera::connect]: Using Serial: %i", serial_);
        }
        else
        {
          ROS_DEBUG("[SpinnakerCamera::connect]: Unable to determine serial number.");
          return false;
        }
      }

      Spinnaker::GenApi::CEnumerationPtr device_type_ptr =
          static_cast<Spinnaker::GenApi::CEnumerationPtr>(genTLNodeMap.GetNode("DeviceType"));

      if (IsAvailable(device_type_ptr) && IsReadable(device_type_ptr))
      {
        ROS_DEBUG_STREAM("[SpinnakerCamera::connect]: Detected device type: " << device_type_ptr->ToString());

        if (device_type_ptr->GetCurrentEntry() == device_type_ptr->GetEntryByName("U3V"))
        {
          Spinnaker::GenApi::CEnumerationPtr device_speed_ptr =
              static_cast<Spinnaker::GenApi::CEnumerationPtr>(genTLNodeMap.GetNode("DeviceCurrentSpeed"));
          if (IsAvailable(device_speed_ptr) && IsReadable(device_speed_ptr))
          {
            if (device_speed_ptr->GetCurrentEntry() != device_speed_ptr->GetEntryByName("SuperSpeed"))
              ROS_ERROR_STREAM("[SpinnakerCamera::connect]: U3V Device not running at Super-Speed. Check Cables! ");
          }
        }
        // TODO(mhosmar): - check if interface is GigE and connect to GigE cam
      }
    }
    catch (const Spinnaker::Exception& e)
    {
      ROS_ERROR_STREAM("[SpinnakerCamera::connect] Failed to determine device info with error: " +
                               std::string(e.what()));
      return false;
    }

    try
    {
	  // Set buffer mode to newest first, to always transmit first the most recent frames.
      pCam_->TLStream.StreamBufferHandlingMode.SetValue(Spinnaker::StreamBufferHandlingMode_NewestFirst);
      
      // Set the buffer count mode to manual, and limit number of buffers to 1 per camera to ensure real-time.
      pCam_->TLStream.StreamBufferCountMode.SetValue(Spinnaker::StreamBufferCountMode_Manual);
      pCam_->TLStream.StreamBufferCountManual.SetValue(1);
		
      // Initialize Camera
      pCam_->Init();

      // Retrieve GenICam nodemap
      node_map_ = &pCam_->GetNodeMap();

      // detect model and set camera_ accordingly;
      Spinnaker::GenApi::CStringPtr model_name = node_map_->GetNode("DeviceModelName");
      const std::string model_name_str(model_name->ToString());

      ROS_DEBUG("[SpinnakerCamera::connect]: Camera model name: %s", model_name_str.c_str());

      // Display device information summary
      Spinnaker::GenApi::INodeMap& genTLNodeMap = pCam_->GetTLDeviceNodeMap();
      Spinnaker::GenApi::CEnumerationPtr device_type_ptr =
          static_cast<Spinnaker::GenApi::CEnumerationPtr>(genTLNodeMap.GetNode("DeviceType"));
      const std::string device_type_str(device_type_ptr->ToString());

      ROS_INFO_STREAM("[SpinnakerCamera]: Detected device type: " << device_type_str << "."
                   << " Camera model name: '" <<  model_name_str << "'"
                   << " with serial '" << std::to_string(serial_) << "'.");

      if (model_name_str.find("Blackfly S") != std::string::npos){
        camera_.reset(new Camera(node_map_));
      }
      else if (model_name_str.find("Chameleon3") != std::string::npos){
        camera_.reset(new Cm3(node_map_));
      }
      else
      {
        camera_.reset(new Camera(node_map_));
        ROS_WARN("SpinnakerCamera::connect: Could not detect camera model name.");
      }

      // Configure chunk data - Enable Metadata
      // SpinnakerCamera::ConfigureChunkData(*node_map_);
    }
    catch (const Spinnaker::Exception& e)
    {
      if (e == Spinnaker::SPINNAKER_ERR_INVALID_ADDRESS)
      {
        ROS_WARN_STREAM("The camera is on a wrong subnet. Will run auto force IP to configure the "
                "camera correctly.");
        if (pCam_){
          autoConfigure(pCam_);
        }
      }
      ROS_ERROR_STREAM("[SpinnakerCamera::connect] Failed to connect to camera. Error: " +
                               std::string(e.what()));
      return false;
    }
    catch (const std::runtime_error& e)
    {
      ROS_ERROR_STREAM("[SpinnakerCamera::connect] Failed to configure chunk data. Error: " +
                               std::string(e.what()));
      return false;
    }
  }
  return true;

  // TODO(mhosmar): Get camera info to check if camera is running in color or mono mode
  /*
  CameraInfo cInfo;
  error = cam_.GetCameraInfo(&cInfo);
  SpinnakerCamera::handleError("SpinnakerCamera::connect  Failed to get camera info.", error);
  isColor_ = cInfo.isColorCamera;
  */
}

void SpinnakerCamera::autoConfigure(const Spinnaker::CameraPtr& camPointer) const
{
  // Retrieve TL nodemap from Camera pointer.
  Spinnaker::GenApi::INodeMap& nodeMapInterface = camPointer->GetTLDeviceNodeMap();

  Spinnaker::GenApi::CEnumerationPtr ptrDeviceType =  nodeMapInterface.GetNode("DeviceType");
  if (!IsAvailable(ptrDeviceType) || !IsReadable(ptrDeviceType))
  {
    ROS_WARN_STREAM("Unable to read DeviceType for the camera with a serial " << serial_);
    return;
  }

  if (ptrDeviceType->GetIntValue() != Spinnaker::DeviceType_GigEVision)
  {
    // Only force IP on GEV device.
    return;
  }

  Spinnaker::GenApi::CCommandPtr ptrAutoForceIP = nodeMapInterface.GetNode("GevDeviceAutoForceIP");
  if (IsAvailable(ptrAutoForceIP) && IsWritable(ptrAutoForceIP))
  {
    camPointer->TLDevice.GevDeviceAutoForceIP.Execute();
    ROS_INFO_STREAM("AutoForceIP executed for camera with a serial "  << serial_);
  }
  else
  {
    ROS_WARN("Force IP node not available for this interface");
  }
}

void SpinnakerCamera::disconnect()
{
  std::lock_guard<std::mutex> scopedLock(mutex_);
  captureRunning_ = false;
  try
  {
    // Check if camera is connected
    if (pCam_)
    {
      pCam_->DeInit();
      pCam_ = static_cast<int>(NULL);
      camList_.RemoveBySerial(std::to_string(serial_));
    }
    Spinnaker::CameraList temp_list = system_->GetCameras();
    camList_.Append(temp_list);
  }
  catch (const Spinnaker::Exception& e)
  {
    throw std::runtime_error("[SpinnakerCamera::disconnect] Failed to disconnect camera with error: " +
                             std::string(e.what()));
  }
}

void SpinnakerCamera::start()
{
  try
  {
    // Check if camera is connected
    if (pCam_ && !captureRunning_)
    {
      // Start capturing images
      pCam_->BeginAcquisition();
      captureRunning_ = true;
    }
  }
  catch (const Spinnaker::Exception& e)
  {
    throw std::runtime_error("[SpinnakerCamera::start] Failed to start capture with error: " + std::string(e.what()));
  }
}

void SpinnakerCamera::stop()
{
  if (pCam_ && captureRunning_)
  {
    // Stop capturing images
    try
    {
      captureRunning_ = false;
      pCam_->EndAcquisition();
    }
    catch (const Spinnaker::Exception& e)
    {
      throw std::runtime_error("[SpinnakerCamera::stop] Failed to stop capture with error: " + std::string(e.what()));
    }
  }
}

bool SpinnakerCamera::grabImage(sensor_msgs::Image* image, const std::string& frame_id)
{
  std::lock_guard<std::mutex> scopedLock(mutex_);

  // Check if Camera is connected and Running
  if (pCam_ && captureRunning_)
  {
    // Handle "Image Retrieval" Exception
    try
    {
      Spinnaker::ImagePtr image_ptr = pCam_->GetNextImage(timeout_);
      //  std::string format(image_ptr->GetPixelFormatName());
      //  std::printf("\033[100m format: %s \n", format.c_str());

      if (image_ptr->IsIncomplete())
      {
        ROS_ERROR_STREAM("[SpinnakerCamera::grabImage] Image received from camera " << std::to_string(serial_) <<
                                 " is incomplete. " << "Status: " <<  Spinnaker::Image::GetImageStatusDescription(image_ptr->GetImageStatus()));
        return false;
      }
      else
      {
        // Set Image Time Stamp
        image->header.stamp.sec = image_ptr->GetTimeStamp() * 1e-9;
        image->header.stamp.nsec = image_ptr->GetTimeStamp();

        // Check the bits per pixel.
        size_t bitsPerPixel = image_ptr->GetBitsPerPixel();

        // --------------------------------------------------
        // Set the image encoding
        std::string imageEncoding = sensor_msgs::image_encodings::MONO8;

        Spinnaker::GenApi::CEnumerationPtr color_filter_ptr =
            static_cast<Spinnaker::GenApi::CEnumerationPtr>(node_map_->GetNode("PixelColorFilter"));

        Spinnaker::GenICam::gcstring color_filter_str = color_filter_ptr->ToString();
        Spinnaker::GenICam::gcstring bayer_rg_str = "BayerRG";
        Spinnaker::GenICam::gcstring bayer_gr_str = "BayerGR";
        Spinnaker::GenICam::gcstring bayer_gb_str = "BayerGB";
        Spinnaker::GenICam::gcstring bayer_bg_str = "BayerBG";

        // if(isColor_ && bayer_format != NONE)
        if (color_filter_ptr->GetCurrentEntry() != color_filter_ptr->GetEntryByName("None"))
        {
          if (bitsPerPixel == 16)
          {
            // 16 Bits per Pixel
            if (color_filter_str.compare(bayer_rg_str) == 0)
            {
              imageEncoding = sensor_msgs::image_encodings::BAYER_RGGB16;
            }
            else if (color_filter_str.compare(bayer_gr_str) == 0)
            {
              imageEncoding = sensor_msgs::image_encodings::BAYER_GRBG16;
            }
            else if (color_filter_str.compare(bayer_gb_str) == 0)
            {
              imageEncoding = sensor_msgs::image_encodings::BAYER_GBRG16;
            }
            else if (color_filter_str.compare(bayer_bg_str) == 0)
            {
              imageEncoding = sensor_msgs::image_encodings::BAYER_BGGR16;
            }
            else
            {
              throw std::runtime_error("[SpinnakerCamera::grabImage] Bayer format not recognized for 16-bit format.");
            }
          }
          else
          {
            // 8 Bits per Pixel
            if (color_filter_str.compare(bayer_rg_str) == 0)
            {
              imageEncoding = sensor_msgs::image_encodings::BAYER_RGGB8;
            }
            else if (color_filter_str.compare(bayer_gr_str) == 0)
            {
              imageEncoding = sensor_msgs::image_encodings::BAYER_GRBG8;
            }
            else if (color_filter_str.compare(bayer_gb_str) == 0)
            {
              imageEncoding = sensor_msgs::image_encodings::BAYER_GBRG8;
            }
            else if (color_filter_str.compare(bayer_bg_str) == 0)
            {
              imageEncoding = sensor_msgs::image_encodings::BAYER_BGGR8;
            }
            else
            {
              throw std::runtime_error("[SpinnakerCamera::grabImage] Bayer format not recognized for 8-bit format.");
            }
          }
        }
        else  // Mono camera or in pixel binned mode.
        {
          if (bitsPerPixel == 16)
          {
            imageEncoding = sensor_msgs::image_encodings::MONO16;
          }
          else if (bitsPerPixel == 24)
          {
            imageEncoding = sensor_msgs::image_encodings::RGB8;
          }
          else
          {
            imageEncoding = sensor_msgs::image_encodings::MONO8;
          }
        }

        int width = image_ptr->GetWidth();
        int height = image_ptr->GetHeight();
        int stride = image_ptr->GetStride();

        ROS_DEBUG_ONCE("\033[93m wxh: (%d, %d), stride: %d \n", width, height, stride);
        fillImage(*image, imageEncoding, height, width, stride, image_ptr->GetData());
        image->header.frame_id = frame_id;
        return true;
      }  // end else
    }
    catch (const Spinnaker::Exception& e)
    {
      ROS_ERROR_STREAM("[SpinnakerCamera::grabImage] Failed to retrieve buffer with error: " << e.what());
      return false;
    }
  }
  else if (pCam_)
  {
    throw CameraNotRunningException("[SpinnakerCamera::grabImage] Camera is currently not running.  Please start "
                                    "capturing frames first.");
  }
  else
  {
    throw std::runtime_error("[SpinnakerCamera::grabImage] Not connected to the camera.");
  }
}  // end grabImage

void SpinnakerCamera::setTimeout(const double& timeout)
{
  timeout_ = static_cast<uint64_t>(std::round(timeout * 1000));
}
void SpinnakerCamera::setDesiredCamera(const uint32_t& id)
{
  serial_ = id;
}

void SpinnakerCamera::ConfigureChunkData(const Spinnaker::GenApi::INodeMap& nodeMap)
{
  ROS_INFO_STREAM("*** CONFIGURING CHUNK DATA ***");
  try
  {
    // Activate chunk mode
    //
    // *** NOTES ***
    // Once enabled, chunk data will be available at the end of the payload
    // of every image captured until it is disabled. Chunk data can also be
    // retrieved from the nodemap.
    //
    Spinnaker::GenApi::CBooleanPtr ptrChunkModeActive = nodeMap.GetNode("ChunkModeActive");
    if (!Spinnaker::GenApi::IsAvailable(ptrChunkModeActive) || !Spinnaker::GenApi::IsWritable(ptrChunkModeActive))
    {
      throw std::runtime_error("Unable to activate chunk mode. Aborting...");
    }
    ptrChunkModeActive->SetValue(true);
    ROS_INFO_STREAM_ONCE("Chunk mode activated...");

    // Enable all types of chunk data
    //
    // *** NOTES ***
    // Enabling chunk data requires working with nodes: "ChunkSelector"
    // is an enumeration selector node and "ChunkEnable" is a boolean. It
    // requires retrieving the selector node (which is of enumeration node
    // type), selecting the entry of the chunk data to be enabled, retrieving
    // the corresponding boolean, and setting it to true.
    //
    // In this example, all chunk data is enabled, so these steps are
    // performed in a loop. Once this is complete, chunk mode still needs to
    // be activated.
    //
    Spinnaker::GenApi::NodeList_t entries;
    // Retrieve the selector node
    Spinnaker::GenApi::CEnumerationPtr ptrChunkSelector = nodeMap.GetNode("ChunkSelector");
    if (!Spinnaker::GenApi::IsAvailable(ptrChunkSelector) || !Spinnaker::GenApi::IsReadable(ptrChunkSelector))
    {
      throw std::runtime_error("Unable to retrieve chunk selector. Aborting...");
    }
    // Retrieve entries
    ptrChunkSelector->GetEntries(entries);

    ROS_INFO_STREAM("Enabling entries...");

    for (unsigned int i = 0; i < entries.size(); i++)
    {
      // Select entry to be enabled
      Spinnaker::GenApi::CEnumEntryPtr ptrChunkSelectorEntry = entries.at(i);
      // Go to next node if problem occurs
      if (!Spinnaker::GenApi::IsAvailable(ptrChunkSelectorEntry) ||
          !Spinnaker::GenApi::IsReadable(ptrChunkSelectorEntry))
      {
        continue;
      }
      ptrChunkSelector->SetIntValue(ptrChunkSelectorEntry->GetValue());

      ROS_INFO_STREAM("\t" << ptrChunkSelectorEntry->GetSymbolic() << ": ");
      // Retrieve corresponding boolean
      Spinnaker::GenApi::CBooleanPtr ptrChunkEnable = nodeMap.GetNode("ChunkEnable");
      // Enable the boolean, thus enabling the corresponding chunk data
      if (!Spinnaker::GenApi::IsAvailable(ptrChunkEnable))
      {
        ROS_INFO("Node not available");
      }
      else if (ptrChunkEnable->GetValue())
      {
        ROS_INFO("Enabled");
      }
      else if (Spinnaker::GenApi::IsWritable(ptrChunkEnable))
      {
        ptrChunkEnable->SetValue(true);
        ROS_INFO("Enabled");
      }
      else
      {
        ROS_INFO("Node not writable");
      }
    }
  }
  catch (const Spinnaker::Exception& e)
  {
    throw std::runtime_error(e.what());
  }
}
}  // namespace any_spinnaker_camera_driver
