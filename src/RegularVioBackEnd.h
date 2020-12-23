/* ----------------------------------------------------------------------------
 * Copyright 2017, Massachusetts Institute of Technology,
 * Cambridge, MA 02139
 * All Rights Reserved
 * Authors: Luca Carlone, et al. (see THANKS for the full author list)
 * See LICENSE for the license information
 * -------------------------------------------------------------------------- */

/**
 * @file   RegularVioBackEnd.h
 * @brief  Derived class from VioBackEnd which enforces regularity constraints
 * on the factor graph.
 *
 * A. Rosinol, T. Sattler, M. Pollefeys, and L. Carlone. Incremental
 * Visual-Inertial 3D Mesh Generation with Structural Regularities. IEEE Intl.
 * Conf. on Robotics and Automation (ICRA), 2019
 *
 * @author Toni Rosinol
 */

#pragma once

#include <gtsam/slam/StereoFactor.h>

#include "VioBackEnd.h"
#include "RegularVioBackEndParams.h"

namespace VIO {

class RegularVioBackEnd: public VioBackEnd {
public:

  /* ------------------------------------------------------------------------ */
  // Defines the behaviour of this backend.
  enum class BackendModality {
    STRUCTURELESS = 0, // Only use structureless factors, equiv to normal Vio.
    PROJECTION = 1, // Converts all structureless factors to projection factors.
    STRUCTURELESS_AND_PROJECTION = 2, // Projection factors used for regularities.
    PROJECTION_AND_REGULARITY = 3, // Projection Vio + regularity factors.
    STRUCTURELESS_PROJECTION_AND_REGULARITY = 4 // All types of factors used.
  };

  /* ------------------------------------------------------------------------ */
  RegularVioBackEnd(
      const Pose3& leftCamPose,
      const Cal3_S2& leftCameraCalRectified,
      const double& baseline,
      const VioNavState& initial_state_seed,
      const Timestamp& timestamp,
      const VioBackEndParams& vioParams = VioBackEndParams(),
      const bool& log_timing = false,
      const BackendModality& backend_modality =
          BackendModality::STRUCTURELESS_PROJECTION_AND_REGULARITY);

  /* ------------------------------------------------------------------------ */
  ~RegularVioBackEnd() = default;

public:
  /* ------------------------------------------------------------------------ */
  virtual void addVisualInertialStateAndOptimize(
      const Timestamp& timestamp_kf_nsec,
      const StatusSmartStereoMeasurements& status_smart_stereo_measurements_kf,
      const gtsam::PreintegratedImuMeasurements& pim,
      std::vector<Plane>* planes = nullptr,
      boost::optional<gtsam::Pose3> stereo_ransac_body_pose = boost::none);

private:
  typedef size_t Slot;

  // Type of handled regularities.
  enum class RegularityType{
    POINT_PLANE
  };

  using GenericProjectionFactor = gtsam::GenericStereoFactor<Pose3, Point3>;
  // Map from lmk ID to corresponding factor type, true: smart.
  using LmkIdIsSmart = gtsam::FastMap<LandmarkId, bool>;

  /// Members
  // Decides which kind of functionality the backend exhibits.
  const BackendModality backend_modality_;
  LmkIdIsSmart lmk_id_is_smart_; // TODO GROWS UNBOUNDED, use the loop in getMapLmkIdsTo3dPointsInTimeHorizon();
  typedef std::map<LandmarkId, RegularityType> LmkIdToRegularityTypeMap;
  typedef std::map<PlaneId, LmkIdToRegularityTypeMap> PlaneIdToLmkIdRegType;
  PlaneIdToLmkIdRegType plane_id_to_lmk_id_reg_type_;
  gtsam::FactorIndices delete_slots_of_converted_smart_factors_;

  // For Stereo and Projection factors.
  gtsam::SharedNoiseModel stereo_noise_;
  gtsam::SharedNoiseModel mono_noise_;
  boost::shared_ptr<Cal3_S2> mono_cal_;

  // For regularity factors.
  gtsam::SharedNoiseModel point_plane_regularity_noise_;

  // RAW parameters given by the user for the regulaVIO backend.
  const RegularVioBackEndParams regular_vio_params_;

private:
  /* ------------------------------------------------------------------------ */
  void addLandmarksToGraph(const LandmarkIds& lmks_kf,
                           const LandmarkIds& lmk_ids_with_regularity);

  /* ------------------------------------------------------------------------ */
  void addLandmarkToGraph(const LandmarkId& lm_id,
                          const FeatureTrack& lm,
                          LmkIdIsSmart* lmk_id_is_smart);

  /* ------------------------------------------------------------------------ */
  void updateLandmarkInGraph(const LandmarkId& lmk_id,
                             const bool& is_lmk_smart,
                             const std::pair<FrameId, StereoPoint2>& new_obs);

  /* ------------------------------------------------------------------------ */
  bool updateLmkIdIsSmart(const LandmarkId& lmk_id,
                       const LandmarkIds& lmk_ids_with_regularity,
                       LmkIdIsSmart* lmk_id_is_smart);

  /* ------------------------------------------------------------------------ */
  bool isSmartFactor3dPointGood(SmartStereoFactor::shared_ptr factor,
                                 const size_t &min_num_of_observations);

  /* ------------------------------------------------------------------------ */
  void updateExistingSmartFactor(const LandmarkId& lmk_id,
                                 const std::pair<FrameId, StereoPoint2>& new_obs,
                                 LandmarkIdSmartFactorMap* new_smart_factors,
                                 SmartFactorMap* old_smart_factors);
  /* ------------------------------------------------------------------------ */
  bool convertSmartToProjectionFactor(
      const LandmarkId& lmk_id,
      LandmarkIdSmartFactorMap* new_smart_factors,
      SmartFactorMap* old_smart_factors,
      gtsam::Values* new_values,
      gtsam::NonlinearFactorGraph* new_imu_prior_and_other_factors,
      gtsam::FactorIndices* delete_slots_of_converted_smart_factors);

  /* ------------------------------------------------------------------------ */
  void convertExtraSmartFactorToProjFactor(
      const LandmarkIds& lmk_ids_with_regularity);

  /* ------------------------------------------------------------------------ */
  virtual void deleteLmkFromExtraStructures(const LandmarkId& lmk_id);

  /* ------------------------------------------------------------------------ */
  void addProjectionFactor(
      const LandmarkId& lmk_id,
      const std::pair<FrameId, StereoPoint2>& new_obs,
      gtsam::NonlinearFactorGraph* new_imu_prior_and_other_factors);

  /* ------------------------------------------------------------------------ */
  void addRegularityFactors(
      const Plane& plane,
      LmkIdToRegularityTypeMap* lmk_id_to_regularity_type_map,
      std::vector<std::pair<Slot, LandmarkId>>* idx_of_point_plane_factors_to_add);

  /* ------------------------------------------------------------------------ */
  void removeOldRegularityFactors_Slow(
    const std::vector<Plane>& planes,
    const std::map<PlaneId, std::vector<std::pair<Slot, LandmarkId>>>&
    map_idx_of_point_plane_factors_to_add,
    PlaneIdToLmkIdRegType* plane_id_to_lmk_id_to_regularity_type_map,
    gtsam::FactorIndices* delete_slots);

  /* ------------------------------------------------------------------------ */
  void fillDeleteSlots(
      const std::vector<std::pair<Slot, LandmarkId>>& point_plane_factor_slots,
      LmkIdToRegularityTypeMap* lmk_id_to_regularity_type_map,
      gtsam::FactorIndices* delete_slots);

  /* ------------------------------------------------------------------------ */
  // Remove as well the factors that are going to be added in this iteration.
  void deleteNewSlots(
      const PlaneId& plane_key,
      const std::vector<std::pair<Slot, LandmarkId>>& idx_of_point_plane_factors_to_add,
      LmkIdToRegularityTypeMap* lmk_id_to_regularity_type_map,
      gtsam::NonlinearFactorGraph* new_imu_prior_and_other_factors_);

  /* ------------------------------------------------------------------------ */
  // Output a noise model with a selected norm type:
  // norm_type = 0: l-2.
  // norm_type = 1: Huber.
  // norm_type = 2: Tukey.
  void selectNormType(gtsam::SharedNoiseModel* noise_model_output,
      const gtsam::SharedNoiseModel& noise_model_input,
      const size_t& norm_type,
      const double& norm_type_parameter);

  /* ------------------------------------------------------------------------ */
  // Extract all lmk ids, wo repetition, from the set of planes.
  void extractLmkIdsFromPlanes(const std::vector<Plane>& planes,
                               LandmarkIds* lmk_ids_with_regularity) const;

  /* ------------------------------------------------------------------------ */
  // Update plane normal and distance if the plane could be found in the state.
  // Otherwise, erase the plane.
  void updatePlaneEstimates(std::vector<Plane>* planes);

};

} // namespace VIO
