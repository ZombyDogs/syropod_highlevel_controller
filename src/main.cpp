/* (c) Copyright CSIRO 2013. Author: Thomas Lowe
   This software is provided under the terms of Schedule 1 of the license agreement between CSIRO, 3DLM and GeoSLAM.
*/
#include "../include/simple_hexapod_controller/standardIncludes.h"
#include "../include/simple_hexapod_controller/model.h"
#include "../include/simple_hexapod_controller/tripodWalk.h"
#include "../include/simple_hexapod_controller/debugOutput.h"
#include "../include/simple_hexapod_controller/motorInterface.h"
#include <boost/concept_check.hpp>
#include <iostream>
#include <sys/select.h>
#include "geometry_msgs/Twist.h"

static Vector2d localVelocity(0,0);
static double turnRate = 0;

// source catkin_ws/devel/setup.bash
// roslaunch hexapod_teleop hexapod_controllers.launch

void joypadChangeCallback(const geometry_msgs::Twist &twist)
{
  ASSERT(twist.angular.z < 0.51);
 // ASSERT(twist.linear.y < 0.51);
  // these are 0 to 5 for some reason, so multiply by 0.2
  localVelocity = Vector2d(twist.angular.y, twist.angular.z) * 2.0;
  turnRate = -twist.linear.y*0.2;
}

int main(int argc, char* argv[])
{
  ros::init(argc, argv, "Hexapod");
  ros::NodeHandle n;
  
  Model hexapod;
  Vector3d yawOffsets(0.77,0,-0.77);
  GaitController walker(&hexapod, 1, 0.5, 0.12, yawOffsets, Vector3d(1.4,1.4,1.4), 2.2);
  DebugOutput debug;

  std_msgs::Float64 angle;  
  dynamixel_controllers::SetSpeed speed;

  MotorInterface interface;  
  speed.request.speed=0.5;
  interface.setupSpeed(speed);

  ros::Subscriber subscriber = n.subscribe("/desired_body_velocity", 1, joypadChangeCallback);
  ros::Rate r(roundToInt(1.0/timeDelta));         //frequency of the loop. 
  double t = 0;
  
  while (ros::ok())
  {
    walker.update(localVelocity, turnRate);
    debug.drawRobot(walker.pose, hexapod.legs[0][0].rootOffset, hexapod.getJointPositions(walker.pose), Vector4d(1,1,1,1));
    debug.drawPoints(walker.targets, Vector4d(1,0,0,1));

//    if (false)
    {
      std_msgs::Float64 angle;
      for (int s = 0; s<2; s++)
      {
        double dir = s==0 ? -1 : 1;
        for (int l = 0; l<3; l++)
        {
          angle.data = dir*(walker.model->legs[l][s].yaw - yawOffsets[l]);
          interface.setTargetAngle(l, s, 0, angle);
          angle.data = -dir*walker.model->legs[l][s].liftAngle;
          interface.setTargetAngle(l, s, 1, angle);
          angle.data = dir*walker.model->legs[l][s].kneeAngle;
          interface.setTargetAngle(l, s, 2, angle);
        }
      }
    }
    ros::spinOnce();
    r.sleep();

    debug.reset();
    t += timeDelta;
  }
}
