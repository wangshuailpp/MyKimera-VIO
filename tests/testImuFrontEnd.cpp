/* ----------------------------------------------------------------------------
 * Copyright 2017, Massachusetts Institute of Technology,
 * Cambridge, MA 02139
 * All Rights Reserved
 * Authors: Luca Carlone, et al. (see THANKS for the full author list)
 * See LICENSE for the license information
 * -------------------------------------------------------------------------- */

/**
 * @file   testImuFrontEnd.cpp
 * @brief  Unit tests ImuFrontEnd class' functionality.
 * @author Antoni Rosinol
 */

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "imu-frontend/ImuFrontEnd-definitions.h"
#include "imu-frontend/ImuFrontEnd.h"
#include "imu-frontend/ImuFrontEndParams.h"
#include "utils/ThreadsafeImuBuffer.h"

namespace VIO {

/* -------------------------------------------------------------------------- */
TEST(ImuFrontEnd, ImuFrontEndInitialization) {
  // Check that IMU frontend instantiation works.
  ImuParams imu_params;
  imu_params.acc_walk_ = 1.0;
  imu_params.acc_noise_ = 1.0;
  imu_params.gyro_walk_ = 1.0;
  imu_params.gyro_noise_ = 1.0;
  imu_params.n_gravity_ << 1.0, 1.0, 1.0;
  imu_params.imu_integration_sigma_ = 1.0;
  Vector3 bias_acc(1.0, 1.0, 1.0);
  Vector3 bias_gyr(1.0, 1.0, 1.0);
  ImuBias imu_bias(bias_acc, bias_gyr);
  ImuFrontEnd imu_frontend(imu_params, imu_bias);
  EXPECT_TRUE(imu_frontend.getCurrentImuBias().equals(imu_bias));
  EXPECT_TRUE(imu_frontend.getCurrentPIM().equals(
      ImuFrontEnd::PreintegratedImuMeasurements(
          boost::make_shared<ImuFrontEnd::PreintegratedImuMeasurements::Params>(
              imu_frontend.getImuParams()),
          imu_bias)));
}

/* -------------------------------------------------------------------------- */
TEST(ImuFrontEnd, UpdateBias) {
  // Check that IMU frontend bias update works.
  ImuParams imu_params;
  imu_params.acc_walk_ = 1.0;
  imu_params.acc_noise_ = 1.0;
  imu_params.gyro_walk_ = 1.0;
  imu_params.gyro_noise_ = 1.0;
  imu_params.n_gravity_ << 1.0, 1.0, 1.0;
  imu_params.imu_integration_sigma_ = 1.0;
  Vector3 bias_acc(1.0, 1.0, 1.0);
  Vector3 bias_gyr(1.0, 1.0, 1.0);
  ImuBias imu_bias(bias_acc, bias_gyr);
  ImuFrontEnd imu_frontend(imu_params, imu_bias);
  // Do some random change to the bias.
  ImuBias updated_imu_bias = -imu_bias;
  imu_frontend.updateBias(updated_imu_bias);
  EXPECT_TRUE(imu_frontend.getCurrentImuBias().equals(updated_imu_bias));
  // Do some random composition with itself.
  updated_imu_bias = imu_bias.compose(imu_bias);
  imu_frontend.updateBias(updated_imu_bias);
  EXPECT_TRUE(imu_frontend.getCurrentImuBias().equals(updated_imu_bias));
  // Check that the updated bias does not reset pim!
  auto pim = imu_frontend.getCurrentPIM();
  EXPECT_TRUE(pim.biasHat().equals(imu_bias));
  EXPECT_TRUE(!pim.biasHat().equals(updated_imu_bias));
}

/* -------------------------------------------------------------------------- */
TEST(ImuFrontEnd, UpdateBiasThreadSafe) {
  // Check that IMU frontend bias update works.
  ImuParams imu_params;
  imu_params.acc_walk_ = 1.0;
  imu_params.acc_noise_ = 1.0;
  imu_params.gyro_walk_ = 1.0;
  imu_params.gyro_noise_ = 1.0;
  imu_params.n_gravity_ << 1.0, 1.0, 1.0;
  imu_params.imu_integration_sigma_ = 1.0;
  Vector3 bias_acc(1.0, 1.0, 1.0);
  Vector3 bias_gyr(1.0, 1.0, 1.0);
  ImuBias imu_bias(bias_acc, bias_gyr);
  ImuFrontEnd imu_frontend(imu_params, imu_bias);
  // Change and log imu_bias by a pool of threads.
  // vector container stores threads
  std::vector<std::thread> workers;
  static constexpr int number_of_threads = 5;
  for (int i = 0; i < number_of_threads; i++) {
    workers.push_back(std::thread([&imu_frontend, i]() {
      Vector3 bias_acc(1.0 + i, 1.0, 1.0 * i);
      Vector3 bias_gyr(0.0 + i, 0.0, 1.0 * i);
      imu_frontend.updateBias(ImuBias(bias_acc, bias_gyr));
      auto curr_imu_bias = imu_frontend.getCurrentImuBias();
      // If there is a race condition, this might not hold.
      ASSERT_NEAR(curr_imu_bias.gyroscope().x(),
                  curr_imu_bias.accelerometer().x() - 1.0, 0.1);
      ASSERT_NEAR(curr_imu_bias.gyroscope().y(),
                  curr_imu_bias.accelerometer().y() - 1.0, 0.1);
      ASSERT_NEAR(curr_imu_bias.gyroscope().z(),
                  curr_imu_bias.accelerometer().z(), 0.1);
      // Do random things that lock the bias as well.
      imu_frontend.resetIntegrationWithCachedBias();
    }));
  }

  // Looping every thread via for_each
  // The 3rd argument assigns a task
  // It tells the compiler we're using lambda ([])
  // The lambda function takes its argument as a reference to a thread, t
  // Then, joins one by one, and this works like barrier
  EXPECT_NO_THROW(std::for_each(workers.begin(), workers.end(),
                                [](std::thread &t) { t.join(); }););
}

/* -------------------------------------------------------------------------- */
TEST(ImuFrontEnd, ResetPreintegration) {
  // Check that IMU frontend reset preintegration works.
  ImuParams imu_params;
  imu_params.acc_walk_ = 1.0;
  imu_params.acc_noise_ = 1.0;
  imu_params.gyro_walk_ = 1.0;
  imu_params.gyro_noise_ = 1.0;
  imu_params.n_gravity_ << 1.0, 1.0, 1.0;
  imu_params.imu_integration_sigma_ = 1.0;
  Vector3 bias_acc(1.0, 1.0, 1.0);
  Vector3 bias_gyr(1.0, 1.0, 1.0);
  ImuBias imu_bias(bias_acc, bias_gyr);
  ImuFrontEnd imu_frontend(imu_params, imu_bias);
  // If we do not update the bias, both PIMs should be the same.
  auto curr_pim = imu_frontend.getCurrentPIM();
  imu_frontend.resetIntegrationWithCachedBias();
  auto reseted_pim = imu_frontend.getCurrentPIM();
  EXPECT_TRUE(reseted_pim.equals(curr_pim));
  // If we update the biases, the new PIM should reflect that.
  ImuBias updated_imu_bias = imu_bias.compose(imu_bias);  // Random composition.
  imu_frontend.updateBias(updated_imu_bias);
  imu_frontend.resetIntegrationWithCachedBias();
  reseted_pim = imu_frontend.getCurrentPIM();
  EXPECT_TRUE(reseted_pim.biasHat().equals(updated_imu_bias));
  // Also expect that it is not the same as the previous pim.
  EXPECT_TRUE(!reseted_pim.equals(curr_pim));
}

/* TODO(Toni): tests left:
TEST(ImuFrontEnd, PreintegrateEmptyImuData) {
}
TEST(ImuFrontEnd, PreintegrateMinimalImuData) {
}
TEST(ImuFrontEnd, PreintegrateTypicalImuData) {
}
TEST(ImuFrontEnd, PreintegrateIncrementally) {
}
TEST(ImuFrontEnd, ForwardPreintegration) {
}
TEST(ImuFrontEnd, BackwardPreintegration) {
}
*/

}  // namespace VIO
