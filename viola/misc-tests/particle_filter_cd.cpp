#include <limits>
#include <signal.h>

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

#include <mrpt/gui/CDisplayWindow.h>
#include <mrpt/random.h>
#include <mrpt/bayes/CParticleFilterData.h>
#include <mrpt/obs/CSensoryFrame.h>
#include <mrpt/obs/CObservationImage.h>
#include <mrpt/otherlibs/do_opencv_includes.h>

//#define USE_KINECT_2
#ifdef USE_KINECT_2
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#endif

#pragma GCC diagnostic pop

#define USE_INTEL_TBB
#include <tbb/tbb.h>
#ifdef USE_INTEL_TBB
#define TBB_PARTITIONS 8
#endif

#include "misc_helpers.h"
#include "geometry_helpers.h"
#include "color_model.h"

using namespace mrpt;
using namespace mrpt::bayes;
using namespace mrpt::gui;
using namespace mrpt::obs;
using namespace mrpt::random;

using namespace std;


double TRANSITION_MODEL_STD_XY   = 0;
double TRANSITION_MODEL_STD_VXY  = 0;
double NUM_PARTICLES             = 0;

#ifndef USE_KINECT_2
cv::VideoCapture capture;
#endif



vector<cv::Vec3f> detect_circles(const cv::Mat &image);

// ---------------------------------------------------------------
//      Implementation of the system models as a Particle Filter
// ---------------------------------------------------------------
struct CImageParticleData {
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
};

class CImageParticleFilter :
    public mrpt::bayes::CParticleFilterData<CImageParticleData>,
    public mrpt::bayes::CParticleFilterDataImpl < CImageParticleFilter,
    mrpt::bayes::CParticleFilterData<CImageParticleData>::CParticleList >
{
public:
    void update_particles_with_transition_model(double dt, const mrpt::obs::CSensoryFrame * const observation);
    void weight_particles_with_model(const mrpt::obs::CSensoryFrame * const observation);

    void prediction_and_update_pfStandardProposal(
        const mrpt::obs::CActionCollection*,
        const mrpt::obs::CSensoryFrame * const observation,
        const bayes::CParticleFilter::TParticleFilterOptions&);

    void initializeParticles(const size_t M,
                             const pair<float, float> x,
                             const pair<float, float> y,
                             const pair<float, float> z,
                             const pair<float, float> v_x,
                             const pair<float, float> v_y,
                             const pair<float, float> v_z,
                             const mrpt::obs::CSensoryFrame * const observation);


    void update_color_model(cv::Mat *model, const int roi_width, const int roi_height);

    void get_mean(float &x, float &y, float &z, float &vx, float &vy, float &vz) const;
    void print_particle_state(void) const;

    int64_t last_time;
private:
    //TODO POTENTIAL LEAK! USE smartptr
    cv::Mat *color_model;
    int roi_width;
    int roi_height;

};

void CImageParticleFilter::print_particle_state(void) const
{
    size_t N = m_particles.size();
    for (size_t i = 0; i < N; i++) {
        std::cout << i << ' ' << m_particles[i].d->x
            << ' ' << m_particles[i].d->y << ' ' << m_particles[i].d->z
            << ' ' << m_particles[i].d->vx<< ' ' << m_particles[i].d->vy
            << ' ' << m_particles[i].d->vz << std::endl;
    }
}

void CImageParticleFilter::update_color_model(cv::Mat *model, const int roi_width,
        const int roi_height)
{
    color_model = model;
    this->roi_width = roi_width;
    this->roi_height = roi_height;
}

void CImageParticleFilter::update_particles_with_transition_model(const double dt, const mrpt::obs::CSensoryFrame * const observation)
{
    const CObservationImagePtr obs_image = observation->getObservationByClass<CObservationImage>(0);
    const CObservationImagePtr obs_depth = observation->getObservationByClass<CObservationImage>(1);

    ASSERT_(obs_image);
    ASSERT_(obs_depth);

    const cv::Mat image_mat = cv::Mat(obs_image->image.getAs<IplImage>());
    const cv::Mat depth_mat = cv::Mat(obs_depth->image.getAs<IplImage>());

    auto update_particle = [&](int i) {
        m_particles[i].d->x += dt * m_particles[i].d->vx + TRANSITION_MODEL_STD_XY *
                               randomGenerator.drawGaussian1D_normalized();
        m_particles[i].d->y += dt * m_particles[i].d->vy + TRANSITION_MODEL_STD_XY *
                               randomGenerator.drawGaussian1D_normalized();

        const double old_z = m_particles[i].d->z;
        const int x = cvRound((m_particles[i].d->x * depth_mat.cols) / float(image_mat.cols));
        const int y = cvRound((m_particles[i].d->y * depth_mat.rows) / float(image_mat.rows));
        m_particles[i].d->z = depth_mat.at<unsigned short>(y, x);

        m_particles[i].d->vx += TRANSITION_MODEL_STD_VXY * randomGenerator.drawGaussian1D_normalized();
        m_particles[i].d->vy += TRANSITION_MODEL_STD_VXY * randomGenerator.drawGaussian1D_normalized();
        m_particles[i].d->vz = (m_particles[i].d->z - old_z) / dt;
    };

    size_t N = m_particles.size();
#ifndef USE_INTEL_TBB
    for (size_t i = 0; i < N; i++) {
        update_particle(i);
    }
#else
    tbb::parallel_for(tbb::blocked_range<size_t>(0, N,
    N / TBB_PARTITIONS), [&update_particle](const tbb::blocked_range<size_t> &r) {
        for (size_t i = r.begin(); i != r.end(); i++) {
            update_particle(i);
        }
    });
#endif
}

void CImageParticleFilter::weight_particles_with_model(const mrpt::obs::CSensoryFrame * const observation)
{
    const CObservationImagePtr obs_image = observation->getObservationByClass<CObservationImage>(0);

    ASSERT_(obs_image);

    const cv::Mat image_mat = cv::Mat(obs_image->image.getAs<IplImage>());

    cv::Mat frame_hsv;
    cv::cvtColor(image_mat, frame_hsv, cv::COLOR_BGR2HSV);

    size_t N = m_particles.size();

#ifndef USE_INTEL_TBB
    vector <cv::Mat> particles_color_model(N);
    for (size_t i = 0; i < N; i++) {
        const cv::Rect particle_roi(m_particles[i].d->x - roi_width * 0.5,
                                    m_particles[i].d->y - roi_height * 0.5, roi_width, roi_height);

        if (particle_roi.x < 0 || particle_roi.y < 0 || particle_roi.width <= 0
                || particle_roi.height <= 0) {
            continue;
        }

        if (particle_roi.x + particle_roi.width >= frame_hsv.cols
                || particle_roi.y + particle_roi.height >= frame_hsv.rows) {
            continue;
        }

        const cv::Mat mask = create_ellipse_mask(particle_roi, 1);
        const cv::Mat particle_roi_img = frame_hsv(particle_roi);

        particles_color_model[i] = compute_color_model(particle_roi_img, mask);
    }
#else
    tbb::concurrent_vector <cv::Mat> particles_color_model(N);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, N, N / TBB_PARTITIONS),
        [this, &frame_hsv, &particles_color_model](const tbb::blocked_range<size_t> &r) {
            for (size_t i = r.begin(); i != r.end(); i++) {
                const cv::Rect particle_roi(m_particles[i].d->x - roi_width * 0.5,
                                            m_particles[i].d->y - roi_height * 0.5, roi_width, roi_height);
                if (particle_roi.x < 0 || particle_roi.y < 0 || particle_roi.width <= 0
                        || particle_roi.height <= 0) {
                    continue;
                }

                if (particle_roi.x + particle_roi.width >= frame_hsv.cols
                        || particle_roi.y + particle_roi.height >= frame_hsv.rows) {
                    continue;
                }

                const cv::Mat mask = create_ellipse_mask(particle_roi, 1);
                const cv::Mat particle_roi_img = frame_hsv(particle_roi);

                particles_color_model[i] = compute_color_model(particle_roi_img, mask);
            }
        }
    );
#endif

    auto weight_particle = [this, &particles_color_model] (size_t i){
        if (!particles_color_model[i].empty()) {
            const double score = 1 - cv::compareHist(*color_model, particles_color_model[i],
                                 CV_COMP_BHATTACHARYYA);
            m_particles[i].log_w += log(score);
        } else {
            m_particles[i].log_w += log(std::numeric_limits<double>::min());
        }
    };
    //third, weight them
#ifndef USE_INTEL_TBB
    for (size_t i = 0; i < N; i++) {
        weight_particle(i);
    }
#else
    tbb::parallel_for(tbb::blocked_range<size_t>(0, N, N / TBB_PARTITIONS),
        [this, &particles_color_model, &weight_particle](const tbb::blocked_range<size_t> &r) {
            for (size_t i = r.begin(); i != r.end(); i++) {
                weight_particle(i);
            }
        }
    );
#endif
}

void  CImageParticleFilter::prediction_and_update_pfStandardProposal(
    const mrpt::obs::CActionCollection*,
    const mrpt::obs::CSensoryFrame * const observation,
    const bayes::CParticleFilter::TParticleFilterOptions&)
{
    //CDisplayWindow model_window1("model1");
    //CDisplayWindow model_window2("model2");
    //CDisplayWindow model_window3("model3");

    const int64_t current_time = cv::getTickCount();
    const double dt = (current_time - last_time) / cv::getTickFrequency();
    last_time = current_time;

    update_particles_with_transition_model(dt, observation);
    weight_particles_with_model(observation);

    // Resample is automatically performed by CParticleFilter when required.
}

void CImageParticleFilter::initializeParticles(const size_t M, const pair<float, float> x,
        const pair<float, float> y,
        const pair<float, float> z, const pair<float, float> v_x, const pair<float, float> v_y,
        const pair<float, float> v_z, const mrpt::obs::CSensoryFrame * const observation)
{
    clearParticles();

    const CObservationImagePtr obs_depth = observation->getObservationByClass<CObservationImage>(1);
    ASSERT_(obs_depth);

    const cv::Mat depth_mat = cv::Mat(obs_depth->image.getAs<IplImage>());

    m_particles.resize(M);

    for (CParticleList::iterator it = m_particles.begin(); it != m_particles.end(); it++) {
        it->d = new CImageParticleData();

        it->d->x  = randomGenerator.drawGaussian1D(x.first, x.second);
        it->d->y  = randomGenerator.drawGaussian1D(y.first, y.second);

        it->d->vx = randomGenerator.drawGaussian1D(v_x.first, v_x.second);
        it->d->vy = randomGenerator.drawGaussian1D(v_y.first, v_y.second);

        if (observation != nullptr){
            it->d->z  = depth_mat.at<float>(cvRound(it->d->y), cvRound(it->d->x));
            it->d->vz = 0;
        } else{
            it->d->z  = randomGenerator.drawGaussian1D(z.first, z.second);
            it->d->vz = randomGenerator.drawGaussian1D(v_z.first, v_z.second);;
        }

        it->log_w = 0;
    }
}

void CImageParticleFilter::get_mean(float &x, float &y, float &z, float &vx, float &vy,
                                   float &vz) const
{
    double sumW = 0;
#ifndef USE_INTEL_TBB
    for (CParticleList::iterator it = m_particles.begin(); it != m_particles.end(); it++) {
        sumW += exp(it->log_w);
    }
#else
    sumW = tbb::parallel_reduce(
        tbb::blocked_range<CParticleList::const_iterator>(m_particles.begin(), m_particles.end(),
            m_particles.size() / TBB_PARTITIONS), 0.f,
                [](const tbb::blocked_range<CParticleList::const_iterator> &r, double value) -> double {
                    return std::accumulate(r.begin(), r.end(), value,
                        [](double value, const CParticleData &p) -> double {
                            return exp(p.log_w) + value;
                        });
                },
            std::plus<double>()
        );
#endif

    ASSERT_(sumW > 0)

    x = 0;
    y = 0;
    z = 0;
    vx = 0;
    vy = 0;
    vz = 0;

    for (CParticleList::const_iterator it = m_particles.begin(); it != m_particles.end(); it++) {
        const double w = exp(it->log_w) / sumW;
        x += float(w * it->d->x);
        y += float(w * it->d->y);
        z += float(w * it->d->z);

        vx += float(w * it->d->vx);
        vy += float(w * it->d->vy);
        vz += float(w * it->d->vz);
    }
}



vector<cv::Vec3f> detect_circles(const cv::Mat &image)
{
    using namespace cv;
    Mat src_gray;
    Mat color = image.clone();
    cvtColor(image, src_gray, CV_BGR2GRAY);
    GaussianBlur(src_gray, src_gray, Size(9, 9), 2, 2);

    vector<Vec3f> circles;
    HoughCircles(src_gray, circles, CV_HOUGH_GRADIENT, 1, src_gray.rows / 8, 200, 100, 50, 0);
    return circles;
}

void TestBayesianTracking()
{
    cv::Mat color_frame;
    cv::Mat model_frame;
    cv::Mat depth_frame;

#ifdef USE_KINECT_2
    libfreenect2::Freenect2 freenect2;
    std::cout << "kinect2" << std::endl;
    libfreenect2::Freenect2Device *dev = freenect2.openDefaultDevice();

    if (dev == nullptr) {
        std::cout << "no device connected or failure opening the default one!" << std::endl;
        return;
    }

    libfreenect2::SyncMultiFrameListener listener(libfreenect2::Frame::Color |
            libfreenect2::Frame::Ir | libfreenect2::Frame::Depth);
    libfreenect2::FrameMap frames;

    dev->setColorFrameListener(&listener);
    dev->setIrAndDepthFrameListener(&listener);
    dev->start();

    std::cout << "device serial: " << dev->getSerialNumber() << std::endl;
    std::cout << "device firmware: " << dev->getFirmwareVersion() << std::endl;
#else
    capture.open(CV_CAP_OPENNI);
    capture.set(CV_CAP_OPENNI_IMAGE_GENERATOR_OUTPUT_MODE, CV_CAP_OPENNI_SXGA_15HZ);
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = [](int){ capture.release(); exit(0); };
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);
    sigaction(SIGQUIT, &sigIntHandler, NULL);
    sigaction(SIGSEGV, &sigIntHandler, NULL);

    if (!capture.isOpened()) {
        return;
    }
#endif

    randomGenerator.randomize();
    CDisplayWindow image("image");
    CDisplayWindow model_window("model");
    CDisplayWindow model_image_window("model-image");
    CDisplayWindow depth_window("depth_window");

    // Create PF
    // ----------------------
    CParticleFilter::TParticleFilterOptions PF_options;
    PF_options.adaptiveSampleSize = false;
    PF_options.PF_algorithm = CParticleFilter::pfStandardProposal;
    //PF_options.resamplingMethod = CParticleFilter::prSystematic;
    PF_options.resamplingMethod = CParticleFilter::prMultinomial;

    CParticleFilter PF;
    PF.m_options = PF_options;

    CImageParticleFilter particles;

    bool init_model = true;

    while (!mrpt::system::os::kbhit()) {
#ifdef USE_KINECT_2
        listener.waitForNewFrame(frames);
        libfreenect2::Frame *rgb = frames[libfreenect2::Frame::Color];
        libfreenect2::Frame *ir = frames[libfreenect2::Frame::Ir];
        libfreenect2::Frame *depth = frames[libfreenect2::Frame::Depth];
        color_frame = cv::Mat(rgb->height, rgb->width, CV_8UC3, rgb->data);
        depth_frame = cv::Mat(depth->height, depth->width, CV_32FC1, depth->data);
#else //kinect 1
        capture.grab();
        capture.retrieve(color_frame, CV_CAP_OPENNI_BGR_IMAGE);
        capture.retrieve(depth_frame, CV_CAP_OPENNI_DEPTH_MAP);
        /*
        capture.retrieve(grey_frame, CV_CAP_OPENNI_GRAY_IMAGE);
        capture.retrieve(disparity_map, CV_CAP_OPENNI_DISPARITY_MAP);
        capture.retrieve(depth_frame, CV_CAP_OPENNI_DEPTH_MAP);
        capture.retrieve(valid_depth_pixels, CV_CAP_OPENNI_VALID_DEPTH_MASK);
        */
#endif
        /*
        if (color_frame.empty()) {
            capture.set(CV_CAP_PROP_POS_FRAMES, 0);

        }
        */

        // Process with PF:
        CObservationImagePtr obsImage = CObservationImage::Create();
        CObservationImagePtr obsImage2 = CObservationImage::Create();
        obsImage->image.loadFromIplImage(new IplImage(color_frame));
        obsImage2->image.loadFromIplImage(new IplImage(depth_frame));

        // memory freed by SF.
        CSensoryFrame SF;
        SF.insert(obsImage);
        SF.insert(obsImage2);


        cv::Mat gradient = sobel_operator(color_frame);

        double min, max;
        cv::minMaxLoc(depth_frame, &min, &max);
        cv::Mat depth_frame_normalized = (depth_frame * 255)/ max;
        cv::Mat gradient_depth = sobel_operator(depth_frame_normalized);
        cv::Mat gradient_depth_8UC1 = cv::Mat(depth_frame.size(), CV_8UC1);

        gradient_depth.convertTo(gradient_depth_8UC1, CV_8UC1);
        CImage model_image;
        model_image.loadFromIplImage(new IplImage(gradient));
        CImage depth_image;
        depth_image.loadFromIplImage(new IplImage(gradient_depth_8UC1));
        model_window.showImage(model_image);
        depth_window.showImage(depth_image);

        if (init_model) {
            cv::Mat frame_hsv;
            auto circles = detect_circles(color_frame);
            if (circles.size()) {
                int circle_max = 0;
                double radius_max = circles[0][2];
                for (size_t i = 0; i < circles.size(); i++) {
                    cv::Point center(cvRound(circles[i][0]), cvRound(circles[i][1]));
                    int radius = cvRound(circles[i][2]);
                    cv::circle(color_frame, center, 3, cv::Scalar(0, 255, 0), -1, 8, 0);
                    cv::circle(color_frame, center, radius, cv::Scalar(0, 0, 255), 3, 8, 0);
                    if (radius_max < radius){
                        radius_max = radius;
                        circle_max = i;
                    }
                }
                cv::Point center(cvRound(circles[circle_max][0]), cvRound(circles[circle_max][1]));
                int radius = cvRound(circles[circle_max][2]);
                cout << "circle " << center.x << ' ' << center.y << ' ' << radius << endl;
                cv::cvtColor(color_frame, frame_hsv, cv::COLOR_BGR2HSV);
                const cv::Rect model_roi(center.x - radius, center.y - radius, 2 * radius, 2 * radius);
                const cv::Mat mask = create_ellipse_mask(model_roi, 1);
                const cv::Mat model = compute_color_model(frame_hsv(model_roi), mask);
                particles.update_color_model(new cv::Mat(model), radius, radius);
                particles.initializeParticles(NUM_PARTICLES, make_pair(center.x, radius), make_pair(center.y,
                                              radius), make_pair(0, 0), make_pair(0, 0), make_pair(0, 0), make_pair(0, 0), &SF);
                init_model = false;
                particles.last_time = cv::getTickCount();


                model_frame = cv::Mat(color_frame(model_roi).size(), color_frame.type());
                const cv::Mat ones = cv::Mat::ones(color_frame(model_roi).size(), color_frame(model_roi).type());
                bitwise_and(color_frame(model_roi), ones, model_frame, mask);

                //cv::Mat gradient = sobel_operator(color_frame(model_roi));
                //model_window.showImage(CImage(new IplImage(gradient)));
                CImage model_frame;
                model_frame.loadFromIplImage(new IplImage(color_frame(model_roi)));
                model_image_window.showImage(model_frame);
            }
        } else {
            // Process in the PF
            PF.executeOn(particles, NULL, &SF);

            // Show PF state:
            cout << "Particle filter ESS: " << particles.ESS() << endl;


            size_t N = particles.m_particles.size();
            for (size_t i = 0; i < N; i++) {
                particles.m_particles[i].d->x;
                particles.m_particles[i].d->y;
                cv::circle(color_frame, cv::Point(particles.m_particles[i].d->x,
                                                  particles.m_particles[i].d->y), 1, cv::Scalar(0, 0, 255), 1, 1, 0);
            }

            float avrg_x, avrg_y, avrg_z, avrg_vx, avrg_vy, avrg_vz;
            particles.get_mean(avrg_x, avrg_y, avrg_z, avrg_vx, avrg_vy, avrg_vz);
            cv::circle(color_frame, cv::Point(avrg_x, avrg_y), 20, cv::Scalar(255, 0, 0), 5, 1, 0);
            cv::line(color_frame, cv::Point(avrg_x, avrg_y), cv::Point(avrg_x + avrg_vx, avrg_y + avrg_vy),
                     cv::Scalar(0, 255, 0), 5, 1, 0);

            //particles.print_particle_state();
            std::cout << "MEAN " << avrg_x << ' ' << avrg_y << ' ' << avrg_z << ' ' << avrg_vx << ' ' << avrg_vy << ' ' << avrg_vz << std::endl;
        }

        CImage frame_particles;
        frame_particles.loadFromIplImage(new IplImage(color_frame));
        image.showImage(frame_particles);
#ifdef USE_KINECT_2
        listener.release(frames);
#endif
    }
}


int main(int argc, char *argv[])
{
    if (argc > 1){
        NUM_PARTICLES = atof(argv[1]);
    }else{
        NUM_PARTICLES = 1000;
    }

    if (argc > 2){
        TRANSITION_MODEL_STD_XY = atof(argv[2]);
    }else{
        TRANSITION_MODEL_STD_XY = 10;
    }

    if (argc > 3){
        TRANSITION_MODEL_STD_VXY  = atof(argv[3]);
    }else{
        TRANSITION_MODEL_STD_VXY  = 10;
    }

    std::cout << "NUM_PARTICLES: " << NUM_PARTICLES << " TRANSITION_MODEL_STD_XY: " << TRANSITION_MODEL_STD_XY << " TRANSITION_MODEL_STD_VXY: " << TRANSITION_MODEL_STD_VXY << std::endl;
    TestBayesianTracking();

    return 0;

    /*
    try {
        TestBayesianTracking();
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
