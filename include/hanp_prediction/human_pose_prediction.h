/*/
 * Copyright (c) 2015 LAAS/CNRS
 * All rights reserved.
 *
 * Redistribution and use  in source  and binary  forms,  with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *                                  Harmish Khambhaita on Sat Sep 12 2015
 */

#ifndef HUMAN_POSE_PREDICTION_H_
#define HUMAN_POSE_PREDICTION_H_

#include <ros/ros.h>

#include <dynamic_reconfigure/server.h>
#include <hanp_prediction/HumanPosePredictionConfig.h>

#include <hanp_msgs/TrackedHumans.h>
#include <hanp_msgs/TrackedSegmentType.h>
#include <hanp_msgs/HumanPathArray.h>
#include <hanp_prediction/HumanPosePredict.h>
#include <tf/transform_listener.h>
#include <std_srvs/SetBool.h>
#include <std_srvs/Empty.h>

#include <visualization_msgs/MarkerArray.h>

namespace hanp_prediction {
class HumanPosePrediction {
public:
  HumanPosePrediction();
  ~HumanPosePrediction();

  void initialize();

  void setParams(std::vector<double> velscale_scales, double velscale_angle,
                 double velscale_mul, double velobs_mul, double velobs_min_rad,
                 double velobs_max_rad, double velobs_max_rad_time);

private:
  // ros subscribers and publishers
  ros::Subscriber tracked_humans_sub_, external_paths_sub_;
  ros::Publisher predicted_humans_pub_;

  // ros services
  ros::ServiceServer predict_humans_server_, reset_ext_paths_server_,
      publish_markers_srv_;

  // dynamic reconfigure variables
  dynamic_reconfigure::Server<HumanPosePredictionConfig> *dsrv_;
  void reconfigureCB(HumanPosePredictionConfig &config, uint32_t level);

  // subscriber callbacks
  void trackedHumansCB(const hanp_msgs::TrackedHumans &tracked_humans);
  void
  externalPathsCB(const hanp_msgs::HumanPathArray::ConstPtr &external_paths);

  tf::TransformListener tf_;

  std::string tracked_humans_sub_topic_, external_paths_sub_topic_,
      reset_ext_paths_service_name_, predict_service_name_,
      predicted_humans_markers_pub_topic_, publish_markers_srv_name_;
  int default_human_part_;
  bool publish_markers_, showing_markers_, got_new_human_paths_;

  hanp_msgs::TrackedHumans tracked_humans_;
  hanp_msgs::HumanPathArray::ConstPtr external_paths_;
  std::vector<hanp_prediction::PredictedPoses> last_predicted_poses_;
  std::map<uint64_t, size_t> last_prune_indices_;
  std::vector<double> velscale_scales_;
  double velscale_angle_, velscale_mul_, velobs_mul_, velobs_min_rad_,
      velobs_max_rad_, velobs_max_rad_time_;
  visualization_msgs::MarkerArray predicted_humans_markers_;

  bool predictHumans(hanp_prediction::HumanPosePredict::Request &req,
                     hanp_prediction::HumanPosePredict::Response &res);
  bool predictHumansVelScale(hanp_prediction::HumanPosePredict::Request &req,
                             hanp_prediction::HumanPosePredict::Response &res);
  bool predictHumansVelObs(hanp_prediction::HumanPosePredict::Request &req,
                           hanp_prediction::HumanPosePredict::Response &res);
  bool predictHumansExternal(hanp_prediction::HumanPosePredict::Request &req,
                             hanp_prediction::HumanPosePredict::Response &res);
  bool setPublishMarkers(std_srvs::SetBool::Request &req,
                         std_srvs::SetBool::Response &res);

  bool transformPoseTwist(const hanp_msgs::TrackedHumans &tracked_humans,
                          const uint64_t &human_id, const std::string &to_frame,
                          geometry_msgs::PoseStamped &pose,
                          geometry_msgs::TwistStamped &twist);
  size_t
  prunePath(size_t begin_index, const geometry_msgs::Pose &pose,
            const std::vector<geometry_msgs::PoseWithCovarianceStamped> &path);

  bool resetExtPaths(std_srvs::Empty::Request &req,
                     std_srvs::Empty::Response &res);
};
}

#endif // HUMAN_POSE_PREDICTION_H_
