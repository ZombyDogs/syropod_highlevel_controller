#include "../include/simple_hexapod_controller/walkController.h"

/***********************************************************************************************************************
 * Generates control nodes for each quartic bezier curve of swing tip trajectory calculation 
***********************************************************************************************************************/
void WalkController::LegStepper::generateSwingControlNodes(Vector3d strideVector)
{
  double swingHeight = walker->stepClearance*walker->maximumBodyHeight;
  
  //Used to scale the difference between control nodes for the stance curve which has differing delta time values to swing curves
  double bezierScaler = stanceDeltaT/swingDeltaT;
  
  //Control nodes for swing quartic bezier curves - horizontal plane
  swing1ControlNodes[0] = swingOriginTipPosition;         									//Set for horizontal position continuity at transition between stance and primary swing curves (C0 Smoothness)     
  swing1ControlNodes[1] = swing1ControlNodes[0] + bezierScaler*(stanceControlNodes[4]-stanceControlNodes[3]);			//Set for horizontal velocity continuity at transition between stance and primary swing curves (C1 Smoothness)
  swing1ControlNodes[2] = swing1ControlNodes[1] + (swing1ControlNodes[1]-swing1ControlNodes[0]);				//Set for horizontal acceleration continuity at transition between stance and primary swing curves (C2 Smoothness)  
  swing1ControlNodes[4] = defaultTipPosition;											//Set equal to default tip position so that max swing height and transition to 2nd swing curve always occurs at default tip position    
  swing1ControlNodes[3] = (swing1ControlNodes[2]+swing1ControlNodes[4])/2.0;							//Set for horizontal acceleration continuity at transition between primary and secondary swing curves (C2 Smoothness) (symetrical swing curves only!) 
  
  swing2ControlNodes[0] = swing1ControlNodes[4];										//Set for horizontal position continuity at transition between primary and secondary swing curves (C0 Smoothness)
  swing2ControlNodes[1] = swing2ControlNodes[0] + (swing2ControlNodes[0]-swing1ControlNodes[3]);				//Set for horizontal velocity continuity at transition between primary and secondary swing curves (C1 Smoothness)
  swing2ControlNodes[3] = stanceControlNodes[0] + bezierScaler*(stanceControlNodes[0]-stanceControlNodes[1]);			//Set for horizontal velocity continuity at transition between secondary swing and stance curves (C1 Smoothness)
  swing2ControlNodes[2] = swing2ControlNodes[3] + swing2ControlNodes[3]-stanceControlNodes[0];					//Set for horizontal acceleration continuity at transition between secondary swing and stance curves (C2 Smoothness)
  swing2ControlNodes[4] = stanceControlNodes[0];										//Set for horizontal position continuity at transition between secondary swing and stance curves (C0 Smoothness)
  
  //Control nodes for swing quartic bezier curves - vertical plane
  swing1ControlNodes[0][2] = swingOriginTipPosition[2];										//Set for vertical position continuity at transition between stance and primary swing curves (C0 Smoothness)     
  swing1ControlNodes[1][2] = swing1ControlNodes[0][2] + bezierScaler*(stanceControlNodes[4][2]-stanceControlNodes[3][2]);	//Set for vertical velocity continuity at transition between stance and primary swing curves (C1 Smoothness)
  swing1ControlNodes[4][2] = swing1ControlNodes[0][2] + swingHeight;								//Set equal to default tip position plus swing height so that max swing height and transition to 2nd swing curve always occurs at default tip position
  swing1ControlNodes[2][2] = swing1ControlNodes[0][2] + 2.0*bezierScaler*(stanceControlNodes[4][2]-stanceControlNodes[3][2]);	//Set for vertical acceleration continuity at transition between stance and primary swing curves (C2 Smoothness)
  swing1ControlNodes[3][2] = swing1ControlNodes[4][2];										//Set for vertical velocity continuity at transition between primary and secondary swing curves (C1 Smoothness)

  swing2ControlNodes[0][2] = swing1ControlNodes[4][2];										//Set for vertical position continuity at transition between primary and secondary swing curves (C0 Smoothness)
  swing2ControlNodes[1][2] = swing2ControlNodes[0][2];										//Set for vertical velocity continuity at transition between primary and secondary swing curves (C1 Smoothness)
  swing2ControlNodes[2][2] = stanceControlNodes[0][2] + 2.0*bezierScaler*(stanceControlNodes[0][2]-stanceControlNodes[1][2]);	//Set for vertical acceleration continuity at transition between secondary swing and stance curves (C2 Smoothness)
  swing2ControlNodes[3][2] = stanceControlNodes[0][2] + bezierScaler*(stanceControlNodes[0][2]-stanceControlNodes[1][2]);	//Set for vertical velocity continuity at transition between secondary swing and stance curves (C1 Smoothness)					  
  swing2ControlNodes[4][2] = stanceControlNodes[0][2];										//Set for vertical position continuity at transition between secondary swing and stance curves (C0 Smoothness)
}

/***********************************************************************************************************************
 * Generates control nodes for quartic bezier curve of stance tip trajectory calculation 
***********************************************************************************************************************/
void WalkController::LegStepper::generateStanceControlNodes(Vector3d strideVector)
{
  double stanceDepth = walker->stepDepth*walker->maximumBodyHeight;
  
  //Control nodes for stance quartic bezier curve - horizontal plane
  stanceControlNodes[0] = stanceOriginTipPosition;							//Set as initial horizontal tip position  
  stanceControlNodes[4] = stanceOriginTipPosition - strideVector;					//Set as target horizontal tip position 
  stanceControlNodes[1] = stanceControlNodes[4] + 0.75*(stanceControlNodes[0] - stanceControlNodes[4]);	//Set for constant horizontal velocity in stance phase
  stanceControlNodes[2] = stanceControlNodes[4] + 0.5*(stanceControlNodes[0] - stanceControlNodes[4]);	//Set for constant horizontal velocity in stance phase
  stanceControlNodes[3] = stanceControlNodes[4] + 0.25*(stanceControlNodes[0] - stanceControlNodes[4]);	//Set for constant horizontal velocity in stance phase;

  //Control nodes for stance quartic bezier curve - vertical plane
  stanceControlNodes[0][2] = stanceOriginTipPosition[2];						//Set as initial vertical tip position
  stanceControlNodes[4][2] = defaultTipPosition[2];							//Set as target vertical tip position 
  stanceControlNodes[2][2] = stanceControlNodes[0][2] - stanceDepth;					//Set to control depth below ground level of stance trajectory, defined by stanceDepth
  stanceControlNodes[1][2] = (stanceControlNodes[0][2] + stanceControlNodes[2][2])/2.0;			//Set for vertical acceleration continuity at transition between secondary swing and stance curves (C2 Smoothness)
  stanceControlNodes[3][2] = (stanceControlNodes[4][2] + stanceControlNodes[2][2])/2.0;			//Set for vertical acceleration continuity at transition between stance and primary swing curves (C2 Smoothness)  
}

/***********************************************************************************************************************
 * Calculates time deltas for use in quartic bezier curve tip trajectory calculations
***********************************************************************************************************************/
double WalkController::LegStepper::calculateDeltaT(StepState state, int length)
{
  int numIterations = roundToInt((double(length)/walker->phaseLength)/(walker->stepFrequency*walker->timeDelta)/2.0)*2.0;  //Ensure compatible number of iterations 
  if (state == SWING)
  {
    return 2.0/numIterations;
  }
  else
  {
    return 1.0/numIterations;
  }
}

/***********************************************************************************************************************
 * Updates position of tip using tri-quartic bezier curve tip trajectory engine. Calculates change in tip position using
 * the derivatives of three quartic bezier curves, two for swing phase and one for stance phase. Each Bezier curve uses 
 * 5 control nodes designed specifically to give a C2 smooth trajectory for the entire step cycle.  
***********************************************************************************************************************/
void WalkController::LegStepper::updatePosition()
{      
  // Swing Phase
  if (state == SWING)
  {
    int iteration = phase-walker->swingStart+1;
    double swingLength = walker->swingEnd - walker->swingStart;
    swingDeltaT = calculateDeltaT(state, swingLength);
    int numIterations = 2.0/swingDeltaT;
    
    //Save initial tip position at beginning of swing
    if (iteration == 1)
    {
      swingOriginTipPosition = currentTipPosition;
    }
    
    //Calculate change in position using 1st/2nd bezier curve (depending on 1st/2nd half of swing)
    Vector3d deltaPos;
    double t1, t2;
    Vector3d strideVec = Vector3d(strideVector[0], strideVector[1], 0.0);
    
    if (iteration <= numIterations/2)
    {
      generateSwingControlNodes(strideVec);
      t1 = iteration*swingDeltaT;
      deltaPos = swingDeltaT*quarticBezierDot(swing1ControlNodes, t1); //
    }
    else
    {
      //Update values of NEXT stance curve for use in calculation of secondary swing control nodes
      int stanceStart = walker->swingEnd;
      int stanceEnd = walker->swingStart;
      int stanceLength = mod(stanceEnd-stanceStart, walker->phaseLength);
      stanceDeltaT = calculateDeltaT(STANCE, stanceLength); 
      stanceOriginTipPosition = defaultTipPosition + 0.5*strideVec;
      generateStanceControlNodes(strideVec);
      
      generateSwingControlNodes(strideVec);
      t2 = (iteration-numIterations/2)*swingDeltaT;
      deltaPos = swingDeltaT*quarticBezierDot(swing2ControlNodes, t2);
    }    
    
    currentTipPosition += deltaPos; 
    tipVelocity = deltaPos/walker->timeDelta;
    
    if (t1 < swingDeltaT) {t1=0.0;}
    if (t2 < swingDeltaT) {t2=0.0;}
    if (&(walker->legSteppers[0][0]) == this) //Front left leg
    {
      ROS_DEBUG_COND(params->debugUpdateSwingPosition, "SWING TRAJECTORY_DEBUG - ITERATION: %d\t\tTIME: %f:%f\t\tORIGIN: %f:%f:%f\t\tPOS: %f:%f:%f\t\tTARGET: %f:%f:%f\n", 
		    iteration, t1, t2,
		    swingOriginTipPosition[0], swingOriginTipPosition[1], swingOriginTipPosition[2],
		    currentTipPosition[0], currentTipPosition[1], currentTipPosition[2],
		    swing2ControlNodes[4][0], swing2ControlNodes[4][1], swing2ControlNodes[4][2]); 
    }
  }  
  // Stance phase
  else if (state == STANCE)
  {      
    int stanceStart = completedFirstStep ? walker->swingEnd : phaseOffset;
    int stanceEnd = walker->swingStart;
    int stanceLength = mod(stanceEnd-stanceStart, walker->phaseLength);
    stanceDeltaT = calculateDeltaT(STANCE, stanceLength);
    
    int iteration = mod(phase+(walker->phaseLength-stanceStart), walker->phaseLength)+1;
    
    //Save initial tip position at beginning of swing
    if (iteration == 1)
    {
      stanceOriginTipPosition = currentTipPosition;
    }    
    
    //Calculate change in position using 1st/2nd bezier curve (depending on 1st/2nd half of swing)
    Vector3d deltaPos;
    double t;
    
    
    //Scales stride vector according to stance length specifically for STARTING state of walker
    Vector3d strideVec = Vector3d(strideVector[0], strideVector[1], 0.0);
    strideVec *= double(stanceLength)/(mod(walker->swingStart-walker->swingEnd,walker->phaseLength));
    
    generateStanceControlNodes(strideVec);
    t = iteration*stanceDeltaT;
    deltaPos = stanceDeltaT*quarticBezierDot(stanceControlNodes, t);
    
    currentTipPosition += deltaPos; 
    tipVelocity = deltaPos/walker->timeDelta;
    
    if (t < stanceDeltaT) {t=0.0;}
    if (&(walker->legSteppers[0][0]) == this) //Front left leg
    {
      ROS_DEBUG_COND(params->debugUpdateSwingPosition, "STANCE TRAJECTORY_DEBUG - ITERATION: %d\t\tTIME: %f\t\tORIGIN: %f:%f:%f\t\tPOS: %f:%f:%f\t\tTARGET: %f:%f:%f\n", 
		    iteration, t,
		    stanceOriginTipPosition[0], stanceOriginTipPosition[1], stanceOriginTipPosition[2],
		    currentTipPosition[0], currentTipPosition[1], currentTipPosition[2],
		    stanceControlNodes[4][0], stanceControlNodes[4][1], stanceControlNodes[4][2]); 
    }    
  }  
}

/***********************************************************************************************************************
 * Determines the basic stance pose which the hexapod will try to maintain, by 
 * finding the largest footprint radius that each leg can achieve for the 
 * specified level of clearance.
***********************************************************************************************************************/
WalkController::WalkController(Model *model, Parameters p)
{ 
  init(model, p);
}

void WalkController::init(Model *m, Parameters p)
{
  model = m;
  params = p;
  
  stepClearance = params.stepClearance;
  stepDepth = params.stepDepth;
  bodyClearance = params.bodyClearance;
  timeDelta = params.timeDelta;
  
  setGaitParams(p);
  
  ASSERT(stepClearance >= 0 && stepClearance < 1.0);

  double minKnee = max(0.0, model->minMaxKneeBend[0]);
  
  double maxHipDrop = min(-model->minMaxHipLift[0], pi/2.0 - 
    atan2(model->legs[0][0].tibiaLength*sin(minKnee), 
          model->legs[0][0].femurLength + model->legs[0][0].tibiaLength*cos(minKnee)));
  
  maximumBodyHeight = model->legs[0][0].femurLength * sin(maxHipDrop) + model->legs[0][0].tibiaLength * 
    sin(maxHipDrop + clamped(pi/2.0 - maxHipDrop, minKnee, model->minMaxKneeBend[1]));
    
  ASSERT(stepClearance*maximumBodyHeight <= 2.0*model->legs[0][0].femurLength); // impossible to lift this high
 
  // If undefined - work out a best value to maximise circular footprint for given step clearance
  if (bodyClearance == -1) 
  {
    // in this case we assume legs have equal characteristics
    bodyClearance = model->legs[0][0].minLegLength/maximumBodyHeight + params.stepCurvatureAllowance*stepClearance;
  }
  ASSERT(bodyClearance >= 0 && bodyClearance < 1.0);

  minFootprintRadius = 1e10;

  for (int l = 0; l<3; l++)
  {
    // find biggest circle footprint inside the pie segment defined by the body clearance and the yaw limits
    Leg &leg = model->legs[l][0];
    // downward angle of leg
    double legDrop = asin((bodyClearance*maximumBodyHeight)/leg.maxLegLength);
    double horizontalRange = 0;
    double rad = 1e10;

    if (legDrop > -model->minMaxHipLift[0]) // leg can't be straight and touching the ground at bodyClearance
    {
      double extraHeight = bodyClearance*maximumBodyHeight - leg.femurLength * sin(-model->minMaxHipLift[0]);
      ASSERT(extraHeight <= leg.tibiaLength); // this shouldn't be possible with bodyClearance < 1
      rad = sqrt(sqr(leg.tibiaLength) - sqr(extraHeight));
      horizontalRange = leg.femurLength * cos(-model->minMaxHipLift[0]) + rad;
    }
    else
    {	
      horizontalRange = sqrt(sqr(leg.maxLegLength) - sqr(bodyClearance*maximumBodyHeight));
      //horizontalRange*=0.6;
    }
    horizontalRange *= params.legSpanScale;

    double theta = model->yawLimitAroundStance[l];
    double cotanTheta = tan(0.5*pi - theta);
    rad = min(rad, solveQuadratic(sqr(cotanTheta), 2.0*horizontalRange, -sqr(horizontalRange)));
    //rad = horizontalRange*sin(theta)/(1+sin(theta)); //ALTERNATIVE ALGORITHM FOR RADIUS OF CIRCLE INSCRIBED BY A SECTOR
    ASSERT(rad > 0.0); // cannot have negative radius

    // we should also take into account the stepClearance not getting too high for the leg to reach
    double legTipBodyClearance = max(0.0, bodyClearance-params.stepCurvatureAllowance*stepClearance)*maximumBodyHeight; 
    
    // if footprint radius due to lift is smaller due to yaw limits, reduce this minimum radius
    if (legTipBodyClearance < leg.minLegLength)
    {
      rad = min(rad, (horizontalRange - sqrt(sqr(leg.minLegLength) - sqr(legTipBodyClearance))) / 2.0); 
    }
    ASSERT(rad > 0.0); // cannot have negative radius, step height is too high to allow any footprint

    footSpreadDistances[l] = leg.hipLength + horizontalRange - rad;
    
    // this is because the step cycle exceeds the ground footprint in order to maintain velocity
    double footprintDownscale = 0.8; 
    
    minFootprintRadius = min(minFootprintRadius, rad*footprintDownscale);
    
    for (int s = 0; s<2; s++)
    {
      identityTipPositions[l][s] = model->legs[l][s].rootOffset + 
        footSpreadDistances[l]*Vector3d(cos(model->stanceLegYaws[l]), sin(model->stanceLegYaws[l]), 0) + 
          Vector3d(0,0,-bodyClearance*maximumBodyHeight);
          
      identityTipPositions[l][s][0] *= model->legs[l][s].mirrorDir;
      
      legSteppers[l][s].defaultTipPosition = identityTipPositions[l][s];
      legSteppers[l][s].currentTipPosition = identityTipPositions[l][s];
      legSteppers[l][s].phase = 0; // Ensures that feet start stepping naturally and don't pop to up position
      legSteppers[l][s].strideVector = Vector2d(0,0);
      legSteppers[l][s].walker = this;
      legSteppers[l][s].params = &params;
    }
  }
  // check for overlapping radii
  double minGap = 1e10;
  for (int s = 0; s<2; s++)
  {
    Vector3d posDif = identityTipPositions[1][s] - identityTipPositions[0][s];
    posDif[2] = 0.0;
    minGap = min(minGap, posDif.norm() - 2.0*minFootprintRadius);
    posDif = identityTipPositions[1][s] - identityTipPositions[2][s];
    posDif[2] = 0.0;
    minGap = min(minGap, posDif.norm() - 2.0*minFootprintRadius);
  }

  if (minGap < 0.0)
  {
    minFootprintRadius += minGap*0.5;
  }

  stanceRadius = abs(identityTipPositions[1][0][0]);

  localCentreVelocity = Vector2d(0,0);
  angularVelocity = 0;

  pose.rotation = Quat(1,0,0,0);
  pose.position = Vector3d(0, 0, bodyClearance*maximumBodyHeight);
}


void WalkController::setGaitParams(Parameters p)
{
  params = p;
  stanceEnd = params.stancePhase*0.5;      
  swingStart = stanceEnd;
  swingEnd = swingStart + params.swingPhase;      
  stanceStart = swingEnd;
  
  //Normalises the step phase length to match the total number of iterations over a full step
  int basePhaseLength = params.stancePhase + params.swingPhase;
  double swingRatio = (params.swingPhase)/basePhaseLength; //Used to modify stepFreqency based on gait
  phaseLength = (roundToInt((1.0/(2.0*params.stepFrequency*timeDelta))/(basePhaseLength*swingRatio))*(basePhaseLength*swingRatio))/swingRatio;
  stepFrequency = 1/(phaseLength*timeDelta); //adjust stepFrequency to match corrected phaseLength
  ASSERT(phaseLength%basePhaseLength == 0);
  int normaliser = phaseLength/basePhaseLength;
  stanceEnd *= normaliser;   
  swingStart *= normaliser;
  swingEnd *= normaliser;     
  stanceStart *= normaliser;
  
  for (int l = 0; l<3; l++)
  {
    for (int s = 0; s<2; s++)
    {       
      int index = 2*l+s;
      int multiplier = params.offsetMultiplier[index];
      legSteppers[l][s].phaseOffset = (int(params.phaseOffset*normaliser)*multiplier)%phaseLength;
    }
  }
}

/***********************************************************************************************************************
 * Calculates body and stride velocities and uses velocities in body and leg state machines 
 * to update tip positions and apply inverse kinematics
***********************************************************************************************************************/
void WalkController::updateWalk(Vector2d localNormalisedVelocity, double newCurvature, double deltaZ[3][2])
{
  double onGroundRatio = double(phaseLength-(swingEnd-swingStart))/double(phaseLength);
  
  Vector2d localVelocity;
  if (state != STOPPING)
  {
    localVelocity = localNormalisedVelocity*2.0*minFootprintRadius*stepFrequency/onGroundRatio;
  }
  else
  {
    localVelocity = Vector2d(0.0,0.0);
  }
    
  double normalSpeed = localVelocity.norm();
  ASSERT(normalSpeed < 1.01); // normalised speed should not exceed 1, it can't reach this
   
  // we make the speed argument refer to the outer leg, so turning on the spot still has a meaningful speed argument
  double newAngularVelocity = newCurvature * normalSpeed/stanceRadius;
  double dif = newAngularVelocity - angularVelocity;

  if (abs(dif)>0.0)
  {
    angularVelocity += dif * min(1.0, params.maxCurvatureSpeed*timeDelta/abs(dif));
  }

  Vector2d centralVelocity = localVelocity * (1 - abs(newCurvature));
  Vector2d centralAcceleration = centralVelocity - localCentreVelocity;
  
  //Calculate max acceleration if specified by parameter
  if (params.maxAcceleration == -1.0)
  {
    //Ensures tip of last leg to make first swing does not move further than footprint radius before starting first swing (s=0.5*a*(t^2))
    params.maxAcceleration = 2.0*minFootprintRadius/sqr(((phaseLength-(swingEnd-swingStart)*0.5)*timeDelta)); 
  }    
  
  if (centralAcceleration.norm() > 0.0)
  {
    localCentreVelocity += centralAcceleration*min(1.0, params.maxAcceleration*timeDelta/centralAcceleration.norm());
  } 
  
  //State transitions for robot state machine.
  // State transition: STOPPED->STARTING
  if (state == STOPPED && normalSpeed)
  {
    state = STARTING;
    for (int l = 0; l<3; l++)
    {
      for (int s = 0; s<2; s++)
      {
        legSteppers[l][s].phase = legSteppers[l][s].phaseOffset-1;
      }
    }
  }  
  // State transition: STARTING->MOVING
  else if (state == STARTING && legsInCorrectPhase == NUM_LEGS && legsCompletedFirstStep == NUM_LEGS)
  {
    legsInCorrectPhase = 0;
    legsCompletedFirstStep = 0;
    state = MOVING;
  }  
  // State transition: MOVING->STOPPING
  else if (state == MOVING && !normalSpeed)
  {
    state = STOPPING;
  }  
  // State transition: STOPPING->STOPPED
  else if (state == STOPPING && legsInCorrectPhase == NUM_LEGS)
  {
    legsInCorrectPhase = 0;
    state = STOPPED;
  }      
   
  //Robot State Machine
  for (int l = 0; l<3; l++)
  {
    for (int s = 0; s<2; s++)
    { 
      LegStepper &legStepper = legSteppers[l][s];
      Leg &leg = model->legs[l][s]; 
      
      legStepper.strideVector = onGroundRatio*
          (localCentreVelocity + angularVelocity*Vector2d(leg.localTipPosition[1], -leg.localTipPosition[0]))/
          stepFrequency;
      
      if (state == STARTING)
      {  
	//Iterate phase
        legStepper.phase = (legStepper.phase+1)%phaseLength;
	
	//Check if all legs have completed one step
        if (legsInCorrectPhase == NUM_LEGS)
	{
	  if (legStepper.phase == swingEnd && !legStepper.completedFirstStep)
          {
	    legStepper.completedFirstStep = true;
	    legsCompletedFirstStep++;
	  }
	}
	
        // Force any leg state into STANCE if it starts offset in a mid-swing state
        if (!legStepper.inCorrectPhase)
	{
	  if (legStepper.phaseOffset > swingStart && legStepper.phaseOffset < swingEnd) //SWING STATE
	  {
	    if (legStepper.phase == swingEnd)
	    {
	      legsInCorrectPhase++;  
	      legStepper.inCorrectPhase = true;
	    }
	    else
	    {
	      legStepper.state = FORCE_STANCE;  
	    }
	  }
	  else
	  {
	    legsInCorrectPhase++;  
	    legStepper.inCorrectPhase = true;
	  }      
	}
      }
      else if (state == STOPPING)
      {  
	if (!legStepper.inCorrectPhase)
        {
          legStepper.phase = (legStepper.phase+1)%phaseLength; //Iterate phase
          
          //Front_left leg only "meets target" after completing extra step AND returning to zero phase
          if (l==0 && s==0 && legStepper.state == FORCE_STOP && legStepper.phase == 0)
          {
            legStepper.inCorrectPhase = true;
            legsInCorrectPhase++;
            legStepper.state = STANCE;
          }
        }
	
        //All legs (except front_left) must make one extra step after receiving stopping signal
        if (legStepper.strideVector.norm() == 0 && legStepper.phase == swingEnd)
        {
          legStepper.state = FORCE_STOP;
          if (!(l==0 && s==0))
          {
            if (!legStepper.inCorrectPhase)
            {
              legStepper.inCorrectPhase = true;
              legsInCorrectPhase++;
            }
          }
        }             
      }
      else if (state == MOVING)
      {
        legStepper.phase = (legStepper.phase+1)%phaseLength; //Iterate phase
        legStepper.inCorrectPhase = false;
      }
      else if (state == STOPPED)
      {        
        legStepper.inCorrectPhase = false;
	legStepper.completedFirstStep = false;
        legStepper.phase = 0;
        legStepper.state = STANCE;
      } 
    }
  } 
  
  //Leg State Machine
  for (int l = 0; l<3; l++)
  {
    for (int s = 0; s<2; s++)
    { 
      LegStepper &legStepper = legSteppers[l][s];       
         
      //Force leg state as STANCE for STARTING robot state
      if (legStepper.state == FORCE_STANCE)
      {
        legStepper.state = STANCE;
      }
      //Force leg state as FORCE_STOP for STOPPING robot state
      else if (legStepper.state == FORCE_STOP)
      {
        legStepper.state = FORCE_STOP;
      }
      else if (legStepper.phase >= swingStart && legStepper.phase < swingEnd)
      {
        legStepper.state = SWING;
      }
      else if (legStepper.phase < stanceEnd || legStepper.phase >= stanceStart)
      {        
        legStepper.state = STANCE; 
      }       
    }
  }
  
  //Update tip positions and apply inverse kinematics
  for (int l = 0; l<3; l++)
  {
    for (int s = 0; s<2; s++)
    {  
      LegStepper &legStepper = legSteppers[l][s];
      Leg &leg = model->legs[l][s];
      
      if (leg.state == WALKING)
      {
        //Revise default and current tip positions from stanceTipPosition due to change in pose
        Vector3d tipOffset = legStepper.defaultTipPosition - legStepper.currentTipPosition;
        legStepper.defaultTipPosition = leg.stanceTipPosition;
        legStepper.currentTipPosition = legStepper.defaultTipPosition - tipOffset;
        
	if (state != STOPPED) 
	{
	  legStepper.updatePosition(); //updates current tip position through step cycle
	}

        Vector3d adjustedPos = legStepper.currentTipPosition;
        adjustedPos[2] -= deltaZ[l][s]; //Impedance controller
        leg.applyLocalIK(adjustedPos); 
      }
    }
  }  
  
  model->clampToLimits();  
  
  //RVIZ
  Vector2d push = localCentreVelocity*timeDelta;
  pose.position += pose.rotation.rotateVector(Vector3d(push[0], push[1], 0));
  pose.rotation *= Quat(Vector3d(0.0,0.0,-angularVelocity*timeDelta));
  //RVIZ
}

/***********************************************************************************************************************
***********************************************************************************************************************/
