/*
 * CarState.cpp
 *
 *  Created on: Jun 20, 2016
 *      Author: hatem
 */

#include "LocalPlannerH.h"
#include "UtilityH.h"
#include "PlanningHelpers.h"
#include "MappingHelpers.h"
#include "MatrixOperations.h"
#include "PlannerH.h"

using namespace UtilityHNS;

namespace PlannerHNS
{

LocalPlannerH::LocalPlannerH()
{
	pLane = 0;
	m_CurrentVelocity =  m_CurrentVelocityD =0;
	m_CurrentSteering = m_CurrentSteeringD =0;
	m_CurrentShift 		=  m_CurrentShiftD = SHIFT_POS_NN;
	m_CurrentAccSteerAngle = m_CurrentAccVelocity = 0;
	m_pCurrentBehaviorState = 0;
	m_pGoToGoalState = 0;
	m_pStopState= 0;
	m_pWaitState= 0;
	m_pMissionCompleteState= 0;
	m_pAvoidObstacleState = 0;
	m_pTrafficLightStopState = 0;
	m_pTrafficLightWaitState = 0;
	m_pStopSignStopState = 0;
	m_pStopSignWaitState = 0;
	m_pFollowState = 0;
	m_SimulationSteeringDelayFactor = 0.1;
	UtilityH::GetTickCount(m_SteerDelayTimer);
	m_PredictionTime = 0;
	m_iCurrentTotalPathId = 0;
	m_iSafeTrajectory = 0;

	InitBehaviorStates();
}

LocalPlannerH::~LocalPlannerH()
{

}

void LocalPlannerH::Init(const ControllerParams& ctrlParams, const PlannerHNS::PlanningParams& params,const CAR_BASIC_INFO& carInfo)
 	{
 		m_CarInfo = carInfo;
 		m_ControlParams = ctrlParams;
 		m_CurrentVelocity =  m_CurrentVelocityD =0;
 		m_CurrentSteering = m_CurrentSteeringD =0;
 		m_CurrentShift 		=  m_CurrentShiftD = SHIFT_POS_NN;
 		m_CurrentAccSteerAngle = m_CurrentAccVelocity = 0;

 		if(m_pCurrentBehaviorState)
 			m_pCurrentBehaviorState->SetBehaviorsParams(params);
 	}

void LocalPlannerH::InitBehaviorStates()
{

	m_pStopState 				= new StopState(0);
	m_pMissionCompleteState 	= new MissionAccomplishedState(0);
	m_pGoToGoalState 			= new ForwardState(m_pMissionCompleteState);
	m_pWaitState 				= new WaitState(m_pGoToGoalState);
	m_pInitState 				= new InitState(m_pGoToGoalState);
	m_pFollowState				= new FollowState(m_pGoToGoalState);
	m_pAvoidObstacleState		= new SwerveState(m_pGoToGoalState);
	m_pTrafficLightStopState	= new TrafficLightStopState(m_pGoToGoalState);
	m_pTrafficLightWaitState	= new TrafficLightWaitState(m_pGoToGoalState);
	m_pStopSignWaitState		= new StopSignWaitState(m_pGoToGoalState);
	m_pStopSignStopState		= new StopSignStopState(m_pStopSignWaitState);

	m_pGoToGoalState->InsertNextState(m_pStopState);
	m_pGoToGoalState->InsertNextState(m_pWaitState);
	m_pGoToGoalState->InsertNextState(m_pFollowState);
	m_pGoToGoalState->InsertNextState(m_pAvoidObstacleState);
	m_pGoToGoalState->InsertNextState(m_pTrafficLightStopState);
	m_pGoToGoalState->InsertNextState(m_pStopSignStopState);

	m_pAvoidObstacleState->InsertNextState(m_pStopState);
	m_pAvoidObstacleState->InsertNextState(m_pWaitState);
	m_pAvoidObstacleState->InsertNextState(m_pFollowState);
	m_pAvoidObstacleState->decisionMakingTime = 0.0;
	m_pAvoidObstacleState->InsertNextState(m_pTrafficLightStopState);

	m_pFollowState->InsertNextState(m_pStopState);
	m_pFollowState->InsertNextState(m_pWaitState);
	m_pFollowState->InsertNextState(m_pAvoidObstacleState);
	m_pFollowState->InsertNextState(m_pTrafficLightStopState);
	m_pFollowState->InsertNextState(m_pStopSignStopState);

	m_pStopState->InsertNextState(m_pGoToGoalState);

	m_pTrafficLightStopState->InsertNextState(m_pTrafficLightWaitState);

	m_pTrafficLightWaitState->InsertNextState(m_pTrafficLightStopState);

	m_pStopSignWaitState->decisionMakingTime = 5.0;
	m_pStopSignWaitState->InsertNextState(m_pStopSignStopState);

	m_pStopSignStopState->InsertNextState(m_pFollowState);

	m_pCurrentBehaviorState = m_pInitState;

}

void LocalPlannerH::InitPolygons()
{
	double l2 = m_CarInfo.length/2.0;
	double w2 = m_CarInfo.width/2.0;

	m_CarShapePolygon.push_back(GPSPoint(-w2, -l2, 0,0));
	m_CarShapePolygon.push_back(GPSPoint(w2, -l2, 0,0));
	m_CarShapePolygon.push_back(GPSPoint(w2, l2, 0,0));
	m_CarShapePolygon.push_back(GPSPoint(-w2, l2, 0,0));
}

 void LocalPlannerH::FirstLocalizeMe(const WayPoint& initCarPos)
 {
	pLane = initCarPos.pLane;
	state = initCarPos;
	m_OdometryState.pos.a = initCarPos.pos.a;
	m_OdometryState.pos.x = initCarPos.pos.x + (m_CarInfo.wheel_base/2.0 * cos(initCarPos.pos.a));
	m_OdometryState.pos.y = initCarPos.pos.y + (m_CarInfo.wheel_base/2.0 * sin(initCarPos.pos.a));
 }

 void LocalPlannerH::LocalizeMe(const double& dt)
{
	//calculate the new x, y ,
	 WayPoint currPose = state;

	if(m_CurrentShift == SHIFT_POS_DD)
	{
		m_OdometryState.pos.x	 +=  m_CurrentVelocity * dt * cos(currPose.pos.a);
		m_OdometryState.pos.y	 +=  m_CurrentVelocity * dt * sin(currPose.pos.a);
		m_OdometryState.pos.a	 +=  m_CurrentVelocity * dt * tan(m_CurrentSteering)  / m_CarInfo.wheel_base;

	}
	else if(m_CurrentShift == SHIFT_POS_RR )
	{
		m_OdometryState.pos.x	 +=  -m_CurrentVelocity * dt * cos(currPose.pos.a);
		m_OdometryState.pos.y	 +=  -m_CurrentVelocity * dt * sin(currPose.pos.a);
		m_OdometryState.pos.a	 +=  -m_CurrentVelocity * dt * tan(m_CurrentSteering);
	}

	m_OdometryState.pos.a = atan2(sin(m_OdometryState.pos.a), cos(m_OdometryState.pos.a));
	m_OdometryState.pos.a = UtilityH::FixNegativeAngle(m_OdometryState.pos.a);

	state.pos.a = m_OdometryState.pos.a;
	state.pos.x = m_OdometryState.pos.x	 - (m_CurrentVelocity*dt* (m_CarInfo.wheel_base) * cos (m_OdometryState.pos.a));
	state.pos.y = m_OdometryState.pos.y	 - (m_CurrentVelocity*dt* (m_CarInfo.wheel_base/2.0) * sin (m_OdometryState.pos.a));
}

 void LocalPlannerH::UpdateState(const PlannerHNS::VehicleState& state, const bool& bUseDelay)
  {
	 if(!bUseDelay || m_SimulationSteeringDelayFactor == 0)
	 {
		 m_CurrentSteering 	= m_CurrentSteeringD;
	 }
	 else
	 {
		 double currSteerDeg = RAD2DEG * m_CurrentSteering;
		 double desiredSteerDeg = RAD2DEG * m_CurrentSteeringD;

		 double mFact = 1.0 - UtilityH::GetMomentumScaleFactor(state.speed);
		 double diff = desiredSteerDeg - currSteerDeg;
		 double diffSign = UtilityH::GetSign(diff);
		 double inc = 1.0*diffSign;
		 if(abs(diff) < 1.0 )
			 inc = diff;

		 if(UtilityH::GetTimeDiffNow(m_SteerDelayTimer) > m_SimulationSteeringDelayFactor*mFact)
		 {
			 UtilityH::GetTickCount(m_SteerDelayTimer);
			 currSteerDeg += inc;
		 }

		 m_CurrentSteering = DEG2RAD * currSteerDeg;
	 }

	 m_CurrentShift 	= m_CurrentShiftD;
	 m_CurrentVelocity = m_CurrentVelocityD;
  }

 void LocalPlannerH::AddAndTransformContourPoints(const PlannerHNS::DetectedObject& obj, std::vector<PlannerHNS::WayPoint>& contourPoints)
 {
	 contourPoints.clear();
	 WayPoint  p, p_center = obj.center;
	 p_center.pos.a += M_PI_2;
	 for(unsigned int i=0; i< obj.contour.size(); i++)
	 {
		 p.pos = obj.contour.at(i);
		 //TransformPoint(p_center, p.pos);
		 contourPoints.push_back(p);
	 }

	 contourPoints.push_back(obj.center);
 }

 void LocalPlannerH::TransformPoint(const PlannerHNS::WayPoint& refPose, PlannerHNS::GPSPoint& p)
 {
 	PlannerHNS::Mat3 rotationMat(refPose.pos.a);
 	PlannerHNS::Mat3 translationMat(refPose.pos.x, refPose.pos.y);
	p = rotationMat*p;
	p = translationMat*p;
 }

 bool LocalPlannerH::GetNextTrafficLight(const int& prevTrafficLightId, const std::vector<PlannerHNS::TrafficLight>& trafficLights, PlannerHNS::TrafficLight& trafficL)
 {
	 for(unsigned int i = 0; i < trafficLights.size(); i++)
	 {
		 double d = hypot(trafficLights.at(i).pos.y - state.pos.y, trafficLights.at(i).pos.x - state.pos.x);
		 if(d <= trafficLights.at(i).stoppingDistance)
		 {
			 //double a = UtilityH::FixNegativeAngle(atan2(trafficLights.at(i).pos.y - state.pos.y, trafficLights.at(i).pos.x - state.pos.x));
			 double a_diff = UtilityH::AngleBetweenTwoAnglesPositive(UtilityH::FixNegativeAngle(trafficLights.at(i).pos.a) , UtilityH::FixNegativeAngle(state.pos.a));

			 if(a_diff < M_PI_2 && trafficLights.at(i).id != prevTrafficLightId)
			 {
				 std::cout << "Detected Light, ID = " << trafficLights.at(i).id << ", Distance = " << d << ", Angle = " << trafficLights.at(i).pos.a*RAD2DEG << ", Car Heading = " << state.pos.a*RAD2DEG << ", Diff = " << a_diff*RAD2DEG << std::endl;
				 trafficL = trafficLights.at(i);
				 return true;
			 }
		 }
	 }

	 return false;
 }

 void LocalPlannerH::CalculateImportantParameterForDecisionMaking(const PlannerHNS::VehicleState& car_state,
		 const PlannerHNS::GPSPoint& goal, const bool& bEmergencyStop, const bool& bGreenTrafficLight,
		 const TrajectoryCost& bestTrajectory)
 {
 	PreCalculatedConditions* pValues = m_pCurrentBehaviorState->GetCalcParams();

 	//Mission Complete
 	pValues->bGoalReached = IsGoalAchieved(goal);

 	pValues->minStoppingDistance = (-car_state.speed*car_state.speed)/2.0*m_CarInfo.max_deceleration + m_CarInfo.length/2.0;
 	if(pValues->distanceToNext > 0 || pValues->distanceToStop()>0)
 		pValues->minStoppingDistance += 0.5;

 	pValues->iCentralTrajectory		= m_pCurrentBehaviorState->m_PlanningParams.rollOutNumber/2;

 	if(pValues->iCurrSafeTrajectory < 0)
 			pValues->iCurrSafeTrajectory = pValues->iCentralTrajectory;

	if(pValues->iPrevSafeTrajectory < 0)
		pValues->iPrevSafeTrajectory = pValues->iCentralTrajectory;

 	pValues->stoppingDistances.clear();
 	pValues->currentVelocity 		= car_state.speed;
 	pValues->bTrafficIsRed 			= false;
 	pValues->currentTrafficLightID 	= -1;
 	pValues->currentStopSignID		= -1;
 	pValues->bRePlan 				= false;
 	pValues->bFullyBlock 			= false;


 	pValues->distanceToNext = bestTrajectory.closest_obj_distance;
 	pValues->velocityOfNext = bestTrajectory.closest_obj_velocity;
 	if(bestTrajectory.index>=0)
 		pValues->iCurrSafeTrajectory = bestTrajectory.index;

 	if(bestTrajectory.index == -1 && pValues->distanceToNext < m_pCurrentBehaviorState->m_PlanningParams.minFollowingDistance)
 		pValues->bFullyBlock = true;

 	int stopLineID = -1;
 	int stopSignID = -1;
 	int trafficLightID = -1;
 	double distanceToClosestStopLine = 0;

 	if(m_TotalPath.size()>0)
 		distanceToClosestStopLine = PlanningHelpers::GetDistanceToClosestStopLineAndCheck(m_TotalPath.at(m_iCurrentTotalPathId), state, stopLineID, stopSignID, trafficLightID) - m_CarInfo.length/2.0;

 	if(distanceToClosestStopLine > 0 && distanceToClosestStopLine < pValues->minStoppingDistance  && m_pCurrentBehaviorState->m_PlanningParams.enableTrafficLightBehavior)
 	{
		pValues->currentTrafficLightID = trafficLightID;
		pValues->currentStopSignID = stopSignID;
		pValues->stoppingDistances.push_back(distanceToClosestStopLine);
 	}

 	//cout << "Distance To Closest: " << distanceToClosestStopLine << ", Stop LineID: " << stopLineID << ", TFID: " << trafficLightID << endl;

 	pValues->bTrafficIsRed = !bGreenTrafficLight;

 	if(bEmergencyStop)
	{
		pValues->bFullyBlock = true;
		pValues->distanceToNext = 1;
		pValues->velocityOfNext = 0;
	}
 	//cout << "Distances: " << pValues->stoppingDistances.size() << ", Distance To Stop : " << pValues->distanceToStop << endl;
 }

double LocalPlannerH::PredictTimeCostForTrajectory(std::vector<PlannerHNS::WayPoint>& path, const PlannerHNS::VehicleState& vstatus, const PlannerHNS::WayPoint& currState)
{
	PlanningParams* pParams = &m_pCurrentBehaviorState->m_PlanningParams;

		//1- Calculate time prediction for each trajectory
	if(path.size() == 0) return 0;

//	SimulatedTrajectoryFollower predControl;
//	ControllerParams params;
//	params.Steering_Gain = PID_CONST(1.5, 0.0, 0.0);
//	params.Velocity_Gain = PID_CONST(0.2, 0.01, 0.1);
//	params.minPursuiteDistance = 3.0;
//
//	predControl.Init(params, m_CarInfo);
//	//double totalDistance = 0;
//	VehicleState CurrentState = vstatus;
//	VehicleState CurrentSteeringD;
//	bool bNewPath = true;
//	WayPoint localState = currState;
//	WayPoint prevState = currState;
//	int iPrevIndex = 0;
	double accum_time = 0;
//	double pred_max_time = 10.0;
//	double endDistance = pParams->microPlanDistance/2.0;
//
//	for(unsigned int i = 0 ; i < path.size(); i++)
//	{
//		path.at(i).collisionCost = 0;
//		path.at(i).timeCost = -1;
//	}
//
//	int startIndex = PlanningHelpers::GetClosestPointIndex(path, state);
//	double total_distance = 0;
//	path.at(startIndex).timeCost = 0;
//	for(unsigned int i=startIndex+1; i<path.size(); i++)
//	{
//		total_distance += hypot(path.at(i).pos.x- path.at(i-1).pos.x,path.at(i).pos.y- path.at(i-1).pos.y);
//		if(m_CurrentVelocity > 0.1 && total_distance > 0.1)
//			accum_time = total_distance/m_CurrentVelocity;
//		path.at(i).timeCost = accum_time;
//		if(total_distance > endDistance)
//			break;
//	}

//	while(totalDistance < pParams->microPlanDistance/2.0 && accum_time < pred_max_time)
//	{
//		double dt = 0.05;
//		PlannerHNS::BehaviorState currMessage;
//		currMessage.state = FORWARD_STATE;
//		currMessage.maxVelocity = PlannerHNS::PlanningHelpers::GetVelocityAhead(m_Path, state, 1.5*CurrentState.speed*3.6);
//
//		ControllerParams c_params = m_ControlParams;
//		c_params.SteeringDelay = m_ControlParams.SteeringDelay / (1.0-UtilityH::GetMomentumScaleFactor(CurrentState.speed));
//		predControl.Init(c_params, m_CarInfo);
//		CurrentSteeringD = predControl.DoOneStep(dt, currMessage, path, localState, CurrentState, bNewPath);
//
//		if(bNewPath) // first call
//		{
//			if(predControl.m_iCalculatedIndex > 0)
//			{
//				for(unsigned int j=0; j < predControl.m_iCalculatedIndex; j++)
//					path.at(j).timeCost = -1;
//			}
//		}
//		else
//		{
//			if(predControl.m_iCalculatedIndex != iPrevIndex)
//				path.at(iPrevIndex).timeCost = accum_time;
//		}
//
//		accum_time+=dt;
//		bNewPath = false;
//
//		//Update State
//		CurrentState = CurrentSteeringD;
//
//		//Localize Me
//		localState.pos.x	 +=  CurrentState.speed * dt * cos(localState.pos.a);
//		localState.pos.y	 +=  CurrentState.speed * dt * sin(localState.pos.a);
//		localState.pos.a	 +=  CurrentState.speed * dt * tan(CurrentState.steer)  / m_CarInfo.wheel_base;
//
//		totalDistance += distance2points(prevState.pos, localState.pos);
//
//		prevState = localState;
//		iPrevIndex = predControl.m_iCalculatedIndex;
//	}

	return accum_time;
}

void LocalPlannerH::PredictObstacleTrajectory(PlannerHNS::RoadNetwork& map, const PlannerHNS::WayPoint& pos, const double& predTime, std::vector<std::vector<PlannerHNS::WayPoint> >& paths)
{
	PlannerHNS::PlanningParams planningDefaultParams;
	planningDefaultParams.rollOutNumber = 0;
	planningDefaultParams.microPlanDistance = predTime*pos.v;

	planningDefaultParams.pathDensity = 0.5;
	//PlannerHNS::Lane* pMapLane  = MappingHelpers::GetClosestLaneFromMapDirectionBased(pos, map, 3.0);
	std::vector<PlannerHNS::Lane*> pMapLanes = MappingHelpers::GetClosestMultipleLanesFromMap(pos, map, 1.5);

	PlannerHNS::PlannerH planner;
	std::vector<int> LanesIds;
	std::vector< std::vector<PlannerHNS::WayPoint> >  rollOuts;
	std::vector<std::vector<PlannerHNS::WayPoint> > generatedPath;

	if(planningDefaultParams.microPlanDistance > 0)
	{
		for(unsigned int i = 0; i < pMapLanes.size(); i++)
		{
			std::vector<std::vector<PlannerHNS::WayPoint> > loca_generatedPath;
			planner.PredictPlanUsingDP(pMapLanes.at(i), pos, planningDefaultParams.microPlanDistance, loca_generatedPath);
			if(loca_generatedPath.size() > 0)
				generatedPath.insert(generatedPath.begin(),loca_generatedPath.begin(), loca_generatedPath.end());
		}
	}

//	planner.GenerateRunoffTrajectory(generatedPath, pos,
//			planningDefaultParams.enableLaneChange,
//			pos.v,
//			planningDefaultParams.microPlanDistance,
//			m_CarInfo.max_speed_forward,
//			planningDefaultParams.minSpeed,
//			planningDefaultParams.carTipMargin,
//			planningDefaultParams.rollInMargin,
//			planningDefaultParams.rollInSpeedFactor,
//			planningDefaultParams.pathDensity,
//			planningDefaultParams.rollOutDensity,
//			planningDefaultParams.rollOutNumber,
//			planningDefaultParams.smoothingDataWeight,
//			planningDefaultParams.smoothingSmoothWeight,
//			planningDefaultParams.smoothingToleranceError,
//			planningDefaultParams.speedProfileFactor,
//			planningDefaultParams.enableHeadingSmoothing,
//			rollOuts);

	if(generatedPath.size() > 0)
	{
		//path = rollOuts.at(0);
		paths = generatedPath;

//		PlanningHelpers::GenerateRecommendedSpeed(path,
//				m_CarInfo.max_speed_forward,
//				planningDefaultParams.speedProfileFactor);
//		PlanningHelpers::SmoothSpeedProfiles(path, 0.15,0.35, 0.1);
	}

	if(pMapLanes.size() ==0 || paths.size() == 0)
	{
		paths.clear();
		generatedPath.clear();
	}
	else
	{
		//std::cout << "------------------------------------------------" <<  std::endl;
		//std::cout << "Predicted Trajectories for Distance : " <<  planningDefaultParams.microPlanDistance << std::endl;
		for(unsigned int j=0; j < paths.size(); j++)
		{
			if(paths.at(j).size()==0)
				continue;

			double timeDelay = 0;
			double total_distance = 0;
			paths.at(j).at(0).timeCost = 0;
			paths.at(j).at(0).v = pos.v;
			for(unsigned int i=1; i<paths.at(j).size(); i++)
			{
				paths.at(j).at(i).v = pos.v;
				paths.at(j).at(i).pos.a = atan2(paths.at(j).at(i).pos.y - paths.at(j).at(i-1).pos.y, paths.at(j).at(i).pos.x - paths.at(j).at(i-1).pos.x);
				total_distance += distance2points(paths.at(j).at(i).pos, paths.at(j).at(i-1).pos);
				if(pos.v > 0.1 && total_distance > 0.1)
					timeDelay = total_distance/pos.v;
				paths.at(j).at(i).timeCost = timeDelay;
			}

			//std::cout << "ID : " <<  j << ", timeDelay : " << timeDelay << ", Distance : " << total_distance << std::endl;
		}

		//std::cout << "------------------------------------------------" <<  std::endl;
	}
}

bool LocalPlannerH::CalculateIntersectionVelocities(std::vector<PlannerHNS::WayPoint>& ego_path, std::vector<std::vector<PlannerHNS::WayPoint> >& predctedPath, const PlannerHNS::DetectedObject& obj)
{
	bool bCollisionDetected = false;
	for(unsigned int k = 0; k < predctedPath.size(); k++)
	{
		for(unsigned int j = 0; j < predctedPath.at(k).size(); j++)
		{
			bool bCollisionFound =false;
			for(unsigned int i = 0; i < ego_path.size(); i++)
			{
				if(ego_path.at(i).timeCost > 0.0)
				{
					double collision_distance = hypot(ego_path.at(i).pos.x-predctedPath.at(k).at(j).pos.x, ego_path.at(i).pos.y-predctedPath.at(k).at(j).pos.y);
					double contact_distance = hypot(state.pos.x - ego_path.at(i).pos.x,state.pos.y - ego_path.at(i).pos.y);
					if(collision_distance <= m_CarInfo.width  && abs(ego_path.at(i).timeCost - predctedPath.at(k).at(j).timeCost)<4.0)
					{
						ego_path.at(i).collisionCost = 1;
						double a = UtilityH::AngleBetweenTwoAnglesPositive(ego_path.at(i).pos.a, predctedPath.at(k).at(j).pos.a);
						if(a < M_PI_4/2.0)
							ego_path.at(i).v = obj.center.v;
						else
							ego_path.at(i).v = 0;
						predctedPath.at(k).at(j).collisionCost = 1;
						bCollisionFound = true;
						bCollisionDetected = true;
						break;
					}
				}
			}

			if(bCollisionFound)
				break;
		}
	}

	return bCollisionDetected;
}

bool LocalPlannerH::CalculateObstacleCosts(PlannerHNS::RoadNetwork& map, const PlannerHNS::VehicleState& vstatus, const std::vector<PlannerHNS::DetectedObject>& obj_list)
{
	double predTime = PredictTimeCostForTrajectory(m_Path, vstatus, state);
	m_PredictedPath.clear();
	bool bObstacleDetected = false;
	for(unsigned int i = 0; i < obj_list.size(); i++)
	{
		//std::vector<WayPoint> predPath;
		PredictObstacleTrajectory(map, obj_list.at(i).center, 10.0, m_PredictedPath);
		bool bObstacle = CalculateIntersectionVelocities(m_Path, m_PredictedPath, obj_list.at(i));
		if(bObstacle)
			bObstacleDetected = true;
	}

	return bObstacleDetected;
}

 void LocalPlannerH::UpdateCurrentLane(PlannerHNS::RoadNetwork& map, const double& search_distance)
 {
	 PlannerHNS::Lane* pMapLane = 0;
	PlannerHNS::Lane* pPathLane = 0;
	pPathLane = MappingHelpers::GetLaneFromPath(state, m_Path);
	if(!pPathLane)
		pMapLane  = MappingHelpers::GetClosestLaneFromMap(state, map, search_distance);

	if(pPathLane)
		pLane = pPathLane;
	else if(pMapLane)
		pLane = pMapLane;
	else
		pLane = 0;
 }

 void LocalPlannerH::SimulateOdoPosition(const double& dt, const PlannerHNS::VehicleState& vehicleState)
 {
	SetSimulatedTargetOdometryReadings(vehicleState.speed, vehicleState.steer, vehicleState.shift);
	UpdateState(vehicleState, true);
	LocalizeMe(dt);
 }

 bool LocalPlannerH::IsGoalAchieved(const PlannerHNS::GPSPoint& goal)
 {
	double distance_to_goal = distance2points(state.pos , goal);
	if(distance_to_goal < 3.5)
		return true;
	else
		return false;
 }

 bool LocalPlannerH::SelectSafeTrajectoryAndSpeedProfile(const PlannerHNS::VehicleState& vehicleState)
 {
	 PlannerHNS::PreCalculatedConditions *preCalcPrams = m_pCurrentBehaviorState->GetCalcParams();

	bool bNewTrajectory = false;

	if(m_TotalPath.size()>0)
	{
		std::vector<std::vector<WayPoint> > localRollouts;
		if(m_RollOuts.size()>0)
			localRollouts = m_RollOuts.at(0);

		int currIndex = PlannerHNS::PlanningHelpers::GetClosestPointIndex(m_Path, state);
		int index_limit = 0;//m_Path.size() - 20;
		if(index_limit<=0)
			index_limit =  m_Path.size()/2.0;
		if(localRollouts.size() == 0
				|| currIndex > index_limit
				|| m_pCurrentBehaviorState->GetCalcParams()->bRePlan
				|| m_pCurrentBehaviorState->m_Behavior == OBSTACLE_AVOIDANCE_STATE)
		{
			PlannerHNS::PlannerH planner;
			planner.GenerateRunoffTrajectory(m_TotalPath.at(m_iCurrentTotalPathId), state,
					m_pCurrentBehaviorState->m_PlanningParams.enableLaneChange,
					vehicleState.speed,
					m_pCurrentBehaviorState->m_PlanningParams.microPlanDistance,
					m_pCurrentBehaviorState->m_PlanningParams.maxSpeed,
					m_pCurrentBehaviorState->m_PlanningParams.minSpeed,
					m_pCurrentBehaviorState->m_PlanningParams.carTipMargin,
					m_pCurrentBehaviorState->m_PlanningParams.rollInMargin,
					m_pCurrentBehaviorState->m_PlanningParams.rollInSpeedFactor,
					m_pCurrentBehaviorState->m_PlanningParams.pathDensity,
					m_pCurrentBehaviorState->m_PlanningParams.rollOutDensity,
					m_pCurrentBehaviorState->m_PlanningParams.rollOutNumber,
					m_pCurrentBehaviorState->m_PlanningParams.smoothingDataWeight,
					m_pCurrentBehaviorState->m_PlanningParams.smoothingSmoothWeight,
					m_pCurrentBehaviorState->m_PlanningParams.smoothingToleranceError,
					m_pCurrentBehaviorState->m_PlanningParams.speedProfileFactor,
					m_pCurrentBehaviorState->m_PlanningParams.enableHeadingSmoothing,
					localRollouts, m_PathSection, m_SampledPoints);

			m_pCurrentBehaviorState->GetCalcParams()->bRePlan = false;
			m_iSafeTrajectory = preCalcPrams->iCurrSafeTrajectory;

			if(preCalcPrams->iCurrSafeTrajectory >= 0
					&& preCalcPrams->iCurrSafeTrajectory < localRollouts.size()
					&& m_pCurrentBehaviorState->m_Behavior == OBSTACLE_AVOIDANCE_STATE)
			{
				preCalcPrams->iPrevSafeTrajectory = preCalcPrams->iCurrSafeTrajectory;
				m_Path = localRollouts.at(preCalcPrams->iCurrSafeTrajectory);
				bNewTrajectory = true;
			}
			else
			{
				preCalcPrams->iPrevSafeTrajectory = preCalcPrams->iCentralTrajectory;
				m_Path = localRollouts.at(preCalcPrams->iCentralTrajectory);
				bNewTrajectory = true;
			}

			PlanningHelpers::GenerateRecommendedSpeed(m_Path,
					m_pCurrentBehaviorState->m_PlanningParams.maxSpeed,
					m_pCurrentBehaviorState->m_PlanningParams.speedProfileFactor);
			PlanningHelpers::SmoothSpeedProfiles(m_Path, 0.15,0.35, 0.1);

			if(m_RollOuts.size() > 0)
				m_RollOuts.at(0) = localRollouts;
			else
				m_RollOuts.push_back(localRollouts);
		}
	}

	return bNewTrajectory;
 }

 PlannerHNS::BehaviorState LocalPlannerH::GenerateBehaviorState(const PlannerHNS::VehicleState& vehicleState)
 {
	PlannerHNS::PreCalculatedConditions *preCalcPrams = m_pCurrentBehaviorState->GetCalcParams();

	m_pCurrentBehaviorState = m_pCurrentBehaviorState->GetNextState();
	PlannerHNS::BehaviorState currentBehavior;

	currentBehavior.state = m_pCurrentBehaviorState->m_Behavior;
	if(currentBehavior.state == PlannerHNS::FOLLOW_STATE)
		currentBehavior.followDistance = preCalcPrams->distanceToNext;
	else
		currentBehavior.followDistance = 0;

	if(preCalcPrams->bUpcomingRight)
		currentBehavior.indicator = PlannerHNS::INDICATOR_RIGHT;
	else if(preCalcPrams->bUpcomingLeft)
		currentBehavior.indicator = PlannerHNS::INDICATOR_LEFT;
	else
		currentBehavior.indicator = PlannerHNS::INDICATOR_NONE;
	currentBehavior.maxVelocity 	= PlannerHNS::PlanningHelpers::GetVelocityAhead(m_Path, state, vehicleState.speed*3.6);
	currentBehavior.minVelocity		= 0;
	currentBehavior.stopDistance 	= preCalcPrams->distanceToStop();
	currentBehavior.followVelocity 	= preCalcPrams->velocityOfNext;

	return currentBehavior;
 }

 PlannerHNS::BehaviorState LocalPlannerH::DoOneStep(
		 const double& dt,
		const PlannerHNS::VehicleState& vehicleState,
		const std::vector<PlannerHNS::DetectedObject>& obj_list,
		const PlannerHNS::GPSPoint& goal, PlannerHNS::RoadNetwork& map	,
		const bool& bEmergencyStop,
		const bool& bGreenTrafficLight,
		const bool& bLive)
{

	 if(!bLive)
		 SimulateOdoPosition(dt, vehicleState);

	UpdateCurrentLane(map, 3.0);

	timespec costTimer;
	UtilityH::GetTickCount(costTimer);
	TrajectoryCost tc = m_TrajectoryCostsCalculatotor.DoOneStep(m_RollOuts, m_TotalPath, state,
			m_pCurrentBehaviorState->GetCalcParams()->iCurrSafeTrajectory, m_pCurrentBehaviorState->m_PlanningParams,
			m_CarInfo, obj_list);
	m_CostCalculationTime = UtilityH::GetTimeDiffNow(costTimer);


	timespec behTimer;
	UtilityH::GetTickCount(behTimer);
	CalculateImportantParameterForDecisionMaking(vehicleState, goal, bEmergencyStop, bGreenTrafficLight, tc);

	PlannerHNS::BehaviorState beh = GenerateBehaviorState(vehicleState);
	m_BehaviorGenTime = UtilityH::GetTimeDiffNow(behTimer);

	timespec trajGenTimer;
	UtilityH::GetTickCount(trajGenTimer);
	beh.bNewPlan = SelectSafeTrajectoryAndSpeedProfile(vehicleState);
	m_RollOutsGenerationTime = UtilityH::GetTimeDiffNow(trajGenTimer);

/**
 * Usage of predictive planning
 */
//	timespec predictionTime;
//	UtilityH::GetTickCount(predictionTime);
//	if(UtilityH::GetTimeDiffNow(m_PredictionTimer) > 0.5 || beh.bNewPlan)
//	{
//		CalculateObstacleCosts(map, vehicleState, obj_list);
//		m_PredictionTime = UtilityH::GetTimeDiffNow(predictionTime);
//	}
//	bool bCollision = false;
//	int wp_id = -1;
//	for(unsigned int i=0; i < m_Path.size(); i++)
//	{
//		if(m_Path.at(i).collisionCost > 0)
//		{
//			bCollision = true;
//			wp_id = i;
//			beh.maxVelocity = m_Path.at(i).v;//PlannerHNS::PlanningHelpers::GetVelocityAhead(m_Path, state, 1.5*vehicleState.speed*3.6);
//			break;
//		}
//	}
//	std::cout << "------------------------------------------------" <<  std::endl;
//	std::cout << "Max Velocity = " << beh.maxVelocity << ", New Plan : " << beh.bNewPlan <<  std::endl;
//	std::cout << "Collision = " << bCollision << ", @ WayPoint : " << wp_id <<  std::endl;
//	std::cout << "------------------------------------------------" <<  std::endl;

	return beh;
 }

} /* namespace PlannerHNS */
