/* ----------------------------------------------------------------------------
 * Copyright 2017, Massachusetts Institute of Technology,
 * Cambridge, MA 02139
 * All Rights Reserved
 * Authors: Luca Carlone, et al. (see THANKS for the full author list)
 * See LICENSE for the license information
 * -------------------------------------------------------------------------- */

/**
 * @file   InitializationBackEnd.cpp
 * @brief  Derived class from VioBackEnd for bundle adjustment and alignment
 * for the online initialization
 * @author Antoni Rosinol
 * @author Sandro Berchier
 * @author Luca Carlone
 */

#include "InitializationBackEnd.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "initial/OnlineGravityAlignment.h"

namespace VIO {

/* -------------------------------------------------------------------------- */
InitializationBackEnd::InitializationBackEnd(
    const gtsam::Pose3 &leftCamPose,
    const Cal3_S2 &leftCameraCalRectified,
    const double &baseline,
    const VioBackEndParams &vioParams,
    const bool log_output)
    : VioBackEnd(leftCamPose,
                 leftCameraCalRectified,
                 baseline,
                 vioParams,
                 log_output) {}

/* ------------------------------------------------------------------------ */
// Perform Bundle-Adjustment and initial gravity alignment
bool InitializationBackEnd::bundleAdjustmentAndGravityAlignment(
    std::queue<InitializationInputPayload> &output_frontend,
    gtsam::Vector3 *gyro_bias,
    gtsam::Vector3 *g_iter_b0,
    gtsam::NavState *init_navstate) {
  // Logging
  VLOG(10) << "N frames for initial alignment: " << output_frontend.size();
  // Create inputs for backend
  std::vector<std::shared_ptr<VioBackEndInputPayload>> inputs_backend;

  // Create inputs for online gravity alignment
  std::vector<gtsam::PreintegratedImuMeasurements> pims;
  std::vector<double> delta_t_camera;
  // Iterate and fill backend input vector
  while (!output_frontend.empty()) {
    // Create input for backend
    std::shared_ptr<VioBackEndInputPayload> input_backend =
        std::make_shared<VioBackEndInputPayload>(VioBackEndInputPayload(
            output_frontend.front().stereo_frame_lkf_.getTimestamp(),
            output_frontend.front().statusSmartStereoMeasurements_,
            output_frontend.front().tracker_status_,
            output_frontend.front().pim_,
            output_frontend.front().relative_pose_body_stereo_,
            nullptr));
    inputs_backend.push_back(input_backend);
    pims.push_back(output_frontend.front().pim_);
    // Bookkeeping for timestamps
    Timestamp timestamp_kf =
        output_frontend.front().stereo_frame_lkf_.getTimestamp();
    delta_t_camera.push_back(
        UtilsOpenCV::NsecToSec(timestamp_kf - timestamp_lkf_));
    timestamp_lkf_ = timestamp_kf;

    // Check that all frames are keyframes (required)
    CHECK(output_frontend.front().is_keyframe_);
    // Pop from queue
    output_frontend.pop();
  }

  // TODO(Sandro): Bundle-Adjustment is not super robust and accurate!!!
  // Run initial Bundle Adjustment and retrieve body poses
  // wrt. to initial body frame (b0_T_bk, for k in 0:N).
  // The first pim and ransac poses are lost, as in the Bundle
  // Adjustment we need observations of landmarks intra-frames.
  auto tic_ba = utils::Timer::tic();
  std::vector<gtsam::Pose3> estimated_poses =
      addInitialVisualStatesAndOptimize(inputs_backend);
  auto ba_duration =
      utils::Timer::toc<std::chrono::nanoseconds>(tic_ba).count() * 1e-9;
  LOG(WARNING) << "Current bundle-adjustment duration: (" << ba_duration
               << " s).";
  // Remove initial delta time and pims from input vector to online
  // alignment due to the disregarded init values in bundle adjustment
  delta_t_camera.erase(delta_t_camera.begin());
  pims.erase(pims.begin());
  // Logging
  LOG(INFO) << "Initial bundle adjustment terminated.";

  // Run initial visual-inertial alignment(OGA)
  OnlineGravityAlignment initial_alignment(
      estimated_poses, delta_t_camera, pims, vio_params_.n_gravity_);
  auto tic_oga = utils::Timer::tic();
  bool is_success = initial_alignment.alignVisualInertialEstimates(
      gyro_bias, g_iter_b0, init_navstate, true);
  auto alignment_duration =
      utils::Timer::toc<std::chrono::nanoseconds>(tic_oga).count() * 1e-9;
  LOG(WARNING) << "Current alignment duration: (" << alignment_duration
               << " s).";

  // TODO(Sandro): Check initialization against GT
  // Compute performance and Log output if requested
  /*if (FLAGS_log_output && is_success) {
    std::string reason = "BA performance comparison";
    ETHDatasetParser gt_dataset(reason);
    std::vector<Timestamp> timestamps;
    for (int i = 0; i < output_frontend.size(); i++) {
      timestamps.push_back(
          output_frontend.at(i)->stereo_frame_lkf_.getTimestamp());
    }
    gt_dataset.parseGTdata("/home/sb/Dataset/EuRoC/V1_01_gt", "gt");
    const gtsam::NavState init_navstate_pass = *init_navstate;
    const gtsam::Vector3 gravity_iter_pass = *g_iter_b0;
    const gtsam::Vector3 gyro_bias_pass = *gyro_bias;
    logger_.logInitializationResultsCSV(
          gt_dataset.getInitializationPerformance(
            timestamps,
            estimated_poses,
            gtNavState(init_navstate_pass.pose(),
              init_navstate_pass.pose().rotation().transpose() *
                init_navstate_pass.velocity(),
              gtsam::imuBias::ConstantBias(gtsam::Vector3(),
                                          gyro_bias_pass)),
            gravity_iter_pass),
            ba_duration,
            alignment_duration,
            is_success);
  } */
  ////////////////// (Remove)

  return is_success;
}

/* -------------------------------------------------------------------------- */
std::vector<gtsam::Pose3>
InitializationBackEnd::addInitialVisualStatesAndOptimize(
    const std::vector<std::shared_ptr<VioBackEndInputPayload>> &input) {
  CHECK(input.front());

  // Initial clear values.
  new_values_.clear();
  // Initialize to trivial pose for initial bundle adjustment
  W_Pose_B_lkf_ = gtsam::Pose3();

  // Insert relative poses for bundle adjustment
  for (int i = 0; i < input.size(); i++) {
    auto input_iter = input[i];
    bool use_stereo_btw_factor =
        vio_params_.addBetweenStereoFactors_ == true &&
        input_iter->stereo_tracking_status_ == TrackingStatus::VALID;
    VLOG(5) << "Adding initial visual state.";
    VLOG_IF(5, use_stereo_btw_factor) << "Using stereo between factor.";
    // Features and IMU line up --> do iSAM update
    addInitialVisualState(
        input_iter->timestamp_kf_nsec_,  // Current time for fixed lag smoother.
        input_iter->status_smart_stereo_measurements_kf_,  // Vision data.
        input_iter->planes_,
        use_stereo_btw_factor ? input_iter->stereo_ransac_body_pose_
                              : boost::none,
        0);
    last_kf_id_ = curr_kf_id_;
    ++curr_kf_id_;
  }

  VLOG(10) << "Initialisation states added.";

  // Add all landmarks to factor graph
  LandmarkIds landmarks_all_keyframes;
  BOOST_FOREACH (auto keyTrack_j, feature_tracks_)
    landmarks_all_keyframes.push_back(keyTrack_j.first);

  addLandmarksToGraph(landmarks_all_keyframes);

  VLOG(10) << "Initialisation landmarks added.";

  // Perform Bundle Adjustment and retrieve body poses (b0_T_bk)
  // b0 is the initial body frame
  std::vector<gtsam::Pose3> estimated_poses = optimizeInitialVisualStates(
      input.front()->timestamp_kf_nsec_, curr_kf_id_, vio_params_.numOptimize_);

  VLOG(10) << "Initial bundle adjustment completed.";

  // All relative to initial pose, as we need to fix x0 from the BA.
  gtsam::Pose3 initial_pose;
  for (int j = 0; j < estimated_poses.size(); j++) {
    if (j == 0) {
      initial_pose = estimated_poses.at(0);
      estimated_poses.at(0) = gtsam::Pose3();
    } else {
      estimated_poses.at(j) = initial_pose.between(estimated_poses.at(j));
    }
    if (VLOG_IS_ON(10)) estimated_poses.at(j).print();
  }
  // Return poses (b0_T_bk, for k in 0:N).
  // Since we need to optimize for poses with observed landmarks, the
  // ransac estimate for the first pose is not used, as there are no
  // observations for the previous keyframe (which doesn't exist).
  CHECK_EQ(input.size(), estimated_poses.size());
  return estimated_poses;
}

/* -------------------------------------------------------------------------- */
// Adding of states for bundle adjustment used in initialization.
// [in] timestamp_kf_nsec, keyframe timestamp.
// [in] status_smart_stereo_measurements_kf, vision data.
// [in] stereo_ransac_body_pose, inertial data.
void InitializationBackEnd::addInitialVisualState(
    const Timestamp &timestamp_kf_nsec,
    const StatusSmartStereoMeasurements &status_smart_stereo_measurements_kf,
    std::vector<Plane> *planes,
    boost::optional<gtsam::Pose3> stereo_ransac_body_pose,
    const int verbosity_ = 0) {
  debug_info_.resetAddedFactorsStatistics();

  VLOG(10) << "Initialization: adding keyframe " << curr_kf_id_
           << " at timestamp:" << UtilsOpenCV::NsecToSec(timestamp_kf_nsec)
           << " (nsec).";

  /////////////////// MANAGE IMU MEASUREMENTS ///////////////////////////
  // Predict next step, add initial guess
  if (stereo_ransac_body_pose && curr_kf_id_ != 0) {
    // We need to keep adding the relative poses, since we process a
    // whole batch. Otherwise we start with wrong initial guesses.
    W_Pose_B_lkf_ = W_Pose_B_lkf_.compose(*stereo_ransac_body_pose);
    new_values_.insert(gtsam::Symbol('x', curr_kf_id_), W_Pose_B_lkf_);
  } else {
    new_values_.insert(gtsam::Symbol('x', curr_kf_id_), W_Pose_B_lkf_);
  }

  // add between factor from RANSAC
  if (stereo_ransac_body_pose && curr_kf_id_ != 0) {
    VLOG(10) << "Initialization: adding between ";
    if (VLOG_IS_ON(10)) (*stereo_ransac_body_pose).print();
    addBetweenFactor(last_kf_id_, curr_kf_id_, *stereo_ransac_body_pose);
  }

  /////////////////// MANAGE VISION MEASUREMENTS ///////////////////////////
  SmartStereoMeasurements smartStereoMeasurements_kf =
      status_smart_stereo_measurements_kf.second;

  // if stereo ransac failed, remove all right pixels:
  TrackingStatus kfTrackingStatus_stereo =
      status_smart_stereo_measurements_kf.first.kfTrackingStatus_stereo_;

  // extract relevant information from stereo frame
  LandmarkIds landmarks_kf;
  addStereoMeasurementsToFeatureTracks(
      curr_kf_id_, smartStereoMeasurements_kf, &landmarks_kf);

  // Add zero velocity update if no-motion detected
  TrackingStatus kfTrackingStatus_mono =
      status_smart_stereo_measurements_kf.first.kfTrackingStatus_mono_;
  if (kfTrackingStatus_mono == TrackingStatus::LOW_DISPARITY &&
      curr_kf_id_ != 0) {
    VLOG(10) << "No-motion factor added in Bundle-Adjustment.\n";
    LOG(WARNING) << "No-motion factor added in Bundle-Adjustment.\n";
    addNoMotionFactor(last_kf_id_, curr_kf_id_);
  }

  if (verbosity_ >= 8) {
    printFeatureTracks();
  }
}

/* -------------------------------------------------------------------------- */
// TODO(Toni): do not return vectors...
std::vector<gtsam::Pose3> InitializationBackEnd::optimizeInitialVisualStates(
    const Timestamp &timestamp_kf_nsec,
    const FrameId &cur_id,
    const size_t &max_extra_iterations,
    const std::vector<size_t> &extra_factor_slots_to_delete,
    const int verbosity_) {  // TODO: Remove verbosity and use VLOG

  // Only for statistics and debugging.
  // Store start time to calculate absolute total time taken.
  const auto &total_start_time = utils::Timer::tic();
  // Store start time to calculate per module total time.
  auto start_time = total_start_time;
  // Reset all timing info.
  debug_info_.resetTimes();

  // Create and fill non-linear graph
  gtsam::NonlinearFactorGraph new_factors_tmp;
  for (const auto &new_smart_factor : new_smart_factors_) {
    // Push back the smart factor to the list of new factors to add to the
    // graph.
    new_factors_tmp.push_back(
        new_smart_factor.second);  // Smart factor, so same address right?
    // VLOG(10) << "Iteration: " << new_smart_factor.first;
  }

  // Add also other factors (imu, priors).
  // SMART FACTORS MUST BE FIRST, otherwise when recovering the slots
  // for the smart factors we will mess up.
  new_factors_tmp.push_back(new_imu_prior_and_other_factors_.begin(),
                            new_imu_prior_and_other_factors_.end());

  // Print graph before optimization
  // TODO(Sandro): Change back verbosity level!
  if (VLOG_IS_ON(2)) new_factors_tmp.print();

  // Levenberg-Marquardt optimization
  gtsam::LevenbergMarquardtParams lmParams;
  gtsam::LevenbergMarquardtOptimizer initial_bundle_adjustment(
      new_factors_tmp, new_values_, lmParams);
  VLOG(10) << "LM optimizer created with error: "
           << initial_bundle_adjustment.error();

  // Optimize and get values
  gtsam::Values initial_values = initial_bundle_adjustment.optimize();
  VLOG(10) << "Levenberg Marquardt optimizer done.";
  // Query optimized poses in body frame (b0_T_bk)
  std::vector<gtsam::Pose3> initial_states;
  BOOST_FOREACH (const gtsam::Values::ConstKeyValuePair &key_value,
                 initial_values) {
    initial_states.push_back(initial_values.at<gtsam::Pose3>(key_value.key));
  }
  VLOG(10) << "Initialization values retrieved.";

  // TODO(Sandro): Implement check on initial and final covariance
  // Quality check on Bundle-Adjustment
  LOG(INFO) << "Initial states retrieved.\n";
  /*std::vector<gtsam::Matrix> initial_covariances;
  gtsam::Marginals marginals(new_factors_tmp, initial_values,
      gtsam::Marginals::Factorization::QR);
  //  gtsam::Marginals::Factorization::CHOLESKY);
  initial_covariances.push_back(
          marginals.marginalCovariance(gtsam::Symbol('x', 0)));
  //initial_covariances.push_back(
  //        marginals.marginalCovariance(gtsam::Symbol('x', curr_kf_id_)));
  LOG(INFO) << "Initial state covariance: (x, y, z)\n"
            << gtsam::sub(initial_covariances.at(0), 3, 6, 3, 6);
  //CHECK(gtsam::assert_equal(initial_covariance, final_covariance, 1e-2));
  */

  /////////////////////////// BOOKKEEPING //////////////////////////////////////

  // Reset everything for next round.
  VLOG(10) << "Clearing new_smart_factors_!";
  new_smart_factors_.clear();
  old_smart_factors_.clear();
  // Reset list of new imu, prior and other factors to be added.
  // TODO could this be used to check whether we are repeating factors?
  new_imu_prior_and_other_factors_.resize(0);
  // Clear values.
  new_values_.clear();

  return initial_states;
}

}  // namespace VIO
