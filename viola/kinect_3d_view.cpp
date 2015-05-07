#include <signal.h>

#include <cstdlib>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Werror"
#pragma GCC diagnostic ignored "-Wlong-long"

#pragma GCC diagnostic ignored "-pedantic"
#pragma GCC diagnostic ignored "-pedantic-errors"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include <mrpt/otherlibs/do_opencv_includes.h>

#include <mrpt/gui/CDisplayWindow3D.h>

#include <mrpt/maps/CColouredPointsMap.h>
#include <mrpt/opengl/CGridPlaneXY.h>
#include <mrpt/opengl/stock_objects.h>
#include <mrpt/opengl/CPointCloudColoured.h>
#include <mrpt/base/include/mrpt/system/threads.h>

#pragma GCC diagnostic pop

#include "KinectCamera.h"

using namespace mrpt;
using namespace mrpt::math;
using namespace mrpt::gui;
using namespace mrpt::obs;
using namespace mrpt::maps;
using namespace mrpt::utils;
using namespace mrpt::opengl;
using namespace std;

void kinect_3d_view()
{
    mrpt::gui::CDisplayWindow3D win3D("Kinect 3D view", 800, 600);

    win3D.setCameraAzimuthDeg(0);
    win3D.setCameraElevationDeg(0);
    win3D.setCameraZoom(1);
    win3D.setFOV(90);
    win3D.setCameraPointingToPoint(1, 0, 0);

    mrpt::opengl::CPointCloudColouredPtr gl_points = mrpt::opengl::CPointCloudColoured::Create();
    gl_points->setPointSize(1);

    opengl::COpenGLViewportPtr viewRange, viewInt; // Extra viewports for the RGB & D images.

    {
        mrpt::opengl::COpenGLScenePtr &scene = win3D.get3DSceneAndLock();
        scene->insert(gl_points);
        scene->insert(mrpt::opengl::CGridPlaneXY::Create());
        scene->insert(mrpt::opengl::stock_objects::CornerXYZ());
        win3D.unlockAccess3DScene();
        win3D.repaint();
    }
    KinectCamera camera;
    camera.open();
    camera.grabFrames();
    KinectCamera::IRCameraParams params = camera.getIRCameraParams();
    while (win3D.isOpen()) {
        camera.grabFrames();
        cv::Mat depth = camera.frames[KinectCamera::FrameType::DEPTH];
        CColouredPointsMap pntsMap;
        pntsMap.colorScheme.scheme = CColouredPointsMap::cmFromHeightRelativeToSensor;

        for (int x = 0; x < depth.cols; x++) {
            for (int y = 0; y < depth.rows; y++) {
                float v_z = depth.at<float>(x, y) / 1000.f;
                float v_x = ((x - params.cx) * v_z) / params.fx;
                float v_y = ((y - params.cy) * v_z) / params.fy;
                v_x *= 1;
                v_y *= 1;
                v_z *= 1;
                //std::cout << v_x << ' ' << v_y << ' ' << v_z << std::endl;
                pntsMap.insertPoint(v_y, -v_z, -v_x);
            }

        }

        win3D.get3DSceneAndLock();
        gl_points->loadFromPointsMap(&pntsMap);
        win3D.unlockAccess3DScene();

        win3D.repaint();
        mrpt::system::sleep(1);
    }
}


//int main(int argc, char *argv[])
int main()
{
    kinect_3d_view();

    return 0;

    /*
    try {
        kinect_3d_view();
        return 0;
    } catch (std::exception &e) {
        std::cout << "MRPT exception caught: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        printf("Untyped exception!!");
        return -1;
    }
    */
}

