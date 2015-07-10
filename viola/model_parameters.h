#pragma once

double MODEL_TRANSITION_STD_XY   = 0;
double MODEL_TRANSITION_STD_VXY  = 0;
double MODEL_MISSING_UNCERTAINCY_MULTIPLER = 1.2;

int MISSING_TIMES_THRESHOLD = 10;

double NUM_PARTICLES             = 0;

constexpr float LIKEHOOD_FOUND  = 0.3;
constexpr float LIKEHOOD_UPDATE = 0.9;

constexpr float ELLIPSE_FITTING_ANGLE_STEP = 2;



// PERSON MODEL
constexpr float PERSON_TORSO_X_AXIS_METTERS = 0.30;
constexpr float PERSON_TORSO_Y_AXIS_METTERS = 0.20;

constexpr float PERSON_HEAD_X_AXIS_METTERS = 0.15;
constexpr float PERSON_HEAD_Y_AXIS_METTERS = 0.25;

constexpr float PERSON_TORSO_X_SEMIAXIS_METTERS = PERSON_TORSO_X_AXIS_METTERS * 0.5;
constexpr float PERSON_TORSO_Y_SEMIAXIS_METTERS = PERSON_TORSO_Y_AXIS_METTERS * 0.5;

constexpr float PERSON_HEAD_X_SEMIAXIS_METTERS = PERSON_HEAD_X_AXIS_METTERS * 0.5;
constexpr float PERSON_HEAD_Y_SEMIAXIS_METTERS = PERSON_HEAD_Y_AXIS_METTERS * 0.5;

constexpr float PERSON_TORSO_HEAD_DISTANCE_METTERS = 0.30;

const Eigen::Vector3f HEAD_TO_TORSE_CENTER_VECTOR = Eigen::Vector3f(0, PERSON_TORSO_HEAD_DISTANCE_METTERS, 0);

// CD MODEL
//constexpr float MODEL_AXIS_X_METTERS = 0.12;
//constexpr float MODEL_AXIS_Y_METTERS = 0.12;

constexpr float MODEL_AXIS_X_METTERS = PERSON_HEAD_X_AXIS_METTERS;
constexpr float MODEL_AXIS_Y_METTERS = PERSON_HEAD_Y_AXIS_METTERS;

constexpr float MODEL_SEMIAXIS_X_METTERS = MODEL_AXIS_X_METTERS * 0.5;
constexpr float MODEL_SEMIAXIS_Y_METTERS = MODEL_AXIS_Y_METTERS * 0.5;

constexpr float DEPTH_SIGMA = 0.4;

using DEPTH_TYPE = uint16_t;
