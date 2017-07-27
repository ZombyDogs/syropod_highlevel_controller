#ifndef SYROPOD_HIGHLEVEL_CONTROLLER_WALK_CONTROLLER_H
#define SYROPOD_HIGHLEVEL_CONTROLLER_WALK_CONTROLLER_H
/*******************************************************************************************************************//**
 *  @file    walk_controller.h
 *  @brief   Handles control of Syropod walking.
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
 **********************************************************************************************************************/

#include "standard_includes.h"
#include "parameters_and_states.h"
#include "pose.h"

#include "model.h"
#include "pose_controller.h"
#include "debug_output.h"

#define JOINT_POSITION_ITERATION 0.001 // Joint position iteration value used to find optimal angle (rad)

/*******************************************************************************************************************//**
 * This class handles top level management of the walk cycle state machine and calls each leg's LegStepper object to 
 * update tip trajectories. This class also handles generation of default walk stance tip positions, calculation of
 * maximum body velocities and accelerations and transformation of input desired body velocities to individual tip 
 * stride vectors.
***********************************************************************************************************************/
class WalkController : public enable_shared_from_this<WalkController>
{
public:
  /**
    * Constructor for the walk controller.
    * @param[in] model A pointer to the robot model.
    * @param[in] params A pointer to the parameter data structure.
    */
  WalkController(shared_ptr<Model> model, const Parameters& params);

  /** Accessor for pointer to parameter data structure. */
  inline const Parameters& getParameters(void) { return params_; };

  /** Accessor for step cycle phase length. */
  inline int getPhaseLength(void) { return phase_length_; };

  /** Accessor for phase for start of swing period of step cycle. */
  inline int getSwingStart(void) { return swing_start_; };

  /** Accessor for phase for end of swing period of step cycle. */
  inline int getSwingEnd(void) { return swing_end_; };

  /** Accessor for phase for start of stance period of step cycle. */
  inline int getStanceStart(void) { return stance_start_; };

  /** Accessor for phase for end of stance period of step cycle. */
  inline int getStanceEnd(void) { return stance_end_; };

  /** Accessor for ros cycle time period. */
  inline double getTimeDelta(void) { return time_delta_; };

  /** Accessor for step cycle frequency. */
  inline double getStepFrequency(void) { return step_frequency_; };

  /** Accessor for step clearance. */
  inline double getStepClearance(void) { return step_clearance_; };

  /** Accessor for step depth. */
  inline double getStepDepth(void) { return step_depth_; };

  /** Accessor for default body clearance above ground. */
  inline double getBodyHeight(void) { return body_clearance_; };

  /** Accessor for workspace radius. */
  inline double getWorkspaceRadius(void) { return workspace_radius_; };

  /** Accessor for desired linear body velocity. */
  inline Vector2d getDesiredLinearVelocity(void) { return desired_linear_velocity_; };

  /** Accessor for desired angular body velocity. */
  inline double getDesiredAngularVelocity(void) { return desired_angular_velocity_; };

  /** Accessor for stride length. */
  inline double getStrideLength(void) { return stride_length_; };

  /** Accessor for walk cycle state. */
  inline WalkState getWalkState(void) { return walk_state_; };

  /** Resets joint orientation tracking variables to defaults. */
  inline void resetJointOrientationTracking(void) { joint_twist_ = 0.0, ground_bearing_ = 1.57; };

  /**
    * Modifier for posing state.
    * @param[in] state  The new posing state.
    */
  inline void setPoseState(const PosingState& state) { pose_state_ = state; };

  /**
    * Initialises walk controller by calculating default walking stance tip positions and creating LegStepper objects 
    * for each leg. In this process calculates various ancilary variables such as workspace radius and maximum body 
    * height.
    * @todo Redesign algorithm for generating default stance tip positions to work for all form factors.
    * @todo Implement leg span scale parameter to change the 'width' of the stance.
    */
  void init(void);

  /**
    * Calculates walk controller walk cycle parameters, normalising base parameters according to step frequency.
    * Calculates max accelerations and speeds from scaled workspaces which accomodate overshoot.
    */
  void setGaitParams(void);

  /**
    * Updates all legs in the walk cycle. Calculates stride vectors for all legs from robot body velocity inputs and 
    * calls trajectory update functions for each leg to update individual tip positions. Also manages the overall walk
    * state via state machine and input velocities as well as the individual step state of each leg as they progress
    * through stance and swing states.
    * @params[in] linear_velocity_input An input for the desired linear velocity of the robot body in the x/y plane.
    * @params[in] angular_velocity_input An input for the desired angular velocity of the robot body about the z axis.
    */
  void updateWalk(const Vector2d& linear_velocity_input, const double& angular_velocity_input);

  /**
    * Updates the tip position for legs in the manual state from tip velocity inputs. Two modes are available: joint 
    * control allows manipulation of joint positions directly but only works for 3DOF legs; tip control allows 
    * manipulation of the tip in cartesian space in the robot frame.
    * @params[in] primary_leg_selection_ID The designation of a leg selected (in the primary role) for manipulation.
    * @params[in] primary_tip_velocity_input The velocity input to move the 1st leg tip position in the robot frame.
    * @params[in] secondary_leg_selection_ID The designation of a leg selected (in the secondary role) for manipulation.
    * @params[in] secondary_tip_velocity_input The velocity input to move the 2nd leg tip position in the robot frame.
    */
  void updateManual(const int& primary_leg_selection_ID, const Vector3d& primary_tip_velocity_input,
                    const int& secondary_leg_selection_ID, const Vector3d& secondary_tip_velocity_input);

private:
  shared_ptr<Model> model_;  ///! Pointer to robot model object.
  const Parameters& params_; ///! Pointer to parameter data structure for storing parameter variables.
  double time_delta_;        ///! The time period of the ros cycle.

  WalkState walk_state_ = STOPPED;           ///! The current walk cycle state.
  PosingState pose_state_ = POSING_COMPLETE; ///! The current state of auto posing.

  //Joint orientation tracking variables
  double joint_twist_;    ///! Cummulative twist of successive joints in a leg used to track orientation.
  double ground_bearing_; ///! The current bearing to the ground from the tracked joint.

  // Walk parameters
  double step_frequency_; ///! The frequency of the step cycle.
  double step_clearance_; ///! The desired clearance of the leg tip above default position during swing period.
  double step_depth_;     ///! The desired depth of the leg tip below default position during stance period.
  double body_clearance_; ///! The desired clearance of the body above the default tip positions.

  // Gait cycle parameters
  int phase_length_;  ///! The phase length of the step cycle.
  int swing_length_;  ///! The phase length of the swing period of the step cycle.
  int stance_length_; ///! The phase length of the stance period of the step cycle.
  int stance_end_;    ///! The phase at which the stance period ends.
  int swing_start_;   ///! The phase at which the swing period starts.
  int swing_end_;     ///! The phase at which the swing period ends.
  int stance_start_;  ///! The phase at which the stance period starts.

  // Workspace variables
  double maximum_body_height_ = UNASSIGNED_VALUE; ///! The maximum body height the robot model is able to achieve.
  double workspace_radius_;                       ///! The radius of the circle encompassing allowable workspace.
  double stride_length_ = 0.0;                    ///! The current length of the stride vector.
  double stance_radius_;                          ///! The radius of the turning circle used for angular body velocity.

  // Velocity/acceleration variables
  Vector2d desired_linear_velocity_;      ///! The desired linear velocity of the robot body.
  double desired_angular_velocity_;       ///! The desired angular velocity of the robot body.
  double max_linear_speed_ = 0.0;         ///! The max allowable linear speed of the robot body.
  double max_angular_speed_ = 0.0;        ///! The max allowable angular speed of the robot body.
  double max_linear_acceleration_ = 0.0;  ///! The max allowable linear acceleration of the robot body.
  double max_angular_acceleration_ = 0.0; ///! The max allowable angular acceleration of the robot body.

  // Leg coordination variables
  int legs_at_correct_phase_ = 0;     ///! A count of legs currently at the correct phase per the walk cycle state.
  int legs_completed_first_step_ = 0; ///! A count of legs whcih have currently completed their first step.

  // Iteration variables
  LegContainer::iterator leg_it_;     ///! Leg iteration member variable used to minimise code
  JointContainer::iterator joint_it_; ///! Joint iteration member variable used to minimise code.
  LinkContainer::iterator link_it_;   ///! Link iteration member variable used to minimise code.

public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

/*******************************************************************************************************************//**
 * This class handles the generation of leg tip trajectory generation and updating the desired tip position along this
 * trajectory during iteration of the step cycle. Trajectories are generated using 3 bezier curves: a primary and
 * secondary curve for the swing period of the step cycle and one for the stance period of the step cycle. 
 * Characteristics of the step trajectory are defined by parameters such as: step frequency, step clearance, step depth
 * and an input stride vector which is calculated from robot morphology and input desired body velocities.
***********************************************************************************************************************/
class LegStepper
{
public:
  /**
    * Leg stepper object constructor, initialises member variables from walk controller.
    * @param[in] walker A pointer to the walk controller.
    * @param[in] leg A pointer to the parent leg object.
    * @param[in] identity_tip_position The default walking stance tip position about which the step cycle is based.
    */
  LegStepper(shared_ptr<WalkController> walker, shared_ptr<Leg> leg, const Vector3d& identity_tip_position);

  /** Accessor for the current tip position according to the walk controller. */
  inline Vector3d getCurrentTipPosition(void) { return current_tip_position_; };
  
  /** Accessor for the default tip position according to the walk controller. */
  inline Vector3d getDefaultTipPosition(void) { return default_tip_position_;};
  
  /** Accessor for the current state of the walk cycle. */
  inline WalkState getWalkState(void) { return walker_->getWalkState(); };
  
  /** Accessor for the current state of the step cycle. */
  inline StepState getStepState(void) { return step_state_; };
  
  /** Accessor for the current phase of the step cycle. */
  inline int getPhase(void) { return phase_; };
  
  /** Accessor for the current phase offset of the step cycle.  */
  inline int getPhaseOffset(void) { return phase_offset_; };
  
  /** Accessor for the current stride vector used in the step cycle. */
  inline Vector2d getStrideVector(void) { return Vector2d(stride_vector_[0], stride_vector_[1]); };
  
  /** Accessor for desired height of the leg tip above default position during swing period. */
  inline double getSwingHeight(void) { return swing_height_; };
  
  /** Accessor for the current progress of the swing period in the step cycle (0.0 -> 1.0 || -1.0). */
  inline double getSwingProgress(void) { return swing_progress_; };
  
  /** Accessor for the current progress of the stance period in the step cycle (0.0 -> 1.0 || -1.0) */
  inline double getStanceProgress(void) { return stance_progress_; };
  
  /** Returns true if leg has completed its first step whilst the walk state transitions from STOPPED to MOVING. */
  inline bool hasCompletedFirstStep(void) { return completed_first_step_; };
  
  /** Returns true if leg is in the correct step cycle phase per the walk controller state. */
  inline bool isAtCorrectPhase(void) { return at_correct_phase_; };

  /**
    * Accessor for control nodes in the primary swing bezier curve.
    * @param[in] i Index of the control node.
    */
  inline Vector3d getSwing1ControlNode(const int& i) { return swing_1_nodes_[i]; };
  
  /**
    * Accessor for control nodes in the secondary swing bezier curve.
    * @param[in] i Index of the control node.
    */
  inline Vector3d getSwing2ControlNode(const int& i) { return swing_2_nodes_[i]; };
  
  /**
    * Accessor for control nodes in the stance bezier curve.
    * @param[in] i Index of the control node.
    */
  inline Vector3d getStanceControlNode(const int& i) { return stance_nodes_[i]; };

  /**
    * Modifier for the current tip position according to the walk controller.
    * @param[in] current_tip_position The new current tip position.
    */
  inline void setCurrentTipPosition(const Vector3d& current_tip_position) 
  { 
    current_tip_position_ = current_tip_position; 
  };
  
  /**
    * Modifier for the default tip position according to the walk controller.
    * @param[in] tip_position The new default tip position.
    */
  inline void setDefaultTipPosition(const Vector3d& tip_position) { default_tip_position_ = tip_position; };
  
  /**
    * Modifier for the current state of step cycle.
    * @param[in] step_state The new state of the step cycle.
    */
  inline void setStepState(const StepState& step_state) { step_state_ = step_state; };
  
  /**
    * Modifier for the phase of the step cycle.
    * @param[in] phase The new phase.
    */
  inline void setPhase(const int& phase) { phase_ = phase; };
  
  /**
    * Modifier for the phase offset of the step cycle.
    * @param[in] phase_offset The new phase offset.
    */
  inline void setPhaseOffset(const int& phase_offset) { phase_offset_ = phase_offset;};
  
  /**
    * Modifier for the flag denoting if the leg has completed its first step.
    * @param[in] completed_first_step The new value for the flag.
    */
  inline void setCompletedFirstStep(const bool& completed_first_step) { completed_first_step_ = completed_first_step; };
  
  /**
    * Modifier for the flag denoting if the leg in in the correct phase.
    * @param[in] at_correct_phase The new value for the flag.
    */
  inline void setAtCorrectPhase(const bool& at_correct_phase) { at_correct_phase_ = at_correct_phase; };
  
  /**
    * Updates the stride vector with a new value.
    * @param[in] stride_vector The new stride vector.
    */
  inline void updateStride(const Vector2d& stride_vector)
  {
    stride_vector_ = Vector3d(stride_vector[0], stride_vector[1], 0.0);
  };

  /** Iterates the step phase and updates the progress variables */
  void iteratePhase(void);
  
  /**
    * Updates position of tip using three quartic bezier curves to generate the tip trajectory. Calculates change in 
    * tip position using two bezier curves for swing phase and one for stance phase. Each Bezier curve uses 5 control
    * nodes designed specifically to give a C2 smooth trajectory for the entire step cycle.
    */
  void updatePosition(void);
  
  /**
    * Generates control nodes for quartic bezier curve of the 1st half of the swing tip trajectory calculation.
    * @param[in] initial_tip_velocity Tip velocity vector representing the expected velocity of the tip at the beginning
    * of swing trajectory generation.
    */
  void generatePrimarySwingControlNodes(const Vector3d& initial_tip_velocity);
  
  /**
    * Generates control nodes for quartic bezier curve of the 2nd half of the swing tip trajectory calculation.
    * @param[in] final_tip_velocity Tip velocity vector representing the expected velocity of the tip at the end
    * of swing trajectory generation.
    */
  void generateSecondarySwingControlNodes(const Vector3d& final_tip_velocity);
  
  /**
    * Generates control nodes for quartic bezier curve of stance tip trajectory calculation.
    * @param[in] stride_vector A vector defining the stride which the leg is to step as part of the step cycle
    * trajectory.
    */
  void generateStanceControlNodes(const Vector3d& stride_vector);

private:
  shared_ptr<WalkController> walker_; ///! Pointer to walk controller object.
  shared_ptr<Leg> leg_;               ///! Pointer to the parent leg object.

  bool at_correct_phase_ = false;     ///! Flag denoting if the leg is at the correct phase per the walk state.
  bool completed_first_step_ = false; ///! Flag denoting if the leg has completed its first step.

  int phase_ = 0;    ///! Step cycle phase.
  int phase_offset_; ///! Step cycle phase offset.

  double swing_progress_ = -1.0;  ///! The progress of the swing period in the step cycle. (0.0->1.0 || -1.0)
  double stance_progress_ = -1.0; ///! The progress of the stance period in the step cycle. (0.0->1.0 || -1.0)

  StepState step_state_ = STANCE; ///! The state of the step cycle.

  Vector3d swing_1_nodes_[5]; ///! An array of 3d control nodes defining the primary swing bezier curve.
  Vector3d swing_2_nodes_[5]; ///! An array of 3d control nodes defining the secondary swing bezier curve.
  Vector3d stance_nodes_[5];  ///! An array of 3d control nodes defining the stance bezier curve.

  Vector3d stride_vector_; ///! The desired stride vector.
  double swing_height_;    ///! The desired height of the leg tip above default position during swing period.
  
  double swing_delta_t_ = 0.0;
  double stance_delta_t_ = 0.0;

  Vector3d default_tip_position_;       ///! The default tip position per the walk controller.
  Vector3d current_tip_position_;       ///! The current tip position per the walk controller.
  Vector3d current_tip_velocity_;       ///! The default tip velocity per the walk controller.
  Vector3d swing_origin_tip_position_;  ///! The tip position used as the origin for the bezier curve during swing.
  Vector3d stance_origin_tip_position_; ///! The tip position used as the origin for the bezier curve during stance.
  
public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

/***********************************************************************************************************************
***********************************************************************************************************************/
#endif /* SYROPOD_HIGHLEVEL_CONTROLLER_WALK_CONTROLLER_H */