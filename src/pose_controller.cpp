/*******************************************************************************************************************//**
 *  @file    pose_controller.cpp
 *  @brief   Handles control of Syropod body posing.
 *
 *  @author  Fletcher Talbot (fletcher.talbot@csiro.au)
 *  @date    June 2017
 *  @version 0.5.0
 *
 *  CSIRO Autonomous Systems Laboratory
 *  Queensland Centre for Advanced Technologies
 *  PO Box 883, Kenmore, QLD 4069, Australia
 *
 *  (c) Copyright CSIRO 2017
 *
 *  All rights reserved, no part of this program may be used
 *  without explicit permission of CSIRO
 *
***********************************************************************************************************************/

#include "../include/syropod_highlevel_controller/pose_controller.h"

/*******************************************************************************************************************//**
 * PoseController class constructor. Initialises member variables.
 * @param[in] model Pointer to the robot model class object
 * @param[in] params Pointer to the parameter struct object
***********************************************************************************************************************/
PoseController::PoseController(shared_ptr<Model> model, const Parameters& params)
  : model_(model)
  , params_(params)
{
  resetAllPosing();

  imu_data_.orientation = Quat::Identity();
  imu_data_.linear_acceleration = Vector3d(0, 0, 0);
  imu_data_.angular_velocity = Vector3d(0, 0, 0);
  
  rotation_absement_error_ = Vector3d(0, 0, 0);
  rotation_position_error_ = Vector3d(0, 0, 0);
  rotation_velocity_error_ = Vector3d(0, 0, 0);
}

/*******************************************************************************************************************//**
 * Iterates through legs in robot model and generates and assigns a leg poser object. Calls function to initialise auto
 * pose objects. Seperated from constructor due to shared_from_this constraints.
***********************************************************************************************************************/
void PoseController::init(void)
{
  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    leg->setLegPoser(make_shared<LegPoser>(shared_from_this(), leg));
  }
  setAutoPoseParams();
}

/*******************************************************************************************************************//**
 * Initialises auto poser container and populates with auto poser class objects as defined by auto poser parameters.
 * Also sets auto pose parameters for the leg poser object of each leg object in robot model.
***********************************************************************************************************************/
void PoseController::setAutoPoseParams(void)
{
  double raw_phase_length;
  int base_phase_length;
	pose_frequency_ = params_.pose_frequency.data;

  // Calculate posing phase length and normalisation values based off gait/posing cycle parameters
  if (pose_frequency_ == -1.0) //Use step cycle parameters
  {
    base_phase_length = params_.stance_phase.data + params_.swing_phase.data;
    double swing_ratio = double(params_.swing_phase.data) / base_phase_length;
    raw_phase_length = ((1.0 / params_.step_frequency.current_value) / params_.time_delta.data) / swing_ratio;
  }
  else
  {
    base_phase_length = params_.pose_phase_length.data;
    raw_phase_length = ((1.0 / pose_frequency_) / params_.time_delta.data);
  }
  pose_phase_length_ = roundToEvenInt(raw_phase_length / base_phase_length) * base_phase_length;
  normaliser_ = pose_phase_length_ / base_phase_length;

  // Set posing negation phase variables according to auto posing parameters
  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
    leg_poser->setPoseNegationPhaseStart(params_.pose_negation_phase_starts.data.at(leg->getIDName()));
    leg_poser->setPoseNegationPhaseEnd(params_.pose_negation_phase_ends.data.at(leg->getIDName()));
    
    // Set reference leg for auto posing system to that which has zero phase offset
    if (params_.offset_multiplier.data.at(leg->getIDName()) == 0)
    {
      auto_pose_reference_leg_ = leg;
    }
  }

  // Clear any old auto-poser objects and re-populate container
  auto_poser_container_.clear();
	for (int i = 0; i < int(params_.pose_phase_starts.data.size()); ++i)
  {
    auto_poser_container_.push_back(make_shared<AutoPoser>(shared_from_this(), i));
  }

  // For each auto-poser object set control variables from auto_posing parameters
  AutoPoserContainer::iterator auto_poser_it;
  for (auto_poser_it = auto_poser_container_.begin(); auto_poser_it != auto_poser_container_.end(); ++auto_poser_it)
  {
    shared_ptr<AutoPoser> auto_poser = *auto_poser_it;
    int id = auto_poser->getIDNumber();
    auto_poser->setStartPhase(params_.pose_phase_starts.data[id]);
    auto_poser->setEndPhase(params_.pose_phase_ends.data[id]);
    auto_poser->setXAmplitude(params_.x_amplitudes.data[id]);
    auto_poser->setYAmplitude(params_.y_amplitudes.data[id]);
    auto_poser->setZAmplitude(params_.z_amplitudes.data[id]);
    auto_poser->setRollAmplitude(params_.roll_amplitudes.data[id]);
    auto_poser->setPitchAmplitude(params_.pitch_amplitudes.data[id]);
    auto_poser->setYawAmplitude(params_.yaw_amplitudes.data[id]);
    auto_poser->resetChecks();
  }
}

/*******************************************************************************************************************//**
 * Iterates through legs in robot model and updates each Leg Poser tip position. This new tip position is the tip 
 * position defined from the Leg Stepper, posed using the current desired pose. The applied pose is dependent on the
 * state of the Leg and Leg Poser specific auto posing.
***********************************************************************************************************************/
void PoseController::updateStance(void)
{
  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    shared_ptr<LegStepper> leg_stepper = leg->getLegStepper();
    shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
    Pose current_pose = model_->getCurrentPose();
    LegState leg_state = leg->getLegState();

    if (leg_state == WALKING || leg_state == MANUAL_TO_WALKING)
    {
      // Remove auto posing from current pose under correct conditions and add leg specific auto pose
      current_pose = current_pose.removePose(auto_pose_);
      current_pose = current_pose.addPose(leg_poser->getAutoPose());

      // Apply pose to current walking tip position to calculate new 'posed' tip position
      Vector3d new_tip_position = current_pose.inverseTransformVector(leg_stepper->getCurrentTipPosition());
      leg_poser->setCurrentTipPosition(new_tip_position);
    }
    // Do not apply any posing to manually manipulated legs
    else if (leg_state == MANUAL || leg_state == WALKING_TO_MANUAL)
    {
      leg_poser->setCurrentTipPosition(leg_stepper->getCurrentTipPosition());
    }
  }
}

/*******************************************************************************************************************//**
 * Executes saved transition sequence in direction defined by 'sequence' (START_UP or SHUT_DOWN) through the use of the
 * function StepToPosition() to move to pre-defined tip positions for each leg in the robot model. If no sequence exists
 * for target stance, it generates one iteratively by checking workspace limitations.
 * @param[in] sequence The requested sequence - either START_UP or SHUT_DOWN
 * @return Returns an int from 0 to 100 signifying the progress of the sequence (100 meaning 100% complete)
 * @todo Make sequential leg stepping coordination an option instead of only simultaneous (direct) & groups (tripod)
***********************************************************************************************************************/
int PoseController::executeSequence(const SequenceSelection& sequence)
{
  bool debug = params_.debug_execute_sequence.data;
  
  // Initialise/Reset any saved transition sequence
  if (reset_transition_sequence_ && sequence == START_UP)
  {
    reset_transition_sequence_ = false;
    first_sequence_execution_ = true;
    transition_step_ = 0;
    for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
    {
      shared_ptr<Leg> leg = leg_it_->second;
      shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
      leg_poser->resetTransitionSequence();
      leg_poser->addTransitionPosition(leg->getCurrentTipPosition()); // Initial transition position
    }
  }

  int progress = 0; // Percentage progress (0%->100%)
  int normalised_progress;

  // Setup sequence type sepecific variables (transition type, direction and target)
  int next_transition_step;
  int transition_step_target;
  bool execute_horizontal_transition;
  bool execute_vertical_transition;
  int total_progress;
  if (sequence == START_UP)
  {
    execute_horizontal_transition = !(transition_step_%2); // Even steps
    execute_vertical_transition = transition_step_%2; // Odd steps
    next_transition_step = transition_step_ + 1;
    transition_step_target = transition_step_count_;
    total_progress = transition_step_*100 / max(transition_step_count_, 1);
  }
  else if (sequence == SHUT_DOWN)
  {
    execute_horizontal_transition = transition_step_%2; // Odd steps
    execute_vertical_transition = !(transition_step_%2); // Even steps
    next_transition_step = transition_step_ - 1;
    transition_step_target = 0;
    total_progress = 100 - transition_step_*100 / max(transition_step_count_, 1);
  }

  //Determine if this transition is the last one before end of sequence
  bool final_transition;
  bool sequence_complete = false;
  if (first_sequence_execution_)
  {
    final_transition = (horizontal_transition_complete_ || vertical_transition_complete_);
  }
  else
  {
    final_transition = (next_transition_step == transition_step_target);
  }

  // Safety factor during first sequence execution decreases for each successive transition
  double safety_factor = (first_sequence_execution_ ? SAFETY_FACTOR/(transition_step_+1) : 0.0);

  // Attempt to step (in specific coordination) along horizontal plane to transition positions
  if (execute_horizontal_transition)
  {
    if (set_target_)
    {
      // Set horizontal target
      set_target_ = false;
      ROS_DEBUG_COND(debug, "\nTRANSITION STEP: %d (HORIZONTAL):\n", transition_step_);
      for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
      {
        shared_ptr<Leg> leg = leg_it_->second;
        shared_ptr<LegStepper> leg_stepper = leg->getLegStepper();
        shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
        leg_poser->setLegCompletedStep(false);

        Vector3d target_tip_position;
        if (leg_poser->hasTransitionPosition(next_transition_step))
        {
          ROS_DEBUG_COND(debug, "\nLeg %s targeting transition position %d.\n",
                         leg->getIDName().c_str(), next_transition_step);
          target_tip_position = leg_poser->getTransitionPosition(next_transition_step);
        }
        else
        {
          ROS_DEBUG_COND(debug, "\nNo transition position found for leg %s - targeting default stance position.\n",
                          leg->getIDName().c_str());
          target_tip_position = leg_stepper->getDefaultTipPosition();
        }

        //Maintain horizontal position
        target_tip_position[2] = leg->getCurrentTipPosition()[2];

        leg_poser->setTargetTipPosition(target_tip_position);
      }
    }

    // Step to target
    bool direct_step = !model_->legsBearingLoad();
    for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
    {
      shared_ptr<Leg> leg = leg_it_->second;
      shared_ptr<LegStepper> leg_stepper = leg->getLegStepper();
      shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
      if (!leg_poser->getLegCompletedStep())
      {
        // Step leg if leg is in stepping group OR simultaneous direct stepping is allowed
        if (leg->getGroup() == current_group_ || direct_step)
        {
          Vector3d target_tip_position = leg_poser->getTargetTipPosition();
          bool apply_delta_z = (sequence == START_UP && final_transition); //Only add delta_z at end of StartUp sequence
          Pose pose = (apply_delta_z ? model_->getCurrentPose() : Pose::identity());
          double step_height = direct_step ? 0.0 : leg_stepper->getSwingHeight();
          double time_to_step = HORIZONTAL_TRANSITION_TIME / params_.step_frequency.current_value;
          time_to_step *= (first_sequence_execution_ ? 2.0:1.0); // Double time for initial sequence
          progress = leg_poser->stepToPosition(target_tip_position, pose, step_height, time_to_step, apply_delta_z);
          leg->setDesiredTipPosition(leg_poser->getCurrentTipPosition(), false);
          double limit_proximity = leg->applyIK(params_.debug_IK.data);
          bool exceeded_workspace = limit_proximity < safety_factor; // Leg attempted to move beyond safe workspace

          // Leg has attempted to move beyond workspace so stop transition early
          if (first_sequence_execution_ && exceeded_workspace)
          {
            string joint_position_string;
            for (joint_it_ = leg->getJointContainer()->begin();
                 joint_it_ != leg->getJointContainer()->end();
                 ++joint_it_)
            {
              shared_ptr<Joint> joint = joint_it_->second;
              joint_position_string += stringFormat("\tJoint: %s\tPosition: %f\n",
                                                    joint->id_name_.c_str(), joint->desired_position_);
            }
            ROS_DEBUG_COND(debug, "\nLeg %s exceeded safety factor.\nOptimise sequence by setting 'unpacked'joint"
                           "positions to the following:\n%s", leg->getIDName().c_str(), joint_position_string.c_str());
            leg_poser->setTargetTipPosition(leg_poser->getCurrentTipPosition());
            progress = leg_poser->resetStepToPosition(); // Skips to 'complete' progress and resets
            proximity_alert_ = true;
          }

          if (progress == PROGRESS_COMPLETE)
          {
            leg_poser->setLegCompletedStep(true);
            legs_completed_step_ ++;
            if (first_sequence_execution_)
            {
              bool reached_target = !exceeded_workspace;
              Vector3d targetTipPosition = leg_poser->getTargetTipPosition();
              Vector3d currentTipPosition = leg_poser->getCurrentTipPosition();
              Vector3d transition_position = (reached_target ? targetTipPosition : currentTipPosition);
              leg_poser->addTransitionPosition(transition_position);
              ROS_DEBUG_COND(debug, "\nAdded transition position %d for leg %s.\n",
                             next_transition_step, leg->getIDName().c_str());
            }
          }
        }
        // Leg not designated to step so set step completed
        else
        {
          legs_completed_step_++;
          leg_poser->setLegCompletedStep(true);
        }
      }
    }

    // Normalise transition progress for use in calculation of total sequence progress
    if (direct_step)
    {
      normalised_progress = progress/max(transition_step_count_, 1);
    }
    else
    {
      normalised_progress = (progress/2 + (current_group_ == 0 ? 0 : 50))/max(transition_step_count_, 1);
    }

    // Check if legs have completed steps and if transition has completed without a proximity alert
    // TODO Future work - make sequential leg stepping coordination an option
    if (legs_completed_step_ == model_->getLegCount())
    {
      set_target_ = true;
      legs_completed_step_ = 0;
      if (current_group_ == 1 || direct_step)
      {
        current_group_ = 0;
        transition_step_ = next_transition_step;
        horizontal_transition_complete_ = !proximity_alert_;
        sequence_complete = final_transition;
        proximity_alert_ = false;
      }
      else if (current_group_ == 0)
      {
        current_group_ = 1;
      }
    }
  }

  // Attempt to step directly along vertical trajectory to transition positions
  if (execute_vertical_transition)
  {
    if (set_target_)
    {
      // Set vertical target
      set_target_ = false;
      ROS_DEBUG_COND(debug, "\nTRANSITION STEP: %d (VERTICAL):\n", transition_step_);
      for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
      {
        shared_ptr<Leg> leg = leg_it_->second;
        shared_ptr<LegStepper> leg_stepper = leg->getLegStepper();
        shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
        Vector3d target_tip_position;
        if (leg_poser->hasTransitionPosition(next_transition_step))
        {
          ROS_DEBUG_COND(debug, "\nLeg %s targeting transition position %d.\n",
                         leg->getIDName().c_str(), next_transition_step);
          target_tip_position = leg_poser->getTransitionPosition(next_transition_step);
        }
        else
        {
          ROS_DEBUG_COND(debug, "\nNo transition position found for leg %s - targeting default stance position.\n",
                         leg->getIDName().c_str());
          target_tip_position = leg_stepper->getDefaultTipPosition();
        }

        //Maintain horizontal position
        target_tip_position[0] = leg->getCurrentTipPosition()[0];
        target_tip_position[1] = leg->getCurrentTipPosition()[1];
        leg_poser->setTargetTipPosition(target_tip_position);
      }
    }

    // Step to target
    bool all_legs_within_workspace = true;
    for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
    {
      shared_ptr<Leg> leg = leg_it_->second;
      shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
      Vector3d target_tip_position = leg_poser->getTargetTipPosition();
      bool apply_delta_z = (sequence == START_UP && final_transition);
      Pose pose = (apply_delta_z ? model_->getCurrentPose() : Pose::identity());
      double time_to_step = VERTICAL_TRANSITION_TIME / params_.step_frequency.current_value;
      time_to_step *= (first_sequence_execution_ ? 2.0:1.0);
      progress = leg_poser->stepToPosition(target_tip_position, pose, 0.0, time_to_step, apply_delta_z);
      leg->setDesiredTipPosition(leg_poser->getCurrentTipPosition(), false);
      double limit_proximity = leg->applyIK(params_.debug_IK.data);
      all_legs_within_workspace = all_legs_within_workspace && !(limit_proximity < safety_factor);
      ROS_DEBUG_COND(debug && limit_proximity < safety_factor,
                      "\nLeg %s exceeded safety factor\n", leg->getIDName().c_str());
    }

    // All legs have completed vertical transition (either by reaching target or exceeding safe workspace)
    if ((!all_legs_within_workspace && first_sequence_execution_) || progress == PROGRESS_COMPLETE)
    {
      for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
      {
        shared_ptr<Leg> leg = leg_it_->second;
        shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
        progress = leg_poser->resetStepToPosition();
        if (first_sequence_execution_)
        {
          bool reached_target = all_legs_within_workspace; // Assume reached target if all are within safe workspace
          Vector3d targetTipPosition = leg_poser->getTargetTipPosition();
          Vector3d currentTipPosition = leg_poser->getCurrentTipPosition();
          Vector3d transition_position = (reached_target ? targetTipPosition : currentTipPosition);
          leg_poser->addTransitionPosition(transition_position);
          ROS_DEBUG_COND(debug, "\nAdded transition position %d for leg %s.\n",
                        next_transition_step, leg->getIDName().c_str());
        }
      }

      vertical_transition_complete_ = all_legs_within_workspace;
      transition_step_ = next_transition_step;
      sequence_complete = final_transition; //Sequence is complete if this transition was the final one
      set_target_ = true;
    }

    // Normalise transition progress for use in calculation of total sequence progress
    normalised_progress = progress/max(transition_step_count_, 1);
  }

  // Update count of transition steps as first sequence executes
  if (first_sequence_execution_)
  {
    transition_step_count_ = transition_step_;
    transition_step_target = transition_step_;
  }

  // Check for excessive transition steps
  if (transition_step_ > TRANSITION_STEP_THRESHOLD)
  {
    ROS_FATAL("\nUnable to execute sequence, shutting down controller.\n");
    ros::shutdown();
  }

  // Check if sequence has completed
  if (sequence_complete)
  {
    set_target_ = true;
    vertical_transition_complete_ = false;
    horizontal_transition_complete_ = false;
    first_sequence_execution_ = false;
    return  PROGRESS_COMPLETE;
  }
  // If sequence has not completed return percentage estimate of completion (i.e. < 100%)
  else
  {
    total_progress = min(total_progress + normalised_progress, PROGRESS_COMPLETE-1); 
    return (first_sequence_execution_ ? -1 : total_progress);
  }
}

/*******************************************************************************************************************//**
 * Iterates through legs in robot model and attempts to move them simultaneously in a linear trajectory directly from 
 * their current tip position to its default tip position (as defined by the walk controller). This motion completes in
 * a time limit defined by the parameter time_to_start.
 * @return Returns an int from 0 to 100 signifying the progress of the sequence (100 meaning 100% complete)
***********************************************************************************************************************/
int PoseController::directStartup(void) //Simultaneous leg coordination
{
  int progress = 0; // Percentage progress (0%->100%)

  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
    shared_ptr<LegStepper> leg_stepper = leg->getLegStepper();
    double time_to_start = params_.time_to_start.data;
    Vector3d default_tip_position = leg_stepper->getDefaultTipPosition();
    progress = leg_poser->stepToPosition(default_tip_position, model_->getCurrentPose(), 0.0, time_to_start);
    leg->setDesiredTipPosition(leg_poser->getCurrentTipPosition(), false);
    leg->applyIK(params_.debug_IK.data);
  }

  return progress;
}

/*******************************************************************************************************************//**
 * Iterates through legs in robot model and attempts to step each from their current tip position to their default tip
 * position (as defined by the walk controller). The stepping motion is coordinated such that half of the legs execute 
 * the step at any one time (for a hexapod this results in a Tripod stepping coordination). The time period and 
 * height of the stepping maneuver is controlled by the user parameters step_frequency and step_clearance.
 * @return Returns an int from 0 to 100 signifying the progress of the sequence (100 meaning 100% complete)
***********************************************************************************************************************/
int PoseController::stepToNewStance(void) //Tripod leg coordination
{
  int progress = 0; // Percentage progress (0%->100%)
  int leg_count = model_->getLegCount();
  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    if (leg->getGroup() == current_group_)
    {
      shared_ptr<LegStepper> leg_stepper = leg->getLegStepper();
      shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
      double step_height = leg_stepper->getSwingHeight();
      double step_time = 1.0 / params_.step_frequency.current_value;
      Vector3d target_tip_position = leg_stepper->getDefaultTipPosition();
      progress = leg_poser->stepToPosition(target_tip_position, model_->getCurrentPose(), step_height, step_time);
      leg->setDesiredTipPosition(leg_poser->getCurrentTipPosition(), false);
      leg->applyIK(params_.debug_IK.data);
      legs_completed_step_ += int(progress == PROGRESS_COMPLETE);
    }
  }

  // Normalise progress in terms of total procedure
  progress = progress / 2 + current_group_ * 50;

  current_group_ = legs_completed_step_ / (leg_count / 2);

  if (legs_completed_step_ == leg_count)
  {
    legs_completed_step_ = 0;
    current_group_ = 0;
  }

  // Set flag to reset any stored transition sequences and generate new sequence for new stance
  reset_transition_sequence_ = true;

  return progress;
}

/*******************************************************************************************************************//**
 * Iterates through the legs in the robot model and generates a pose for each that is best for leg manipulation. This 
 * pose is generated to attempt to move the centre of gravity within the support polygon of the load bearing legs. All 
 * legs simultaneously step to each new generated pose and the time period and height of the stepping maneuver is 
 * controlled by the user parameters step_frequency and step_clearance.
 * @return Returns an int from 0 to 100 signifying the progress of the sequence (100 meaning 100% complete)
***********************************************************************************************************************/
int PoseController::poseForLegManipulation(void) //Simultaneous leg coordination
{
  Pose target_pose;
  int progress = 0; // Percentage progress (0%->100%)
  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    shared_ptr<LegStepper> leg_stepper = leg->getLegStepper();
    shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
    double step_height = leg_stepper->getSwingHeight();
    double step_time = 1.0 / params_.step_frequency.current_value;

    // Set up target pose for legs depending on state
    if (leg->getLegState() == WALKING_TO_MANUAL)
    {
      target_pose = Pose::identity();
      target_pose.position_ += inclination_pose_.position_; // Apply inclination control to lifted leg
      target_pose.position_[2] -= step_height; // Pose leg at step height to begin manipulation
    }
    else
    {
      target_pose = model_->getCurrentPose();
      target_pose.position_ -= manual_pose_.position_; // Remove manual pose
      target_pose.position_ += default_pose_.position_; // Add default pose as estimated from new loading pattern
    }

    Vector3d target_tip_position = target_pose.inverseTransformVector(leg_stepper->getDefaultTipPosition());

    // Set walker tip position for use in manual or walking mode
    if (leg->getLegState() == WALKING_TO_MANUAL)
    {
      leg_stepper->setCurrentTipPosition(target_tip_position);
    }
    else if (leg->getLegState() == MANUAL_TO_WALKING)
    {
      leg_stepper->setCurrentTipPosition(leg_stepper->getDefaultTipPosition());
    }

    progress = leg_poser->stepToPosition(target_tip_position, Pose::identity(), step_height, step_time);
    leg->setDesiredTipPosition(leg_poser->getCurrentTipPosition(), false);
    leg->applyIK(params_.debug_IK.data);
  }

  return progress;
}

/*******************************************************************************************************************//**
 * Iterate through legs in robot model and directly move joints into 'packed' configuration as defined by joint 
 * parameters. This maneuver occurs simultaneously for all legs in a time period defined by the input argument.
 * @param[in] time_to_pack The time period in which to execute the packing maneuver.
 * @return Returns an int from 0 to 100 signifying the progress of the sequence (100 meaning 100% complete)
***********************************************************************************************************************/
int PoseController::packLegs(const double& time_to_pack) //Simultaneous leg coordination
{
  int progress = 0; //Percentage progress (0%->100%)
  transition_step_ = 0; //Reset for startUp/ShutDown sequences

  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
    vector<double> target_joint_positions;
    JointContainer::iterator joint_it;
    for (joint_it = leg->getJointContainer()->begin(); joint_it != leg->getJointContainer()->end(); ++joint_it)
    {
      target_joint_positions.push_back(joint_it->second->packed_position_);
    }
    progress = leg_poser->moveToJointPosition(target_joint_positions, time_to_pack);
  }

  return progress;
}

/*******************************************************************************************************************//**
 * Iterate through legs in robot model and directly move joints into 'unpacked' configuration as defined by joint 
 * parameters. This maneuver occurs simultaneously for all legs in a time period defined by the input argument.
 * @param[in] time_to_unpack The time period in which to execute the packing maneuver.
 * @return Returns an int from 0 to 100 signifying the progress of the sequence (100 meaning 100% complete)
***********************************************************************************************************************/
int PoseController::unpackLegs(const double& time_to_unpack) //Simultaneous leg coordination
{
  int progress = 0; //Percentage progress (0%->100%)

  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
    vector<double> target_joint_positions;
    JointContainer::iterator joint_it;
    for (joint_it = leg->getJointContainer()->begin(); joint_it != leg->getJointContainer()->end(); ++joint_it)
    {
      target_joint_positions.push_back(joint_it->second->unpacked_position_);
    }

    progress = leg_poser->moveToJointPosition(target_joint_positions, time_to_unpack);
  }

  return progress;
}

/*******************************************************************************************************************//**
 * Depending on parameter flags, calls multiple posing functions and combines individual poses to update the current
 * desired pose of the robot model.
 * @param[in] body_height Desired height of the body above ground level - used in inclination posing.
***********************************************************************************************************************/
void PoseController::updateCurrentPose(const double& body_height)
{
  Pose new_pose = Pose::identity();

  // Manually set (joystick controlled) body pose
  if (params_.manual_posing.data)
  {
    updateManualPose();
    new_pose = new_pose.addPose(manual_pose_);
  }

  // Pose to align centre of gravity evenly between tip positions on incline
  if (params_.inclination_posing.data)
  {
    updateInclinationPose(body_height);
    new_pose = new_pose.addPose(inclination_pose_);
  }

  // Pose to offset average deltaZ from impedance controller and keep body at specificied height
  if (params_.impedance_control.data)
  {
    updateImpedancePose();
    new_pose = new_pose.addPose(impedance_pose_);
  }

  // Auto body pose using IMU feedback
  if (params_.imu_posing.data)
  {
    updateIMUPose();
    new_pose = new_pose.addPose(imu_pose_);
  }
  // Automatic (non-feedback) body posing
  else if (params_.auto_posing.data)
  {
    updateAutoPose();
    new_pose = new_pose.addPose(auto_pose_);
  }

  model_->setCurrentPose(new_pose);
}

/*******************************************************************************************************************//**
 * Generates a manual pose to be applied to the robot model, based on linear (x/y/z) and angular (roll/pitch/yaw)
 * velocity body posing inputs. Clamps the posing within set limits and resets the pose to zero in specified axes 
 * depending on the pose reset mode.
 * @bug Adding pitch and roll simultaneously adds unwanted yaw - fixed by moving away from using quat.h
***********************************************************************************************************************/
void PoseController::updateManualPose(void)
{
  Vector3d translation_position = manual_pose_.position_;
  Quat rotation_position = manual_pose_.rotation_;

  Vector3d default_translation = default_pose_.position_;
  Vector3d default_rotation = default_pose_.rotation_.toEulerAngles();

  Vector3d max_translation;
  max_translation[0] = params_.max_translation.data.at("x");
  max_translation[1] = params_.max_translation.data.at("y");
  max_translation[2] = params_.max_translation.data.at("z");
  Vector3d max_rotation;
  max_rotation[0] = params_.max_rotation.data.at("roll");
  max_rotation[1] = params_.max_rotation.data.at("pitch");
  max_rotation[2] = params_.max_rotation.data.at("yaw");

  bool reset_translation[3] = { false, false, false };
  bool reset_rotation[3] = { false, false, false };

  switch (pose_reset_mode_)
  {
    case (Z_AND_YAW_RESET):
      reset_translation[2] = true;
      reset_rotation[2] = true;
      break;

    case (X_AND_Y_RESET):
      reset_translation[0] = true;
      reset_translation[1] = true;
      break;

    case (PITCH_AND_ROLL_RESET):
      reset_rotation[0] = true;
      reset_rotation[1] = true;
      break;

    case (ALL_RESET):
      reset_translation[0] = true;
      reset_translation[1] = true;
      reset_translation[2] = true;
      reset_rotation[0] = true;
      reset_rotation[1] = true;
      reset_rotation[2] = true;
      break;

    case (IMMEDIATE_ALL_RESET):
      manual_pose_ = default_pose_;
      return;

    case (NO_RESET):  // Do nothing
    default:  // Do nothing
      break;
  }

  // Override posing velocity commands depending on pose reset mode
  for (int i = 0; i < 3; i++)  // For each axis (x,y,z)/(roll,pitch,yaw)
  {
    if (reset_translation[i])
    {
      if (translation_position[i] < default_translation[i])
      {
        translation_velocity_input_[i] = 1.0;
      }
      else if (translation_position[i] > default_translation[i])
      {
        translation_velocity_input_[i] = -1.0;
      }
    }

    if (reset_rotation[i])
    {
      if (rotation_position.toEulerAngles()[i] < default_rotation[i])
      {
        rotation_velocity_input_[i] = 1.0;
      }
      else if (rotation_position.toEulerAngles()[i] > default_rotation[i])
      {
        rotation_velocity_input_[i] = -1.0;
      }
    }
  }

  Vector3d translation_velocity = clamped(translation_velocity_input_, 1.0) * params_.max_translation_velocity.data;
  Vector3d rotation_velocity = clamped(rotation_velocity_input_, 1.0) * params_.max_rotation_velocity.data;

  Vector3d new_translation_position = translation_position + translation_velocity * params_.time_delta.data;
  Quat new_rotation_position = rotation_position * Quat(Vector3d(rotation_velocity * params_.time_delta.data));

  Vector3d translation_limit = Vector3d(0, 0, 0);
  Vector3d rotation_limit = Vector3d(0, 0, 0);

  // Zero velocity input depending on position limitations
  for (int i = 0; i < 3; i++)  // For each axis (x,y,z)/(roll,pitch,yaw)
  {
    // TRANSLATION
    // Assign correct translation limit based on velocity direction and reset command
    translation_limit[i] = sign(translation_velocity[i]) * max_translation[i];

    if (reset_translation[i] &&
        default_translation[i] < max_translation[i] &&
        default_translation[i] > -max_translation[i])
    {
      translation_limit[i] = default_translation[i];
    }

    bool positive_translation_velocity = sign(translation_velocity[i]) > 0;
    bool exceeds_positive_translation_limit = positive_translation_velocity &&
                                              (new_translation_position[i] > translation_limit[i]);
    bool exceeds_negative_translation_limit = !positive_translation_velocity &&
                                              (new_translation_position[i] < translation_limit[i]);

    // Zero velocity when translation position reaches limit
    if (exceeds_positive_translation_limit || exceeds_negative_translation_limit)
    {
      translation_velocity[i] = (translation_limit[i] - translation_position[i]) / params_.time_delta.data;
    }

    // ROTATION
    // Assign correct rotation limit based on velocity direction and reset command
    rotation_limit[i] = sign(rotation_velocity[i]) * max_rotation[i];

    if (reset_rotation[i] && default_rotation[i] < max_rotation[i] && default_rotation[i] > -max_rotation[i])
    {
      rotation_limit[i] = default_rotation[i];
    }

    bool positive_rotation_velocity = sign(rotation_velocity[i]) > 0;
    bool exceeds_positive_rotation_limit = positive_rotation_velocity &&
                                           (new_rotation_position.toEulerAngles()[i] > rotation_limit[i]);
    bool exceeds_negative_rotation_limit = !positive_rotation_velocity &&
                                           (new_rotation_position.toEulerAngles()[i] < rotation_limit[i]);

    // Zero velocity when rotation position reaches limit
    if (exceeds_positive_rotation_limit || exceeds_negative_rotation_limit)
    {
      rotation_velocity[i] = (rotation_limit[i] - rotation_position.toEulerAngles()[i]) / params_.time_delta.data;
    }
  }

  // Update position according to limitations
  manual_pose_.position_ = (translation_position + translation_velocity * params_.time_delta.data);
  manual_pose_.rotation_ = rotation_position * Quat(Vector3d(rotation_velocity * params_.time_delta.data));
  // BUG: ^Adding pitch and roll simultaneously adds unwanted yaw
}

/*******************************************************************************************************************//**
 * Updates the auto pose by feeding each Auto Poser object a phase value and combining the output of each Auto Poser
 * object into a single pose. The input master phase is either an iteration of the pose phase or synced to the step
 * phase from the Walk Controller. This function also iterates through all leg objects in the robot model and updates
 * each Leg Poser's specific Auto Poser pose (this pose is used when the leg needs to ignore the default auto pose)
 * @bug Adding pitch and roll simultaneously adds unwanted yaw - fixed by moving away from using quat.h
***********************************************************************************************************************/
void PoseController::updateAutoPose(void)
{
  shared_ptr<LegStepper> leg_stepper = auto_pose_reference_leg_->getLegStepper();
  auto_pose_ = Pose::identity();

  // Update auto posing state
  bool zero_body_velocity = leg_stepper->getStrideVector().norm() == 0;
  if (leg_stepper->getWalkState() == STARTING || leg_stepper->getWalkState() == MOVING)
  {
    auto_posing_state_ = POSING;
  }
  else if ((zero_body_velocity && leg_stepper->getWalkState() == STOPPING) || leg_stepper->getWalkState() == STOPPED)
  {
    auto_posing_state_ = STOP_POSING;
  }

  // Update master phase
  int master_phase;
  bool sync_with_step_cycle = (pose_frequency_ == -1.0);
  if (sync_with_step_cycle)
  {
    master_phase = leg_stepper->getPhase() + 1; // Correction for calculating auto pose before iterating walk phase
  }
  else
  {
    master_phase = pose_phase_;
    pose_phase_ = (pose_phase_ + 1) % pose_phase_length_; // Iterate pose phase
  }
  
  // Update auto pose from auto posers
  int auto_posers_complete = 0;
  AutoPoserContainer::iterator auto_poser_it;
  for (auto_poser_it = auto_poser_container_.begin(); auto_poser_it != auto_poser_container_.end(); ++auto_poser_it)
  {
    shared_ptr<AutoPoser> auto_poser = *auto_poser_it;
    Pose updated_pose = auto_poser->updatePose(master_phase);
    auto_posers_complete += int(!auto_poser->isPosing());
    auto_pose_ = auto_pose_.addPose(updated_pose);
    // BUG: ^Adding pitch and roll simultaneously adds unwanted yaw
  }

  // All auto posers have completed their required posing cycle (Allows walkController to transition to STARTING)
  if (auto_posers_complete == int(auto_poser_container_.size()))
  {
    auto_posing_state_ = POSING_COMPLETE;
  }

  // Update leg specific auto pose using leg posers
  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    shared_ptr<LegPoser> leg_poser = leg->getLegPoser();
    leg_poser->updateAutoPose(master_phase);
  }
}

/*******************************************************************************************************************//**
 * Attempts to generate a pose (pitch/roll rotation only) for the robot model to 'correct' any differences between the 
 * desired pose rotation and the that estimated by the IMU. A low pass filter is used to smooth out velocity inputs 
 * from the IMU and a basic PID controller is used to do control the output pose.
***********************************************************************************************************************/
void PoseController::updateIMUPose(void)
{
  Quat target_rotation = manual_pose_.rotation_;

  // There are two orientations per quaternion and we want the shorter/smaller difference.
  double dot = target_rotation.dot(~imu_data_.orientation);
  if (dot < 0.0)
  {
    target_rotation = -target_rotation;
  }

  // PID gains
  double kp = params_.rotation_pid_gains.data.at("p");
  double ki = params_.rotation_pid_gains.data.at("i");
  double kd = params_.rotation_pid_gains.data.at("d");

  rotation_position_error_ = imu_data_.orientation.toEulerAngles() - target_rotation.toEulerAngles();
  
  // Integration of angle position error (absement)
  rotation_absement_error_ += rotation_position_error_ * params_.time_delta.data;

  // Low pass filter of IMU angular velocity data
  double smoothingFactor = 0.15;
  rotation_velocity_error_ = smoothingFactor * imu_data_.angular_velocity +
                             (1 - smoothingFactor) * rotation_velocity_error_;

  Vector3d rotation_correction = -(kd * rotation_velocity_error_ +
                                   kp * rotation_position_error_ + 
                                   ki * rotation_absement_error_);
  
  rotation_correction[2] = target_rotation.toEulerAngles()[2];  // No compensation in yaw rotation

  if (rotation_correction.norm() > STABILITY_THRESHOLD)
  {
    ROS_FATAL("IMU rotation compensation became unstable! Adjust PID parameters.\n");
    ros::shutdown();
  }

  imu_pose_.rotation_ = Quat(rotation_correction);
}

/*******************************************************************************************************************//**
 * Attempts to generate a pose (x/y linear translation only) which shifts the assumed centre of gravity of the body to 
 * the vertically projected centre of the support polygon in accordance with the inclination of the terrain.
 * @param[in] body_height The desired height of the body centre, vertically from the ground.
***********************************************************************************************************************/
void PoseController::updateInclinationPose(const double& body_height)
{
  Quat compensation_combined = manual_pose_.rotation_ * auto_pose_.rotation_;
  Quat compensation_removed = imu_data_.orientation * compensation_combined.inverse();
  Vector3d euler_angles = compensation_removed.toEulerAngles();

  double lateral_correction = body_height * tan(euler_angles[0]);
  double longitudinal_correction = -body_height * tan(euler_angles[1]);

  double max_translation_x = params_.max_translation.data.at("x");
  double max_translation_y = params_.max_translation.data.at("y");
  longitudinal_correction = clamped(longitudinal_correction, -max_translation_x, max_translation_x);
  lateral_correction = clamped(lateral_correction, -max_translation_y, max_translation_y);

  inclination_pose_.position_[0] = longitudinal_correction;
  inclination_pose_.position_[1] = lateral_correction;
}

/*******************************************************************************************************************//**
 * Attempts to generate a pose (z linear translation only) which corrects for sagging of the body due to the impedance
 * controller and poses the body at the correct desired height above the ground.
***********************************************************************************************************************/
void PoseController::updateImpedancePose(void)
{
  int loaded_legs = model_->getLegCount();
  double average_delta_z = 0.0;

  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    average_delta_z += leg->getDeltaZ();
  }

  average_delta_z /= loaded_legs;
  
  double max_translation = params_.max_translation.data.at("z");
  impedance_pose_.position_[2] = clamped(abs(average_delta_z), -max_translation, max_translation);
}

/***********************************************************************************************************************
 * Attempts to generate a pose (x/y linear translation only) to position body such that there is a zero sum of moments 
 * from the force acting on the load bearing feet, allowing the robot to shift its centre of mass away from manually
 * manipulated (non-load bearing) legs and remain balanced.
***********************************************************************************************************************/
void PoseController::calculateDefaultPose(void)
{
  int legs_loaded = 0.0;
  int legs_transitioning_states = 0.0;
  
  // Return early if only one leg in model since pointless
  if (model_->getLegCount() == 1)
  {
    return;
  }

  // Check how many legs are load bearing and how many are transitioning states
  for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
  {
    shared_ptr<Leg> leg = leg_it_->second;
    LegState state = leg->getLegState();

    if (state == WALKING || state == MANUAL_TO_WALKING)
    {
      legs_loaded++;
    }

    if (state == MANUAL_TO_WALKING || state == WALKING_TO_MANUAL)
    {
      legs_transitioning_states++;
    }
  }

  // Only update the sum of moments if specific leg is WALKING and ALL other legs are in WALKING OR MANUAL state.
  if (legs_transitioning_states != 0.0)
  {
    if (recalculate_default_pose_)
    {
      Vector3d zero_moment_offset(0, 0, 0);

      for (leg_it_ = model_->getLegContainer()->begin(); leg_it_ != model_->getLegContainer()->end(); ++leg_it_)
      {
        shared_ptr<Leg> leg = leg_it_->second;
        shared_ptr<LegStepper> leg_stepper = leg->getLegStepper();
        LegState state = leg->getLegState();

        if (state == WALKING || state == MANUAL_TO_WALKING)
        {
          zero_moment_offset[0] += leg_stepper->getDefaultTipPosition()[0];
          zero_moment_offset[1] += leg_stepper->getDefaultTipPosition()[1];
        }
      }

      double max_translation_x = params_.max_translation.data.at("x");
      double max_translation_y = params_.max_translation.data.at("y");
      zero_moment_offset /= legs_loaded;
      zero_moment_offset[0] = clamped(zero_moment_offset[0], -max_translation_x, max_translation_x);
      zero_moment_offset[1] = clamped(zero_moment_offset[1], -max_translation_y, max_translation_y);

      default_pose_.position_[0] = zero_moment_offset[0];
      default_pose_.position_[1] = zero_moment_offset[1];
      recalculate_default_pose_ = false;
    }
  }
  else
  {
    recalculate_default_pose_ = true;
  }
}

/*******************************************************************************************************************//**
 * Auto poser contructor.
 * @param[in] poser Pointer to the Pose Controller object
 * @param[in] id Int defining the id number of the created Auto Poser object
***********************************************************************************************************************/
AutoPoser::AutoPoser(shared_ptr<PoseController> poser, const int& id)
  : poser_(poser)
  , id_number_(id)
{
}

/*******************************************************************************************************************//**
 * Returns a pose which contributes to the auto pose applied to the robot body. The resultant pose is defined by a 4th
 * order bezier curve for both linear position and angular rotation and iterated along using the phase input argument.
 * The characteristics of each bezier curves are defined by the user parameters in the auto_pose.yaml config file.
 * @param[in] phase The phase is the input value which is used to determine the progression along the bezier curve which
 * defines the output pose.
 * @return The component of auto pose contributed by this Auto Poser object's posing cycle defined by user parameters.
 * @see config/auto_pose.yaml
***********************************************************************************************************************/
Pose AutoPoser::updatePose(int phase)
{
  Pose return_pose = Pose::identity();
  int start_phase = start_phase_ * poser_->getNormaliser();
  int end_phase = end_phase_ * poser_->getNormaliser();
  
  // Changes start/end phases from zero to phase length value (which is equivalent)
  if (start_phase == 0)
  {
    start_phase = poser_->getPhaseLength();
  }  
  if (end_phase == 0)
  {
    end_phase = poser_->getPhaseLength();
  }

  //Handles phase overlapping master phase start/end
  if (start_phase > end_phase)
  {
    end_phase += poser_->getPhaseLength();
    if (phase < start_phase)
    {
      phase += poser_->getPhaseLength();
    }
  }

  PosingState state = poser_->getAutoPoseState();
  bool sync_with_step_cycle = (poser_->getPoseFrequency() == -1.0);

  // Coordinates starting/stopping of posing period
  // (posing only ends once a FULL posing cycle completes whilst in STOP_POSING state)
  start_check_ = !sync_with_step_cycle || (!start_check_ && state == POSING && phase == start_phase);
  end_check_.first = (end_check_.first || (state == STOP_POSING && phase == start_phase));
  end_check_.second = (end_check_.second || (state == STOP_POSING && phase == end_phase && end_check_.first));
  if (!allow_posing_ && start_check_) // Start posing
  {
    allow_posing_ = true;
    end_check_ = pair<bool, bool>(false, false);
  }
  else if (allow_posing_ && sync_with_step_cycle && end_check_.first && end_check_.second) // Stop posing
  {
    allow_posing_ = false;
    start_check_ = false;
  }

  // Pose if in correct phase
  if (phase >= start_phase && phase < end_phase && allow_posing_)
  {
    int iteration = phase - start_phase + 1;
    int num_iterations = end_phase - start_phase;

    Vector3d zero(0.0, 0.0, 0.0);
    Vector3d position_control_nodes[5] = {zero, zero, zero, zero, zero};
    Vector3d rotation_control_nodes[5] = {zero, zero, zero, zero, zero};
    
    bool first_half = iteration <= num_iterations / 2; // Flag for 1st vs 2nd half of posing cycle

    if (first_half)
    {
      position_control_nodes[3] = Vector3d(x_amplitude_, y_amplitude_, z_amplitude_);
      position_control_nodes[4] = Vector3d(x_amplitude_, y_amplitude_, z_amplitude_);
      rotation_control_nodes[3] = Vector3d(roll_amplitude_, pitch_amplitude_, yaw_amplitude_);
      rotation_control_nodes[4] = Vector3d(roll_amplitude_, pitch_amplitude_, yaw_amplitude_);
    }
    else
    {
      position_control_nodes[0] = Vector3d(x_amplitude_, y_amplitude_, z_amplitude_);
      position_control_nodes[1] = Vector3d(x_amplitude_, y_amplitude_, z_amplitude_);
      rotation_control_nodes[0] = Vector3d(roll_amplitude_, pitch_amplitude_, yaw_amplitude_);
      rotation_control_nodes[1] = Vector3d(roll_amplitude_, pitch_amplitude_, yaw_amplitude_);
    }

    double delta_t = 1.0 / (num_iterations / 2.0);
    int offset = (first_half ? 0 : num_iterations / 2.0); // Offsets iteration count for second half of posing cycle
    double time_input = (iteration - offset) * delta_t;

    Vector3d position = quarticBezier(position_control_nodes, time_input);
    Vector3d rotation = quarticBezier(rotation_control_nodes, time_input);

    return_pose = Pose(position, Quat(rotation));
    
    ROS_DEBUG_COND(false,
                  "AUTOPOSE_DEBUG %d - ITERATION: %d\t\t"
                  "TIME: %f\t\t"
                  "ORIGIN: %f:%f:%f\t\t"
                  "POS: %f:%f:%f\t\t"
                  "TARGET: %f:%f:%f\n",
                  id_number_, iteration, setPrecision(time_input, 3),
                  position_control_nodes[0][0], position_control_nodes[0][1], position_control_nodes[0][2],
                  position[0], position[1], position[2],
                  position_control_nodes[4][0], position_control_nodes[4][1], position_control_nodes[4][2]);
  }

  return return_pose;
}

/*******************************************************************************************************************//**
 * Leg poser contructor
 * @param[in] poser Pointer to the Pose Controller object
 * @param[in] leg Pointer to the parent leg object associated with the create Leg Poser object
***********************************************************************************************************************/
LegPoser::LegPoser(shared_ptr<PoseController> poser, shared_ptr<Leg> leg)
  : poser_(poser)
  , leg_(leg)
  , auto_pose_(Pose::identity())
  , current_tip_position_(Vector3d(0, 0, 0))
{
}

/*******************************************************************************************************************//**
 * Uses a bezier curve to smoothly update (over many iterations) the desired joint position of each joint in the leg 
 * associated with this Leg Poser object, from the original joint position at the first iteration of this function to 
 * the target joint position defined by the input argument. This maneuver completes after a time period defined by the 
 * input argument.
 * @param[in] target_joint_positions A vector of doubles defining the target joint positions of each joint of the parent
 * leg of this Leg Poser object in asscending joint id number order.
 * @param[in] time_to_move The time period in which to complete this maneuver.
 * @return Returns an int from 0 to 100 signifying the progress of the sequence (100 meaning 100% complete)
***********************************************************************************************************************/
int LegPoser::moveToJointPosition(const vector<double>& target_joint_positions, const double& time_to_move)
{
  // Setup origin and target joint positions for bezier curve
  if (first_iteration_)
  {
    origin_joint_positions_.clear();
    JointContainer::iterator joint_it;
    bool all_joints_at_target = true;
    int i = 0;
    for (joint_it = leg_->getJointContainer()->begin(); joint_it != leg_->getJointContainer()->end(); ++joint_it, ++i)
    {
      shared_ptr<Joint> joint = joint_it->second;
      all_joints_at_target = all_joints_at_target && 
                             abs(target_joint_positions[i] - joint->current_position_) < JOINT_TOLERANCE;
      origin_joint_positions_.push_back(joint->current_position_);
    }

    // Complete early if joint positions are already at target
    if (all_joints_at_target)
    {
      return PROGRESS_COMPLETE;
    }
    else
    {
      first_iteration_ = false;
      master_iteration_count_ = 0;
    }
  }

  int num_iterations = max(1, int(roundToInt(time_to_move / poser_->getParameters().time_delta.data)));
  double delta_t = 1.0 / num_iterations;

  master_iteration_count_++;

  JointContainer::iterator joint_it;
  vector<double> new_joint_positions;
  int i = 0;
  for (joint_it = leg_->getJointContainer()->begin(); joint_it != leg_->getJointContainer()->end(); ++joint_it, ++i)
  {
    shared_ptr<Joint> joint = joint_it->second;
    double control_nodes[4];
    control_nodes[0] = origin_joint_positions_[i];
    control_nodes[1] = origin_joint_positions_[i];
    control_nodes[2] = target_joint_positions[i];
    control_nodes[3] = target_joint_positions[i];
    joint->desired_position_ = cubicBezier(control_nodes, master_iteration_count_ * delta_t);
    new_joint_positions.push_back(joint->desired_position_);
  }

  leg_->applyFK();

  if (leg_->getIDNumber() == 1) //reference leg for debugging
  {
    double time = master_iteration_count_ * delta_t;
    bool debug = poser_->getParameters().debug_moveToJointPosition.data;
    ROS_DEBUG_COND(debug, "MOVE_TO_JOINT_POSITION DEBUG - MASTER ITERATION: %d\t\t"
                   "TIME: %f\t\t"
                   "ORIGIN: %f:%f:%f\t\t"
                   "CURRENT: %f:%f:%f\t\t"
                   "TARGET: %f:%f:%f\n",
                   master_iteration_count_, time,
                   origin_joint_positions_[0], origin_joint_positions_[1], origin_joint_positions_[2],
                   new_joint_positions[0], new_joint_positions[1], new_joint_positions[2],
                   target_joint_positions[0], target_joint_positions[1], target_joint_positions[2]);
  }

  //Return percentage of progress completion (0%->100%)
  int progress = int((double(master_iteration_count_ - 1) / double(num_iterations)) * PROGRESS_COMPLETE);

  // Complete once reached total number of iterations
  if (master_iteration_count_ >= num_iterations)
  {
    first_iteration_ = true;
    return PROGRESS_COMPLETE;
  }
  else
  {
    return progress;
  }
}

/*******************************************************************************************************************//**
 * Uses bezier curves to smoothly update (over many iterations) the desired tip position of the leg associated with 
 * this Leg Poser object, from the original tip position at the first iteration of this function to the target tip 
 * position defined by the input argument.
 * @param[in] target A 3d vector defining the target tip position in reference to the body centre frame
 * @param[in] target_pose A Pose to be linearly applied to the tip position over the course of the maneuver
 * @param[in] lift_height The height which the stepping leg trajectory should reach at its peak.
 * @param[in] time_to_step The time period to complete this maneuver.
 * @param[in] apply_delta_z A bool defining if a vertical offset value (generated by the impedance controller) should 
 * be applied to the target tip position.
 * @return Returns an int from 0 to 100 signifying the progress of the sequence (100 meaning 100% complete)
***********************************************************************************************************************/
int LegPoser::stepToPosition(const Vector3d& target, Pose target_pose,
                             const double& lift_height, const double& time_to_step, const bool& apply_delta_z)
{
  Vector3d target_tip_position = target;
  
  if (first_iteration_)
  {
    origin_tip_position_ = leg_->getCurrentTipPosition();

    // Complete early if target and origin positions are approximately equal
    if (abs(origin_tip_position_[0] - target_tip_position[0]) < TIP_TOLERANCE &&
        abs(origin_tip_position_[1] - target_tip_position[1]) < TIP_TOLERANCE &&
        abs(origin_tip_position_[2] - target_tip_position[2]) < TIP_TOLERANCE)
    {
      current_tip_position_ = target_tip_position;
      return PROGRESS_COMPLETE;
    }
    current_tip_position_ = origin_tip_position_;
    master_iteration_count_ = 0;
    first_iteration_ = false;
  }

  // Apply delta z to target tip position (used for transitioning to state using impedance control)
  bool manually_manipulated = (leg_->getLegState() == MANUAL || leg_->getLegState()  == WALKING_TO_MANUAL);
  if (apply_delta_z && !manually_manipulated)
  {
    target_tip_position[2] += leg_->getDeltaZ();
  }

  master_iteration_count_++;

  int num_iterations = max(1, int(roundToInt(time_to_step / poser_->getParameters().time_delta.data)));
  double delta_t = 1.0 / num_iterations;

  double completion_ratio = (double(master_iteration_count_ - 1) / double(num_iterations));

  // Applies required posing slowly over course of transition
  // Scales position vector by 0->1.0
  target_pose.position_ *= completion_ratio;
  // Scales rotation quat by 0.0->1.0 (https://en.wikipedia.org/wiki/Slerp)
  target_pose.rotation_ = Quat::Identity().slerpTo(target_pose.rotation_, completion_ratio);

  int half_swing_iteration = num_iterations / 2;

  // Update leg tip position
  Vector3d control_nodes_primary[5];
  Vector3d control_nodes_secondary[5];

  // Control nodes for dual 3d quartic bezier curves
  control_nodes_primary[0] = origin_tip_position_;
  control_nodes_primary[1] = origin_tip_position_;
  control_nodes_primary[2] = origin_tip_position_;
  control_nodes_primary[3] = target_tip_position + 0.75 * (origin_tip_position_ - target_tip_position);
  control_nodes_primary[4] = target_tip_position + 0.5 * (origin_tip_position_ - target_tip_position);
  control_nodes_primary[2][2] += lift_height;
  control_nodes_primary[3][2] += lift_height;
  control_nodes_primary[4][2] += lift_height;

  control_nodes_secondary[0] = target_tip_position + 0.5 * (origin_tip_position_ - target_tip_position);
  control_nodes_secondary[1] = target_tip_position + 0.25 * (origin_tip_position_ - target_tip_position);
  control_nodes_secondary[2] = target_tip_position;
  control_nodes_secondary[3] = target_tip_position;
  control_nodes_secondary[4] = target_tip_position;
  control_nodes_secondary[0][2] += lift_height;
  control_nodes_secondary[1][2] += lift_height;
  control_nodes_secondary[2][2] += lift_height;

  Vector3d new_tip_position;
  int swing_iteration_count = (master_iteration_count_ + (num_iterations - 1)) % (num_iterations) + 1;
  double time_input;

  // Calculate change in position using 1st/2nd bezier curve (depending on 1st/2nd half of swing)
  if (swing_iteration_count <= half_swing_iteration)
  {
    time_input = swing_iteration_count * delta_t * 2.0;
    new_tip_position = quarticBezier(control_nodes_primary, time_input);
  }
  else
  {
    time_input = (swing_iteration_count - half_swing_iteration) * delta_t * 2.0;
    new_tip_position = quarticBezier(control_nodes_secondary, time_input);
  }

  if (leg_->getIDNumber() == 0) //Reference leg for debugging (AR)
  {
    ROS_DEBUG_COND(poser_->getParameters().debug_stepToPosition.data,
                   "STEP_TO_POSITION DEBUG - LEG: %s\t\t"
                   "MASTER ITERATION: %d\t\t"
                   "TIME INPUT: %f\t\t"
                   "COMPLETION RATIO: %f\t\t"
                   "ORIGIN: %f:%f:%f\t\t"
                   "CURRENT: %f:%f:%f\t\t"
                   "TARGET: %f:%f:%f\n",
                   leg_->getIDName().c_str(),
                   master_iteration_count_,
                   time_input, completion_ratio,
                   origin_tip_position_[0], origin_tip_position_[1], origin_tip_position_[2],
                   new_tip_position[0], new_tip_position[1], new_tip_position[2],
                   target_tip_position[0], target_tip_position[1], target_tip_position[2]);
  }

  if (leg_->getLegState() != MANUAL)
  {
    current_tip_position_ = target_pose.inverseTransformVector(new_tip_position);
  }

  //Return ratio of completion (1.0 when fully complete)
  if (master_iteration_count_ >= num_iterations)
  {
    first_iteration_ = true;
    return PROGRESS_COMPLETE;
  }
  else
  {
    return int(completion_ratio * PROGRESS_COMPLETE);
  }
}

/*******************************************************************************************************************//**
 * Sets a pose for this Leg Poser which negates the default auto pose applied to the robot body. The negation pose is
 * defined by a 4th order bezier curve for both linear position and angular rotation and iterated along using the phase
 * input argument. The characteristics of each bezier curve are defined by the user parameters in the auto_pose.yaml
 * config file.
 * @param[in] phase The phase is the input value which is used to determine the progression along the bezier curves
 * which define the output pose.
 * @see config/auto_pose.yaml
***********************************************************************************************************************/
void LegPoser::updateAutoPose(const int& phase)
{
  int start_phase = pose_negation_phase_start_ * poser_->getNormaliser();
  int end_phase = pose_negation_phase_end_ * poser_->getNormaliser();
  int negation_phase = phase;
  
  // Changes start/end phases from zero to phase length value (which is equivalent)
  if (start_phase == 0)
  {
    start_phase = poser_->getPhaseLength();
  }
  if (end_phase == 0)
  {
    end_phase = poser_->getPhaseLength();
  }

  //Handles phase overlapping master phase start/end
  if (start_phase > end_phase)
  {
    end_phase += poser_->getPhaseLength();
    if (negation_phase < start_phase)
    {
      negation_phase += poser_->getPhaseLength();
    }
  }

  if (negation_phase >= start_phase && negation_phase < end_phase && !stop_negation_)
  {
    int iteration = negation_phase - start_phase + 1;
    int num_iterations = end_phase - start_phase;

    Vector3d zero(0.0, 0.0, 0.0);
    Vector3d position_amplitude = poser_->getAutoPose().position_;
    Vector3d rotation_amplitude = poser_->getAutoPose().rotation_.toEulerAngles();
    Vector3d position_control_nodes[5] = {zero, zero, zero, zero, zero};
    Vector3d rotation_control_nodes[5] = {zero, zero, zero, zero, zero};
    
    bool first_half = iteration <= num_iterations / 2; // Flag for 1st vs 2nd half of posing cycle

    if (first_half)
    {
      position_control_nodes[2] = position_amplitude;
      position_control_nodes[3] = position_amplitude;
      position_control_nodes[4] = position_amplitude;
      rotation_control_nodes[2] = rotation_amplitude;
      rotation_control_nodes[3] = rotation_amplitude;
      rotation_control_nodes[4] = rotation_amplitude;
    }
    else
    {
      position_control_nodes[0] = position_amplitude;
      position_control_nodes[1] = position_amplitude;
      position_control_nodes[2] = position_amplitude;
      rotation_control_nodes[0] = rotation_amplitude;
      rotation_control_nodes[1] = rotation_amplitude;
      rotation_control_nodes[2] = rotation_amplitude;
    }

		double delta_t = 1.0 / (num_iterations / 2.0);
    int offset = (first_half ? 0 : num_iterations / 2.0); // Offsets iteration count for second half of posing cycle
    double time_input = (iteration - offset) * delta_t;

    Vector3d position = quarticBezier(position_control_nodes, time_input);
    Vector3d rotation = quarticBezier(rotation_control_nodes, time_input);

    auto_pose_ = poser_->getAutoPose();
    auto_pose_ = auto_pose_.removePose(Pose(position, Quat(rotation)));
  }
  else
  {
    bool sync_with_step_cycle = (poser_->getPoseFrequency() == -1.0);
    stop_negation_ = (sync_with_step_cycle && poser_->getAutoPoseState() == STOP_POSING);
    auto_pose_ = poser_->getAutoPose();
  }
}

/***********************************************************************************************************************
***********************************************************************************************************************/