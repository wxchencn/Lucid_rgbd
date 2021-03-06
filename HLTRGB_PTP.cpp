/***************************************************************************************
***                                                                                 ***
***  Copyright (c) 2021, Lucid Vision Labs, Inc.                                    ***
***                                                                                 ***
***  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR     ***
***  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,       ***
***  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE    ***
***  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER         ***
***  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,  ***
***  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE  ***
***  SOFTWARE.                                                                      ***
***                                                                                 ***
***************************************************************************************/

/**************************************************************************************
***                                                                                 ***
***               This example requires OpenCV library installed                    ***
***                                                                                 ***
***   For more information see "HLT+RGB_README.txt" in cpp source examples folder   ***
***                                                                                 ***
***                             default path:                                       ***
***      C:\ProgramData\Lucid Vision Labs\Examples\src\C++ Source Code Examples     ***
***                                                                                 ***
***************************************************************************************/

#include <stdio.h>

#include <mutex>

#include "ArenaApi.h"
#include "SaveApi.h"

#define TAB1 "  "
#define TAB2 "    "
#define TAB3 "      "

#include <unistd.h>

#include <fstream>
#include <iostream>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <sstream>  //std::stringstream

// PTP control variables
bool g_use_sac = true;
uint32_t g_action_delta_time = 1;
bool g_round_up_action_time = true;
std::mutex g_transfer_control_mutex;
std::mutex syncHLTMutex;
std::mutex syncTRIMutex;
int g_ActionDeviceKey = 1;
int g_ActionGroupKey = 1;
int g_ActionGroupMask = 1;
int g_ActionCommandTargetIp = 0xFFFFFFFF;  // Send the commands to the broadcast address, 255.255.255.255

// Helios RGB: Overlay PTP SAC
// This example demonstrates color overlay over 3D image, part 3 - Overlay:
//		With the system calibrated, we can now remove the calibration target from the scene and grab new images with the Helios and Triton cameras,
//      using the calibration result to find the RGB color for each 3D point measured with the Helios. Based on the output of solvePnP we can project
//      the 3D points measured by the Helios onto the RGB camera image using the OpenCV function projectPoints.
//      Grab a Helios image with the GetHeliosImage() function(output: xyz_mm) and a Triton RGB image with the GetTritionRGBImage() function(output: triton_rgb).
//      The following code shows how to project the Helios xyz points onto the Triton image, giving a(row, col) position for each 3D point.
//      We can sample the Triton image at that(row, col) position to find the 3D point?s RGB value.
//
// This example also adds PTP & SAC functionality to synchronize the images.

// =-=-=-=-=-=-=-=-=-
// =-=- SETTINGS =-=-
// =-=-=-=-=-=-=-=-=-

// image timeout
#define TIMEOUT 200

// orientation values file name
#define FILE_NAME_IN "orientation.yml"

// file name
// separate naming patterns for OpenCV and Arena since they use counters differently
// #define OPENCV_FILE_NAME "Images\\Cpp_HLTRGB_3" //for HLT and TRI images
// #define ARENA_FILE_NAME "Images\\Cpp_HLTRGB_3_Overlay<count:path>.ply" //for overlay
#define OPENCV_FILE_NAME "Images/Cpp_HLTRGB_3"                         // for HLT and TRI images
#define ARENA_FILE_NAME "Images/Cpp_HLTRGB_3_Overlay<count:path>.ply"  // for overlay

// number of images to capture from each camera
#define NUM_ITERATIONS 3

// HLT settings
// #define HLT_Operating_Mode "Distance6000mmSingleFreq"
#define HLT_Operating_Mode "Distance3000mmSingleFreq"
// options:
//	 Distance8333mmMultiFreq
//	 Distance6000mmSingleFreq
//	 Distance5000mmMultiFreq
//	 Distance4000mmSingleFreq
//	 Distance3000mmSingleFreq
//	 Distance1250mmSingleFreq
// single-frequency operating modes have faster image capture
#define HLT_Exposure_Time "Exp1000Us"
// options:
//	 Exp1000Us
//	 Exp250Us
//	 Exp62_5Us
// shorter exposure time has faster image capture

// =-=-=-=-=-=-=-=-=-
// =-=- HELPERS -=-=-
// =-=-=-=-=-=-=-=-=-

//
// For PTP/SAC
//

void SetCameraAsPtpMaster(Arena::IDevice* pDevice) {
    Arena::SetNodeValue<bool>(pDevice->GetNodeMap(), "PtpSlaveOnly", false);
    Arena::SetNodeValue<bool>(pDevice->GetNodeMap(), "PtpEnable", true);
}

void SetCameraAsPtpSlave(Arena::IDevice* pDevice) {
    Arena::SetNodeValue<bool>(pDevice->GetNodeMap(), "PtpSlaveOnly", true);
    Arena::SetNodeValue<bool>(pDevice->GetNodeMap(), "PtpEnable", true);
}

void ApplyHLTSettings(Arena::IDevice* pDevice) {
    // Enable packet size negotiation and packet resend
    Arena::SetNodeValue<bool>(pDevice->GetTLStreamNodeMap(), "StreamAutoNegotiatePacketSize", true);
    Arena::SetNodeValue<bool>(pDevice->GetTLStreamNodeMap(), "StreamPacketResendEnable", true);

    // Trigger on Action0
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "TriggerSelector", "FrameStart");
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "TriggerSource", "Action0");
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "TriggerMode", "On");

    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "ActionUnconditionalMode", "On");
    Arena::SetNodeValue<int64_t>(pDevice->GetNodeMap(), "ActionSelector", 0);
    Arena::SetNodeValue<int64_t>(pDevice->GetNodeMap(), "ActionDeviceKey", g_ActionDeviceKey);
    Arena::SetNodeValue<int64_t>(pDevice->GetNodeMap(), "ActionGroupKey", g_ActionGroupKey);
    Arena::SetNodeValue<int64_t>(pDevice->GetNodeMap(), "ActionGroupMask", g_ActionDeviceKey);

    // Use Coord3D_ABCY16 format
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "PixelFormat", "Coord3D_ABCY16");

    // Set Operating Mode and Exposure Time as defined at top of code
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "Scan3dOperatingMode", HLT_Operating_Mode);
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "ExposureTimeSelector", HLT_Exposure_Time);

    std::cout << "HLT using: " << HLT_Operating_Mode << " operating mode, and " << HLT_Exposure_Time << " exposure time" << std::endl;
}

void ApplyTRISettings(Arena::IDevice* pDevice) {
    // Enable packet size negotiation and packet resend
    Arena::SetNodeValue<bool>(pDevice->GetTLStreamNodeMap(), "StreamAutoNegotiatePacketSize", true);
    Arena::SetNodeValue<bool>(pDevice->GetTLStreamNodeMap(), "StreamPacketResendEnable", true);

    // Trigger on Action0
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "TriggerSelector", "FrameStart");
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "TriggerSource", "Action0");
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "TriggerMode", "On");

    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "ActionUnconditionalMode", "On");
    Arena::SetNodeValue<int64_t>(pDevice->GetNodeMap(), "ActionSelector", 0);
    Arena::SetNodeValue<int64_t>(pDevice->GetNodeMap(), "ActionDeviceKey", g_ActionDeviceKey);
    Arena::SetNodeValue<int64_t>(pDevice->GetNodeMap(), "ActionGroupKey", g_ActionGroupKey);
    Arena::SetNodeValue<int64_t>(pDevice->GetNodeMap(), "ActionGroupMask", g_ActionDeviceKey);

    // Use automatic exposure time
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "ExposureAuto", "Continuous");

    // Use RGB8 format
    Arena::SetNodeValue<GenICam::gcstring>(pDevice->GetNodeMap(), "PixelFormat", "RGB8");

    std::cout << "TRI using automatic exposure time, and RGB8 pixel format" << std::endl;
}

void FireScheduledActionCommand(Arena::ISystem* pSystem, Arena::IDevice* pDeviceHLT) {
    // Get the PTP timestamp from the Master camera
    Arena::ExecuteNode(pDeviceHLT->GetNodeMap(), "PtpDataSetLatch");
    int64_t curr_ptp = Arena::GetNodeValue<int64_t>(pDeviceHLT->GetNodeMap(), "PtpDataSetLatchValue");

    std::cout << TAB1 << "Read PtpDataSetLatchValue on HLT " << curr_ptp << " ns" << std::endl;

    // Round up to the nearest second
    if (g_round_up_action_time) {
        curr_ptp /= 1000000000;
        curr_ptp += static_cast<int64_t>(g_action_delta_time) + 1;
        curr_ptp *= 1000000000;
    } else {
        curr_ptp += static_cast<int64_t>(g_action_delta_time) * 1000000000;
    }

    // Fire an Action Command g_action_delta_time seconds from now
    std::cout << TAB1 << "Scheduled Action Command set for time: " << curr_ptp << " ns" << std::endl;

    Arena::SetNodeValue<int64_t>(pSystem->GetTLSystemNodeMap(), "ActionCommandExecuteTime", curr_ptp);
    Arena::ExecuteNode(pSystem->GetTLSystemNodeMap(), "ActionCommandFireCommand");
}

//
// For Overlay
//
void OverlayColorOnto3DAndSave(Arena::IDevice* pDeviceTRI, Arena::IDevice* pDeviceHLT, int64_t actionCommandExecuteTime, int counter) {
    // Read in camera matrix, distance coefficients, and rotation and translation vectors
    cv::Mat cameraMatrix;
    cv::Mat distCoeffs;
    cv::Mat rotationVector;
    cv::Mat translationVector;

    cv::FileStorage fs(FILE_NAME_IN, cv::FileStorage::READ);

    fs["cameraMatrix"] >> cameraMatrix;
    fs["distCoeffs"] >> distCoeffs;
    fs["rotationVector"] >> rotationVector;
    fs["translationVector"] >> translationVector;

    fs.release();

    // variables for HLT
    Arena::IImage* pImageHLT = nullptr;
    cv::Mat imageMatrixXYZ;
    size_t width = 0;
    size_t height = 0;
    GenApi::INodeMap* HLT_node_map = pDeviceHLT->GetNodeMap();
    double xyz_scale_mm = Arena::GetNodeValue<double>(HLT_node_map, "Scan3dCoordinateScale");
    Arena::SetNodeValue<GenICam::gcstring>(HLT_node_map, "Scan3dCoordinateSelector", "CoordinateA");
    double x_offset_mm = Arena::GetNodeValue<double>(HLT_node_map, "Scan3dCoordinateOffset");
    Arena::SetNodeValue<GenICam::gcstring>(HLT_node_map, "Scan3dCoordinateSelector", "CoordinateB");
    double y_offset_mm = Arena::GetNodeValue<double>(HLT_node_map, "Scan3dCoordinateOffset");
    Arena::SetNodeValue<GenICam::gcstring>(HLT_node_map, "Scan3dCoordinateSelector", "CoordinateC");
    double z_offset_mm = Arena::GetNodeValue<double>(HLT_node_map, "Scan3dCoordinateOffset");

    // variables for TRI
    Arena::IImage* pImageTRI = nullptr;
    size_t triHeight, triWidth;
    cv::Mat imageMatrixRGB;

    std::cout << TAB1 << "Get HLT and TRI images\n";
    for (uint32_t x = 0; x < 2; x++) {
        std::unique_lock<std::mutex> deviceLock(g_transfer_control_mutex);

        if (x == 0) {
            // Get an image from Helios
            pImageHLT = pDeviceHLT->GetImage(g_action_delta_time * 1000 * 2);  // Wait for 2 * g_action_delta_time in seconds
        } else {
            // Get an image from Triton
            pImageTRI = pDeviceTRI->GetImage(g_action_delta_time * 1000 * 2);  // Wait for 2 * g_action_delta_time in seconds
        }
    }

    // HLT image processing
    width = pImageHLT->GetWidth();
    height = pImageHLT->GetHeight();
    imageMatrixXYZ = cv::Mat((int)height, (int)width, CV_32FC3);
    const uint16_t* input_data = reinterpret_cast<const uint16_t*>(pImageHLT->GetData());
    for (unsigned int ir = 0; ir < height; ++ir) {
        for (unsigned int ic = 0; ic < width; ++ic) {
            // Get unsigned 16 bit values for X,Y,Z coordinates
            ushort x_u16 = input_data[0];
            ushort y_u16 = input_data[1];
            ushort z_u16 = input_data[2];

            // Convert 16-bit X,Y,Z to float values in mm
            if (x_u16 == 0xffff || y_u16 == 0xffff || z_u16 == 0xffff) {  // erase invalid data
                imageMatrixXYZ.at<cv::Vec3f>(ir, ic)[0] = (float)(0.0);
                imageMatrixXYZ.at<cv::Vec3f>(ir, ic)[1] = (float)(0.0);
                imageMatrixXYZ.at<cv::Vec3f>(ir, ic)[2] = (float)(0.0);
            } else {
                imageMatrixXYZ.at<cv::Vec3f>(ir, ic)[0] = (float)(x_u16 * xyz_scale_mm + x_offset_mm);
                imageMatrixXYZ.at<cv::Vec3f>(ir, ic)[1] = (float)(y_u16 * xyz_scale_mm + y_offset_mm);
                imageMatrixXYZ.at<cv::Vec3f>(ir, ic)[2] = (float)(z_u16 * xyz_scale_mm + z_offset_mm);
            }

            input_data += 4;
        }
    }

    // HLT image and timestamp
    cv::imwrite(OPENCV_FILE_NAME "_XYZ" + std::to_string(counter) + ".jpg", imageMatrixXYZ);
    std::cout << TAB2 << "Got FrameID " << pImageHLT->GetFrameId() << " from HLT with timestamp: " << pImageHLT->GetTimestamp() << " ns \t (" << (pImageHLT->GetTimestamp() - actionCommandExecuteTime) << " ns offset)" << std::endl;

    // TRI image processing
    triHeight = pImageTRI->GetHeight();
    triWidth = pImageTRI->GetWidth();
    imageMatrixRGB = cv::Mat((int)triHeight, (int)triWidth, CV_8UC3);
    memcpy(imageMatrixRGB.data, pImageTRI->GetData(), triHeight * triWidth * 3);

    // TRI image and timestamp
    cv::imwrite(OPENCV_FILE_NAME "_RGB" + std::to_string(counter) + ".jpg", imageMatrixRGB);
    std::cout << TAB2 << "Got FrameID " << pImageTRI->GetFrameId() << " from TRI with timestamp: " << pImageTRI->GetTimestamp() << " ns \t (" << (pImageTRI->GetTimestamp() - actionCommandExecuteTime) << " ns offset)" << std::endl;

    // Overlay RGB color data onto 3D XYZ points
    std::cout << TAB1 << "Overlay the RGB color data onto the 3D XYZ points\n";

    // reshape image matrix
    std::cout << TAB2 << "Reshape XYZ matrix\n";

    int size = imageMatrixXYZ.rows * imageMatrixXYZ.cols;
    cv::Mat xyzPoints = imageMatrixXYZ.reshape(3, size);

    // project points
    std::cout << TAB2 << "Project points\n";

    cv::Mat projectedPointsTRI;

    cv::projectPoints(
        xyzPoints,
        rotationVector,
        translationVector,
        cameraMatrix,
        distCoeffs,
        projectedPointsTRI);

    // loop through projected points to access RGB data at those points
    std::cout << TAB2 << "Get values at projected points\n";

    uint8_t* pColorData = new uint8_t[width * height * 3];

    for (int i = 0; i < width * height; i++) {
        unsigned int colTRI = (unsigned int)std::round(projectedPointsTRI.at<cv::Vec2f>(i)[0]);
        unsigned int rowTRI = (unsigned int)std::round(projectedPointsTRI.at<cv::Vec2f>(i)[1]);

        // only handle appropriate points
        if (rowTRI < 0 ||
            colTRI < 0 ||
            rowTRI >= static_cast<unsigned int>(imageMatrixRGB.rows) ||
            colTRI >= static_cast<unsigned int>(imageMatrixRGB.cols))
            continue;

        // access corresponding XYZ and RGB data
        uchar R = imageMatrixRGB.at<cv::Vec3b>(rowTRI, colTRI)[0];
        uchar G = imageMatrixRGB.at<cv::Vec3b>(rowTRI, colTRI)[1];
        uchar B = imageMatrixRGB.at<cv::Vec3b>(rowTRI, colTRI)[2];

        float X = imageMatrixXYZ.at<cv::Vec3f>(i)[0];
        float Y = imageMatrixXYZ.at<cv::Vec3f>(i)[1];
        float Z = imageMatrixXYZ.at<cv::Vec3f>(i)[2];

        // grab RGB data to save colored .ply
        pColorData[i * 3 + 0] = B;
        pColorData[i * 3 + 1] = G;
        pColorData[i * 3 + 2] = R;
    }

    // Save result

    // prepare to save
    Save::ImageParams params(
        pImageHLT->GetWidth(),
        pImageHLT->GetHeight(),
        pImageHLT->GetBitsPerPixel());

    Save::ImageWriter plyWriter(params, ARENA_FILE_NAME);

    // save .ply with color data
    bool filterPoints = true;
    bool isSignedPixelFormat = false;

    plyWriter.SetPly(".ply", filterPoints, isSignedPixelFormat, xyz_scale_mm, x_offset_mm, y_offset_mm, z_offset_mm);

    plyWriter.Save(pImageHLT->GetData(), pColorData);

    std::cout << TAB1 << "Save overlay to " << plyWriter.GetLastFileName(true) << "\n\n";

    // requeue image buffers
    pDeviceHLT->RequeueBuffer(pImageHLT);
    pDeviceTRI->RequeueBuffer(pImageTRI);

    // delete pColorData to prevent memory leak
    pColorData = NULL;
    delete pColorData;
}

// =-=-=-=-=-=-=-=-=-
// =- PREPARATION -=-
// =- & CLEAN UP =-=-
// =-=-=-=-=-=-=-=-=-

bool isApplicableDeviceTriton(Arena::DeviceInfo deviceInfo) {
    return deviceInfo.ModelName() == "TRI032S-C";  // change name here to search for different Triton model
}

bool isApplicableDeviceHelios2(Arena::DeviceInfo deviceInfo)  // either Helios2 or Helios2+ can be used
{
    return ((deviceInfo.ModelName().find("HLT") != GenICam::gcstring::npos) || (deviceInfo.ModelName().find("HTP") != GenICam::gcstring::npos));
}

int main() {
    // flag to track when an exception has been thrown
    bool exceptionThrown = false;

    std::cout << "Cpp_HLTRGB_3_Overlay_PTP_SAC\n";

    try {
        std::ifstream ifile;
        ifile.open(FILE_NAME_IN);
        if (!ifile) {
            std::cout << "File '" << FILE_NAME_IN << "' not found\nPlease run examples 'Cpp_HLTRGB_1_Calibration' and 'Cpp_HLTRGB_2_Orientation' prior to this one\nPress enter to complete\n";
            std::getchar();
            return 0;
        }

        // prepare example
        Arena::ISystem* pSystem = Arena::OpenSystem();
        pSystem->UpdateDevices(100);
        std::vector<Arena::DeviceInfo> deviceInfos = pSystem->GetDevices();
        if (deviceInfos.size() == 0) {
            std::cout << "\nNo camera connected\nPress enter to complete\n";
            std::getchar();
            return 0;
        }

        // print list of detected device for troubleshooting
        std::cout << "Detected devices :" << std::endl;
        int counter = 0;
        for (auto& deviceInfo : deviceInfos) {
            std::cout << TAB1 << "Device " << counter << " : " << deviceInfo.ModelName() << std::endl;
            counter++;
        }

        Arena::IDevice* pDeviceTRI = nullptr;
        Arena::IDevice* pDeviceHLT = nullptr;
        std::string currPtpStatus;

        // find HLT and make it Master
        for (auto& deviceInfo : deviceInfos) {
            if (!pDeviceHLT && isApplicableDeviceHelios2(deviceInfo)) {
                pDeviceHLT = pSystem->CreateDevice(deviceInfo);
                SetCameraAsPtpMaster(pDeviceHLT);
                std::cout << "Waiting for HLT to become Master" << std::endl;
                do {
                    // Sleep(1000);
                    sleep(1);
                    std::cout << ".";

                    std::unique_lock<std::mutex> deviceLock(syncHLTMutex);
                    currPtpStatus = Arena::GetNodeValue<GenICam::gcstring>(pDeviceHLT->GetNodeMap(), "PtpStatus");
                } while (currPtpStatus != "Master");
                std::cout << std::endl;
                std::cout << "Apply HLT Settings" << std::endl;
                ApplyHLTSettings(pDeviceHLT);

            } else if (isApplicableDeviceHelios2(deviceInfo)) {
                throw std::logic_error("too many Helios2 devices connected");
            }
        }

        // find TRI and make it Slave
        for (auto& deviceInfo : deviceInfos) {
            if (!pDeviceTRI && isApplicableDeviceTriton(deviceInfo)) {
                pDeviceTRI = pSystem->CreateDevice(deviceInfo);
                SetCameraAsPtpSlave(pDeviceTRI);

                std::cout << "Waiting for TRI to become Slave" << std::endl;
                do {
                    // Sleep(1000);
                    sleep(1);
                    std::cout << ".";

                    std::unique_lock<std::mutex> deviceLock(syncTRIMutex);
                    currPtpStatus = Arena::GetNodeValue<GenICam::gcstring>(pDeviceTRI->GetNodeMap(), "PtpStatus");
                } while (currPtpStatus != "Slave");
                std::cout << std::endl;
                ApplyTRISettings(pDeviceTRI);
            } else if (isApplicableDeviceTriton(deviceInfo)) {
                throw std::logic_error("too many Triton devices connected");
            }
        }

        if (!pDeviceTRI)
            throw std::logic_error("No applicable Triton devices");

        if (!pDeviceHLT)
            throw std::logic_error("No applicable Helios 2 devices");

        if (g_use_sac == true) {
            std::cout << "Applied the following settings to GenTL System:" << std::endl;
            Arena::SetNodeValue<int64_t>(pSystem->GetTLSystemNodeMap(), "ActionCommandDeviceKey", g_ActionDeviceKey);
            Arena::SetNodeValue<int64_t>(pSystem->GetTLSystemNodeMap(), "ActionCommandGroupKey", g_ActionGroupKey);
            Arena::SetNodeValue<int64_t>(pSystem->GetTLSystemNodeMap(), "ActionCommandGroupMask", g_ActionGroupMask);
            Arena::SetNodeValue<int64_t>(pSystem->GetTLSystemNodeMap(), "ActionCommandTargetIP", g_ActionCommandTargetIp);

            std::cout << TAB1 << "ActionCommandDeviceKey = " << g_ActionDeviceKey << std::endl;
            std::cout << TAB1 << "ActionCommandGroupKey = " << g_ActionGroupKey << std::endl;
            std::cout << TAB1 << "ActionCommandGroupMask = " << g_ActionGroupMask << std::endl;
            std::cout << TAB1 << "ActionCommandTargetIP = " << (g_ActionCommandTargetIp >> 24 & 0xFF) << "." << (g_ActionCommandTargetIp >> 16 & 0xFF) << "." << (g_ActionCommandTargetIp >> 8 & 0xFF) << "." << (g_ActionCommandTargetIp & 0xFF) << std::endl;
        }
        std::cout << std::endl;

        // run example

        // get node values that will be changed in order to return their values at
        // the end of the example
        GenICam::gcstring pixelFormatInitialTRI = Arena::GetNodeValue<GenICam::gcstring>(pDeviceTRI->GetNodeMap(), "PixelFormat");
        GenICam::gcstring pixelFormatInitialHLT = Arena::GetNodeValue<GenICam::gcstring>(pDeviceHLT->GetNodeMap(), "PixelFormat");

        pDeviceHLT->StartStream();
        pDeviceTRI->StartStream();

        if (pDeviceTRI && pDeviceHLT) {
            std::cout << "Capture " << NUM_ITERATIONS << " overlays \n\n";
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                FireScheduledActionCommand(pSystem, pDeviceHLT);
                int64_t actionCommandExecuteTime = Arena::GetNodeValue<int64_t>(pSystem->GetTLSystemNodeMap(), "ActionCommandExecuteTime");
                OverlayColorOnto3DAndSave(pDeviceTRI, pDeviceHLT, actionCommandExecuteTime, i);
            }

            std::cout << "\nExample complete\n";
        }

        pDeviceHLT->StopStream();
        pDeviceTRI->StopStream();

        // return nodes to their initial values
        Arena::SetNodeValue<GenICam::gcstring>(pDeviceTRI->GetNodeMap(), "PixelFormat", pixelFormatInitialTRI);
        Arena::SetNodeValue<GenICam::gcstring>(pDeviceHLT->GetNodeMap(), "PixelFormat", pixelFormatInitialHLT);

        if (pDeviceTRI)
            pSystem->DestroyDevice(pDeviceTRI);
        if (pDeviceHLT)
            pSystem->DestroyDevice(pDeviceHLT);

        Arena::CloseSystem(pSystem);
    } catch (GenICam::GenericException& ge) {
        std::cout << "\nGenICam exception thrown: " << ge.what() << "\n";
        exceptionThrown = true;
    } catch (std::exception& ex) {
        std::cout << "\nStandard exception thrown: " << ex.what() << "\n";
        exceptionThrown = true;
    } catch (...) {
        std::cout << "\nUnexpected exception thrown\n";
        exceptionThrown = true;
    }

    std::cout << "Press enter to complete\n";
    std::getchar();

    if (exceptionThrown)
        return -1;
    else
        return 0;
}