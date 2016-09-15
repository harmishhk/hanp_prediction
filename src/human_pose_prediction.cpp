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

// defining constants
#define NODE_NAME "human_pose_prediction"

#define HUMANS_SUB_TOPIC "tracked_humans"
#define EXTERNAL_PATHS_SUB_TOPIC "external_human_paths"
#define PREDICT_SERVICE_NAME "predict_human_poses"
#define PUBLISH_MARKERS_SRV_NAME "publish_prediction_markers"
#define PREDICTED_HUMANS_MARKERS_PUB_TOPIC "predicted_human_poses"
#define DEFAULT_HUMAN_PART hanp_msgs::TrackedSegmentType::TORSO
#define MAX_HUMAN_MARKERS 100
#define MIN_MARKER_LIFETIME 1.0
#define MINIMUM_COVARIANCE_MARKERS 0.1

#include <signal.h>

#include <hanp_prediction/human_pose_prediction.h>

namespace hanp_prediction {
// empty constructor and destructor
HumanPosePrediction::HumanPosePrediction() {}
HumanPosePrediction::~HumanPosePrediction() {}

void HumanPosePrediction::initialize() {
  // get private node handle
  ros::NodeHandle private_nh("~/");

  // get parameters
  private_nh.param("tracked_humans_sub_topic", tracked_humans_sub_topic_,
                   std::string(HUMANS_SUB_TOPIC));
  private_nh.param("external_paths_sub_topic", external_paths_sub_topic_,
                   std::string(EXTERNAL_PATHS_SUB_TOPIC));
  private_nh.param("predict_service_name", predict_service_name_,
                   std::string(PREDICT_SERVICE_NAME));
  private_nh.param("predicted_humans_markers_pub_topic",
                   predicted_humans_markers_pub_topic_,
                   std::string(PREDICTED_HUMANS_MARKERS_PUB_TOPIC));
  private_nh.param("publish_markers_srv_name", publish_markers_srv_name_,
                   std::string(PUBLISH_MARKERS_SRV_NAME));
  private_nh.param("default_human_part", default_human_part_,
                   (int)(DEFAULT_HUMAN_PART));

  // initialize subscribers and publishers
  tracked_humans_sub_ =
      private_nh.subscribe(tracked_humans_sub_topic_, 1,
                           &HumanPosePrediction::trackedHumansCB, this);
  external_paths_sub_ =
      private_nh.subscribe(external_paths_sub_topic_, 1,
                           &HumanPosePrediction::externalPathsCB, this);
  predicted_humans_pub_ = private_nh.advertise<visualization_msgs::MarkerArray>(
      predicted_humans_markers_pub_topic_, 1);

  // set-up dynamic reconfigure
  dsrv_ =
      new dynamic_reconfigure::Server<HumanPosePredictionConfig>(private_nh);
  dynamic_reconfigure::Server<HumanPosePredictionConfig>::CallbackType cb =
      boost::bind(&HumanPosePrediction::reconfigureCB, this, _1, _2);
  dsrv_->setCallback(cb);

  // initialize services
  predict_humans_server_ = private_nh.advertiseService(
      predict_service_name_, &HumanPosePrediction::predictHumans, this);
  publish_markers_srv_ = private_nh.advertiseService(
      publish_markers_srv_name_, &HumanPosePrediction::setPublishMarkers, this);
  showing_markers_ = false;
  got_new_human_paths_ = false;

  ROS_DEBUG_NAMED(NODE_NAME, "node %s initialized", NODE_NAME);
}

void HumanPosePrediction::setParams(std::vector<double> velscale_scales,
                                    double velscale_angle, double velscale_mul,
                                    double velobs_mul, double velobs_min_rad,
                                    double velobs_max_rad,
                                    double velobs_max_rad_time) {
  velscale_scales_ = velscale_scales;
  velscale_angle_ = velscale_angle;
  velscale_mul_ = velscale_mul;
  velobs_mul_ = velobs_mul;
  velobs_min_rad_ = velobs_min_rad;
  velobs_max_rad_ = velobs_max_rad;
  velobs_max_rad_time_ = velobs_max_rad_time;

  ROS_DEBUG_NAMED(
      NODE_NAME,
      "parameters set: velocity-scale: scales=[%f, %f, %f], angle=%f, "
      "velscale-mul=%f, velobs-mul=%f"
      "velocity-obstacle: min-radius:%f, max-radius:%f, max-radius-time=%f",
      velscale_scales_[0], velscale_scales_[1], velscale_scales_[2],
      velscale_angle_, velscale_mul_, velobs_mul_, velobs_min_rad_,
      velobs_max_rad_, velobs_max_rad_time_);
}

void HumanPosePrediction::reconfigureCB(HumanPosePredictionConfig &config,
                                        uint32_t level) {
  setParams(
      {config.velscale_lower, config.velscale_nominal, config.velscale_higher},
      config.velscale_angle, config.velscale_mul, config.velobs_mul,
      config.velobs_min_rad, config.velobs_max_rad, config.velobs_max_rad_time);
}

void HumanPosePrediction::trackedHumansCB(
    const hanp_msgs::TrackedHumans &tracked_humans) {
  ROS_INFO_ONCE_NAMED(NODE_NAME, "hanp_prediction: received humans");
  tracked_humans_ = tracked_humans;
}

void HumanPosePrediction::externalPathsCB(
    const hanp_msgs::HumanPathArray::ConstPtr &external_paths) {
  ROS_INFO_ONCE_NAMED(NODE_NAME, "hanp_prediction: received human paths");
  external_paths_ = external_paths;
  got_new_human_paths_ = true;
}

bool HumanPosePrediction::predictHumans(
    hanp_prediction::HumanPosePredict::Request &req,
    hanp_prediction::HumanPosePredict::Response &res) {
  boost::function<bool(hanp_prediction::HumanPosePredict::Request & req,
                       hanp_prediction::HumanPosePredict::Response & res)>
      prediction_function;

  switch (req.type) {
  case hanp_prediction::HumanPosePredictRequest::VELOCITY_SCALE:
    prediction_function =
        boost::bind(&HumanPosePrediction::predictHumansVelScale, this, _1, _2);
    break;
  case hanp_prediction::HumanPosePredictRequest::VELOCITY_OBSTACLE:
    prediction_function =
        boost::bind(&HumanPosePrediction::predictHumansVelObs, this, _1, _2);
    break;
  case hanp_prediction::HumanPosePredictRequest::EXTERNAL:
    prediction_function =
        boost::bind(&HumanPosePrediction::predictHumansExternal, this, _1, _2);
    break;
  default:
    ROS_ERROR_NAMED(NODE_NAME, "%s: unkonwn prediction type %d", NODE_NAME,
                    req.type);
  }

  if (!prediction_function.empty() && prediction_function(req, res)) {
    if (publish_markers_) {
      // create new markers
      predicted_humans_markers_.markers.clear();

      for (auto predicted_human : res.predicted_humans_poses) {
        if (predicted_human.poses.size() > 0) {
          auto first_pose_time = predicted_human.poses[0].header.stamp;
          int marker_id = 0;

          for (auto predicted_human_pose : predicted_human.poses) {
            visualization_msgs::Marker predicted_human_marker;
            predicted_human_marker.header.frame_id =
                predicted_human_pose.header.frame_id;
            predicted_human_marker.header.stamp = first_pose_time;
            predicted_human_marker.id =
                (predicted_human.id * MAX_HUMAN_MARKERS) + marker_id++;
            predicted_human_marker.type = visualization_msgs::Marker::CYLINDER;
            predicted_human_marker.action = visualization_msgs::Marker::MODIFY;
            // assuming diagonal covariance matrix (with row-major order)
            predicted_human_marker.scale.x =
                std::max(predicted_human_pose.pose.covariance[0],
                         MINIMUM_COVARIANCE_MARKERS);
            predicted_human_marker.scale.y =
                std::max(predicted_human_pose.pose.covariance[7],
                         MINIMUM_COVARIANCE_MARKERS);
            predicted_human_marker.scale.z = 0.01;
            predicted_human_marker.color.a = 1.0;
            predicted_human_marker.color.r = 0.0;
            predicted_human_marker.color.g = 0.0;
            predicted_human_marker.color.b = 1.0;
            predicted_human_marker.lifetime =
                ros::Duration(MIN_MARKER_LIFETIME) +
                (predicted_human_pose.header.stamp - first_pose_time);
            predicted_human_marker.pose.position.x =
                predicted_human_pose.pose.pose.position.x;
            predicted_human_marker.pose.position.y =
                predicted_human_pose.pose.pose.position.y;
            // time on z axis
            predicted_human_marker.pose.position.z =
                (predicted_human_pose.header.stamp - first_pose_time).toSec();
            predicted_humans_markers_.markers.push_back(predicted_human_marker);
          }
        } else {
          ROS_WARN_NAMED(NODE_NAME, "no predicted poses fro human %ld",
                         predicted_human.id);
        }
      }

      predicted_humans_pub_.publish(predicted_humans_markers_);
      showing_markers_ = true;

      ROS_DEBUG_NAMED(NODE_NAME, "published predicted humans");
    } else {
      if (showing_markers_) {
        predicted_humans_markers_.markers.clear();
        visualization_msgs::Marker delete_human_markers;
        delete_human_markers.action =
            3; // visualization_msgs::Marker::DELETEALL;
        predicted_humans_markers_.markers.push_back(delete_human_markers);
        predicted_humans_pub_.publish(predicted_humans_markers_);
        showing_markers_ = false;
      }
    }

    return true;
  } else {
    return false;
  }
}

bool HumanPosePrediction::predictHumansVelScale(
    hanp_prediction::HumanPosePredict::Request &req,
    hanp_prediction::HumanPosePredict::Response &res) {
  // validate prediction time
  if (req.predict_times.size() == 0) {
    ROS_ERROR_NAMED(NODE_NAME, "prediction times cannot be empty");
    return false;
  }
  if (req.predict_times[0] < 0) {
    ROS_ERROR_NAMED(NODE_NAME, "prediction time cannot be negative (give %f)",
                    req.predict_times[0]);
    return false;
  }

  // get local refrence of humans
  auto humans = tracked_humans_.humans;
  auto track_frame = tracked_humans_.header.frame_id;
  auto track_time = tracked_humans_.header.stamp;

  if (track_time.toSec() < req.predict_times[0]) {
    ROS_DEBUG_NAMED(
        NODE_NAME,
        "human data is older than prediction time, predicting nothing");
    return true;
  }

  for (auto human : humans) {
    // TODO: filter by res.ids

    for (auto segment : human.segments) {
      if (segment.type == default_human_part_) {
        // get linear velocity of the human
        tf::Vector3 linear_vel(segment.twist.twist.linear.x,
                               segment.twist.twist.linear.y,
                               segment.twist.twist.linear.z);

        // calculate variations in velocity of human
        std::vector<tf::Vector3> vel_variations;
        for (auto vel_scale : velscale_scales_) {
          vel_variations.push_back(
              linear_vel.rotate(tf::Vector3(0, 0, 1), velscale_angle_) *
              vel_scale * velscale_mul_);
          vel_variations.push_back(
              linear_vel.rotate(tf::Vector3(0, 0, 1), -velscale_angle_) *
              vel_scale * velscale_mul_);
        }

        // calculate future human poses based on velocity variations
        hanp_prediction::PredictedPoses predicted_poses;
        predicted_poses.id = human.track_id;
        for (auto vel : vel_variations) {
          geometry_msgs::PoseWithCovarianceStamped predicted_pose;
          predicted_pose.header.frame_id = track_frame;
          predicted_pose.header.stamp =
              track_time + ros::Duration(req.predict_times[0]);
          predicted_pose.pose.pose.position.x =
              segment.pose.pose.position.x + vel.x() * req.predict_times[0];
          predicted_pose.pose.pose.position.y =
              segment.pose.pose.position.y + vel.y() * req.predict_times[0];
          predicted_pose.pose.pose.orientation = segment.pose.pose.orientation;
          // no covariance for this method
          predicted_poses.poses.push_back(predicted_pose);

          ROS_DEBUG_NAMED(
              NODE_NAME, "predected human (%lu) segment (%d)"
                         " pose: x=%f, y=%f, theta=%f with vel x=%f,y=%f",
              human.track_id, segment.type, predicted_pose.pose.pose.position.x,
              predicted_pose.pose.pose.position.y,
              tf::getYaw(predicted_pose.pose.pose.orientation), vel.x(),
              vel.y());
        }

        geometry_msgs::TwistStamped current_twist;
        current_twist.header.frame_id = track_frame;
        current_twist.header.stamp = track_time;
        current_twist.twist = segment.twist.twist;
        predicted_poses.start_velocity = current_twist;

        res.predicted_humans_poses.push_back(predicted_poses);
      }
    }
  }

  return true;
}

bool HumanPosePrediction::predictHumansVelObs(
    hanp_prediction::HumanPosePredict::Request &req,
    hanp_prediction::HumanPosePredict::Response &res) {
  // validate prediction time
  if (req.predict_times.size() == 0) {
    ROS_ERROR_NAMED(NODE_NAME, "prediction times cannot be empty");
    return false;
  }
  if (*std::min_element(req.predict_times.begin(), req.predict_times.end()) <
      0.0) {
    ROS_ERROR_NAMED(NODE_NAME, "prediction time cannot be negative");
    return false;
  }

  // get local refrence of humans
  auto humans = tracked_humans_.humans;
  auto track_frame = tracked_humans_.header.frame_id;
  auto track_time = tracked_humans_.header.stamp;

  if ((ros::Time::now() - track_time).toSec() >
      *std::max_element(req.predict_times.begin(), req.predict_times.end())) {
    ROS_DEBUG_NAMED(NODE_NAME, "human data is older than maximum given "
                               "prediction time, predicting nothing");
    return true;
  }

  for (auto human : humans) {
    // TODO: filter by res.ids

    for (auto segment : human.segments) {
      if (segment.type == default_human_part_) {
        // calculate future human poses based on current velocity
        hanp_prediction::PredictedPoses predicted_poses;
        predicted_poses.id = human.track_id;

        // get linear velocity of the human
        tf::Vector3 linear_vel(segment.twist.twist.linear.x,
                               segment.twist.twist.linear.y,
                               segment.twist.twist.linear.z);

        for (auto predict_time : req.predict_times) {
          // validate prediction time
          if (predict_time < 0) {
            ROS_ERROR_NAMED(NODE_NAME,
                            "%s: prediction time cannot be negative (give %f)",
                            NODE_NAME, predict_time);
            return false;
          }

          geometry_msgs::PoseWithCovarianceStamped predicted_pose;
          tf::Vector3 predict_lin_vel(linear_vel * predict_time * velobs_mul_);
          predicted_pose.header.frame_id = track_frame;
          predicted_pose.header.stamp =
              track_time + ros::Duration(predict_time);
          predicted_pose.pose.pose.position.x =
              segment.pose.pose.position.x + predict_lin_vel[0];
          predicted_pose.pose.pose.position.y =
              segment.pose.pose.position.y + predict_lin_vel[1];
          predicted_pose.pose.pose.orientation = segment.pose.pose.orientation;
          double xy_vel = hypot(predict_lin_vel[0], predict_lin_vel[1]);
          // storing only x, y covariance in diagonal matrix
          predicted_pose.pose.covariance[0] =
              velobs_min_rad_ +
              (velobs_max_rad_ - velobs_min_rad_) *
                  (predict_time / velobs_max_rad_time_) * xy_vel;
          predicted_pose.pose.covariance[7] = predicted_pose.pose.covariance[0];
          predicted_poses.poses.push_back(predicted_pose);

          ROS_DEBUG_NAMED(
              NODE_NAME, "%s: predected human (%lu) segment (%d)"
                         " pose: x=%f, y=%f, theta=%f, predict-time=%f",
              NODE_NAME, human.track_id, segment.type,
              predicted_pose.pose.pose.position.x,
              predicted_pose.pose.pose.position.y,
              tf::getYaw(predicted_pose.pose.pose.orientation), predict_time);
        }

        geometry_msgs::TwistStamped current_twist;
        current_twist.header.frame_id = track_frame;
        current_twist.header.stamp = track_time;
        current_twist.twist = segment.twist.twist;
        predicted_poses.start_velocity = current_twist;

        res.predicted_humans_poses.push_back(predicted_poses);
      }
    }
  }

  return true;
}

bool HumanPosePrediction::predictHumansExternal(
    hanp_prediction::HumanPosePredict::Request &req,
    hanp_prediction::HumanPosePredict::Response &res) {
  auto external_paths = external_paths_;
  auto tracked_humans = tracked_humans_;

  if (got_new_human_paths_) {
    for (auto human_path : external_paths->paths) {
      auto &poses = human_path.path.poses;
      if (!poses.empty()) {
        hanp_prediction::PredictedPoses predicted_poses;
        predicted_poses.id = human_path.id;

        predicted_poses.poses.resize(poses.size());
        for (size_t i = 0; i < poses.size(); ++i) {
          auto &pose = poses[i];
          geometry_msgs::PoseWithCovarianceStamped predicted_pose;
          predicted_pose.header.stamp = pose.header.stamp;
          predicted_pose.header.frame_id = pose.header.frame_id;
          predicted_pose.pose.pose = pose.pose;
          predicted_poses.poses[i] = predicted_pose;
        }

        for (auto it = last_predicted_poses_.begin();
             it != last_predicted_poses_.end(); ++it) {
          if (it->id == predicted_poses.id) {
            last_predicted_poses_.erase(it);
            break;
          }
        }
        last_predicted_poses_.push_back(predicted_poses);

        last_prune_indices_.erase(predicted_poses.id);

        for (auto it = tracked_humans.humans.begin();
             it != tracked_humans.humans.end(); ++it) {
          if (it->track_id == predicted_poses.id) {
            tracked_humans.humans.erase(it);
            break;
          }
        }
        ROS_DEBUG_NAMED(
            NODE_NAME,
            "Processed new external path for human %ld with %ld poses",
            human_path.id, predicted_poses.poses.size());
      }
    }
  }
  got_new_human_paths_ = false;

  for (auto &poses : last_predicted_poses_) {
    if (!poses.poses.empty()) {
      geometry_msgs::PoseStamped start_pose;
      geometry_msgs::TwistStamped start_twist;
      if (transformPoseTwist(tracked_humans, poses.id,
                             poses.poses.front().header.frame_id, start_pose,
                             start_twist)) {
        auto last_prune_index_it = last_prune_indices_.find(poses.id);
        auto begin_index = (last_prune_index_it != last_prune_indices_.end())
                               ? last_prune_index_it->second
                               : 0;
        auto prune_index = prunePath(begin_index, start_pose.pose, poses.poses);
        last_prune_indices_[poses.id] = prune_index;
        if (prune_index < 0 || prune_index > poses.poses.size()) {
          ROS_ERROR_NAMED(NODE_NAME, "Logical error, cannot prune path");
          continue;
        }

        std::vector<geometry_msgs::PoseWithCovarianceStamped> pruned_path(
            poses.poses.begin() + prune_index, poses.poses.end());

        if (!pruned_path.empty()) {
          hanp_prediction::PredictedPoses predicted_poses;
          predicted_poses.id = poses.id;
          predicted_poses.poses = pruned_path;
          predicted_poses.start_velocity = start_twist;
          res.predicted_humans_poses.push_back(predicted_poses);
          ROS_DEBUG_NAMED(
              NODE_NAME,
              "Giving path of %ld points from %ld points for human %ld\n",
              predicted_poses.poses.size(), poses.poses.size(), poses.id);
        }
      }
    }
  }

  return true;
}

bool HumanPosePrediction::setPublishMarkers(std_srvs::SetBool::Request &req,
                                            std_srvs::SetBool::Response &res) {
  publish_markers_ = req.data;
  res.success = true;
  res.message = "Prediction markers publishing " + publish_markers_
                    ? "enabled"
                    : "disabled";
  return true;
}

bool HumanPosePrediction::transformPoseTwist(
    const hanp_msgs::TrackedHumans &tracked_humans, const uint64_t &human_id,
    const std::string &to_frame, geometry_msgs::PoseStamped &pose,
    geometry_msgs::TwistStamped &twist) {
  for (auto &human : tracked_humans.humans) {
    if (human.track_id == human_id) {
      for (auto &segment : human.segments) {
        if (segment.type == default_human_part_) {
          geometry_msgs::PoseStamped pose_ut;
          pose_ut.header.stamp = tracked_humans.header.stamp;
          pose_ut.header.frame_id = tracked_humans.header.frame_id;
          pose_ut.pose = segment.pose.pose;
          twist.header.stamp = tracked_humans.header.stamp;
          twist.header.frame_id = tracked_humans.header.frame_id;
          twist.twist = segment.twist.twist;
          try {
            tf::Stamped<tf::Pose> pose_tf;
            tf::poseStampedMsgToTF(pose_ut, pose_tf);
            tf::StampedTransform start_pose_to_plan_transform;
            tf_.waitForTransform(to_frame, pose_ut.header.frame_id,
                                 ros::Time(0), ros::Duration(0.5));
            tf_.lookupTransform(to_frame, pose_ut.header.frame_id, ros::Time(0),
                                start_pose_to_plan_transform);
            pose_tf.setData(start_pose_to_plan_transform * pose_tf);
            tf::poseStampedTFToMsg(pose_tf, pose);

            geometry_msgs::Twist start_twist_to_plan_transform;
            tf_.lookupTwist(to_frame, twist.header.frame_id, ros::Time(0),
                            ros::Duration(0.1), start_twist_to_plan_transform);
            twist.twist.linear.x -= start_twist_to_plan_transform.linear.x;
            twist.twist.linear.y -= start_twist_to_plan_transform.linear.y;
            twist.twist.angular.z -= start_twist_to_plan_transform.angular.z;
            return true;
          } catch (tf::LookupException &ex) {
            ROS_ERROR_NAMED(NODE_NAME, "No Transform available Error: %s\n",
                            ex.what());
          } catch (tf::ConnectivityException &ex) {
            ROS_ERROR_NAMED(NODE_NAME, "Connectivity Error: %s\n", ex.what());
          } catch (tf::ExtrapolationException &ex) {
            ROS_ERROR_NAMED(NODE_NAME, "Extrapolation Error: %s\n", ex.what());
          }
          break;
        }
      }
      break;
    }
  }
  return false;
}

size_t HumanPosePrediction::prunePath(
    size_t begin_index, const geometry_msgs::Pose &pose,
    const std::vector<geometry_msgs::PoseWithCovarianceStamped> &path) {
  size_t prune_index = begin_index;
  double x_diff, y_diff, sq_diff,
      smallest_sq_diff = std::numeric_limits<double>::max();
  while (begin_index < path.size()) {
    x_diff = path[begin_index].pose.pose.position.x - pose.position.x;
    y_diff = path[begin_index].pose.pose.position.y - pose.position.y;
    sq_diff = x_diff * x_diff + y_diff * y_diff;
    if (sq_diff < smallest_sq_diff) {
      prune_index = begin_index;
      smallest_sq_diff = sq_diff;
    }
    ++begin_index;
  }
  return prune_index;
}
}

// handler for something to do before killing the node
void sigintHandler(int sig) {
  ROS_DEBUG_NAMED(NODE_NAME, "node %s will now shutdown", NODE_NAME);

  // the default sigint handler, it calls shutdown() on node
  ros::shutdown();
}

// the main method starts a rosnode and initializes the optotrack_person class
int main(int argc, char **argv) {
  // starting the optotrack_person node
  ros::init(argc, argv, NODE_NAME);
  ROS_DEBUG_NAMED(NODE_NAME, "started %s node", NODE_NAME);

  // initiazling HANPHeadBehavior class
  hanp_prediction::HumanPosePrediction HumanPosePrediction;
  HumanPosePrediction.initialize();

  // look for sigint and start spinning the node
  signal(SIGINT, sigintHandler);
  ros::spin();

  return 0;
}
