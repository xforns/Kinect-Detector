
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// HEADERS
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

#define CHECK_RC(rc, what)											\
if (rc != XN_STATUS_OK)												\
{																	\
    printf("%s failed: %s\n", what, xnGetStatusString(rc));			\
																	\
}

// PLATFORM CONDITIONALS
//----------------------------
#define WINDOWS 1000
//#define OSX 2000

// UDP MESSAGES
//----------------------------
#define PUSH_GESTURE 27
#define START_HAND1 21
#define LOST_HAND1 22
#define START_HAND2 23
#define LOST_HAND2 24
#define HAND1_MOVE_UPDATE 28
#define USER1_HAND_POSITION 94
#define USER2_HAND_POSITION 95
#define USER1_FOUND 25
#define USER2_FOUND 80
#define USER1_LOST 26
#define USER2_LOST 81
#define FOO_USER 96

// GENERAL HEADERS
//----------------------------
#include <stdio.h>
#include <vector>
#include <deque>
#include <iostream>
#include <sstream>
#include <map>

// UDP HEADERS
//----------------------------
#ifdef WINDOWS
	#include <ctype.h>
	#include <string.h>
	#include <winsock2.h> //can also be winsock.h
	#include <windows.h>
	#pragma comment(lib, "ws2_32.lib")
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <fcntl.h>
#endif

// OPENNI HEADERS
//----------------------------
#include <XnOpenNI.h>

// NITE HEADERS
//----------------------------
#include <XnCppWrapper.h>
#include <XnVSessionManager.h>
#include "XnVMultiProcessFlowClient.h"
#include <XnVWaveDetector.h>
#include <XnVPushDetector.h>
#include <XnVFlowRouter.h>
#include <XnVPointControl.h>

// XML INITIALIZATION
//----------------------------
#define SAMPLE_XML_FILE_LOCAL "mide.xml"

// CALIBRATION DATA
//----------------------------
#define XN_CALIBRATION_FILE_NAME "UserCalibration.bin"

// UDP IP AND PORT
//----------------------------
#define UDPPORT 53001
#define IP_A 127
#define IP_B 0
#define IP_C 0
#define IP_D 1

using namespace std;

// OpenNI and NITE variables
//----------------------------
xn::DepthGenerator depthGenerator;
xn::UserGenerator userGenerator;
xn::HandsGenerator handsGenerator;
xn::GestureGenerator gestureGenerator;
XnVFlowRouter *flowRouter;
XnVPointControl *pHandler; 
XnVSessionManager *sessionManager;

XnUserID g_nPlayer = 0;
XnBool g_bCalibrated = FALSE;

int currentPrimaryPoint = -1;
int lastPrimaryPoint = -1;

XnPoint3D handPosition;
XnPoint3D lastPointPosition;

XnUserID skeleton1 = -1;
XnUserID skeleton2 = -1;

std::map<XnUInt32, std::pair<XnCalibrationStatus, XnPoseDetectionStatus> > m_Errors;

int frames_to_skip = 0;

bool g_bNeedPose;

int rsocket;




//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// SOCKET LOGIC
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


// Implementation for sending UDP data
//-----------------------------------------------------------------------------
bool Send(const void *data, int size) {
	assert( data );
	assert( size > 0 );
	
	if ( rsocket == 0 )
		return false;
	
	unsigned int destIP = ( IP_A << 24 ) | ( IP_B << 16 ) | ( IP_C << 8 ) | IP_D;
	
	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl( destIP );
	address.sin_port = htons( (unsigned short) UDPPORT );
	
	int sent_bytes = sendto( rsocket, (const char*)data, size, 0, (sockaddr*)&address, sizeof(sockaddr_in) );
	bool comp = sent_bytes == size;
	
	return comp;
}


// Wrapper for sending UDP data: it expects an ID (identifies a gesture or event) and an XnPoint3D
//-----------------------------------------------------------------------------
void sendWrapper(int idGesture, XnPoint3D pos) {
	char data[31];
	#ifdef WINDOWS
		//_snprintf(data,22,"%d:%f,%f",idGesture,pos.X,pos.Y);
		_snprintf(data,31,"%d:%f,%f,%f\0",idGesture,pos.X,pos.Y,pos.Z);
	#else
		snprintf(data,31,"%d:%0.3f,%0.3f,%0.3f",idGesture,pos.X,pos.Y,pos.Z);
	#endif
	Send(data,sizeof(data));
}


// Wrapper for sending positions of hands of a given user
//-----------------------------------------------------------------------------
void sendWrapperUser(int idGesture, XnPoint3D pos1,XnPoint3D pos2) {
	char data[31];
	#ifdef WINDOWS
		_snprintf(data,31,"%d:%d,%d,%d,%d\0",idGesture,(int)pos1.X,(int)pos1.Y,(int)pos2.X,(int)pos2.Y);
	#else
		snprintf(data,31,"%d:%d,%d,%d,%d",idGesture,pos1.X,pos1.Y,pos2.X,pos2.Y);
	#endif
	Send(data,sizeof(data));
}




//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// HAND CALLBACKS
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


// Session in progress
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE SessionProgress(const XnChar* strFocus, const XnPoint3D& ptFocusPoint, XnFloat fProgress, void* UserCxt) {
	printf("Session progress %6.2f [%s]\n",fProgress,strFocus);
}

// Session start
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE SessionStart(const XnPoint3D& ptFocusPoint, void* UserCxt) {
	printf("Session started. Please wave\n");
}

// Session end
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE SessionEnd(void* UserCxt) {
	printf("Session ended. Please perform focus gesture to start session\n");
}

// Wave detection
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE OnWaveCB(void* cxt) {
	printf("Wave: %f , %f\n",handPosition.X,handPosition.Y);
}

// Push detection
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE OnPush(XnFloat fVelocity, XnFloat fAngle, void* cxt) {
	printf("Push: %f , %f\n",lastPointPosition.X,lastPointPosition.Y);
	
	sendWrapper(PUSH_GESTURE,lastPointPosition);
}

// Primary point creation
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE OnPrimaryPointCreate(const XnVHandPointContext* pContext,const XnPoint3D& ptFocus, void* cxt) {
	printf("Primary hand created: %d\n",pContext->nID);
	
	// Grab the id
	currentPrimaryPoint = pContext->nID;
	
	sendWrapper(START_HAND1,handPosition);
}

// Primary point destruction
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE OnPrimaryPointDestroy(const XnUInt32 nId, void* cxt) {
	printf("Primary hand destroyed: %d\n",nId);
	
	lastPrimaryPoint = currentPrimaryPoint;
	currentPrimaryPoint = -1;
	
	sendWrapper(LOST_HAND1,handPosition);
}

// Point creation
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE OnPointCreate(const XnVHandPointContext* pContext, void* cxt) {
	
	if(pHandler->GetPrimaryID()==pContext->nID) {
		printf("Primary hand created: %d\n",pContext->nID);
		sendWrapper(START_HAND1,handPosition);
	}
	else {
		printf("Hand created: %d\n",pContext->nID);
		sendWrapper(START_HAND2,handPosition);
	}
}

// Point destruction
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE OnPointDestroy(const XnUInt32 nId, void* cxt) {
	
	if( nId==pHandler->GetPrimaryID() || nId== currentPrimaryPoint ) {
		printf("Primary hand destroyed: %d\n",nId);
		sendWrapper(LOST_HAND1,handPosition);
	}
	else {
		printf("Hand destroyed: %d\n",nId);
		sendWrapper(LOST_HAND2,handPosition);
	}
}

// Point update of any hand
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE OnPointUpdate(const XnVHandPointContext* pContext, void* cxt) {
	handPosition.X = (float)floor(pContext->ptPosition.X+0.5);
	handPosition.Y = (float)floor(pContext->ptPosition.Y+0.5);
	handPosition.Z = (float)floor(pContext->ptPosition.Z+0.5);
	depthGenerator.ConvertRealWorldToProjective(1, &handPosition, &handPosition);
	
	// Grab only the primary hand position
	if(pHandler->GetPrimaryID()==pContext->nID) {
		lastPointPosition.X = handPosition.X;
		lastPointPosition.Y = handPosition.Y;
		lastPointPosition.Z = handPosition.Z;

		sendWrapper(HAND1_MOVE_UPDATE,handPosition);
	}
}




//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// PERSON CALLBACKS
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


// Get projective position given actual joints
//-----------------------------------------------------------------------------
void getPosition(XnUserID user,XnSkeletonJoint eJoint1, XnSkeletonJoint eJoint2,XnPoint3D pt[2]) {
	XnSkeletonJointPosition joint1, joint2;
	userGenerator.GetSkeletonCap().GetSkeletonJointPosition(user, eJoint1, joint1);
	userGenerator.GetSkeletonCap().GetSkeletonJointPosition(user, eJoint2, joint2);
	
	
	if (joint1.fConfidence < 0.5 || joint2.fConfidence < 0.5) {
		return;
	}
	pt[0] = joint1.position;
	pt[1] = joint2.position;
	depthGenerator.ConvertRealWorldToProjective(2,pt,pt);
}


// Get hands positions of the skeleton of any user, and send the data
//-----------------------------------------------------------------------------
void getUserData() {
	XnUserID aUsers[2];
	XnUInt16 nUsers = 2;
	userGenerator.GetUsers(aUsers, nUsers);

	for(int i = 0; i < 2; i++){
		XnPoint3D com;
		userGenerator.GetCoM(aUsers[i], com);
		depthGenerator.ConvertRealWorldToProjective(1, &com, &com);

		if(userGenerator.GetSkeletonCap().IsTracking(aUsers[i]) && (com.X >0 && com.X < 640) && (com.Y >0 && com.Y < 480)) {
			if(frames_to_skip == 0){
				if(aUsers[i] == skeleton1){
					XnSkeletonJointPosition left_hand_joint;
					userGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[i], XN_SKEL_LEFT_HAND, left_hand_joint);
					XnPoint3D left_hand_spt;
					left_hand_spt.X = left_hand_joint.position.X;
					left_hand_spt.Y = left_hand_joint.position.Y;
					left_hand_spt.Z = left_hand_joint.position.Z;
					depthGenerator.ConvertRealWorldToProjective(1, &left_hand_spt, &left_hand_spt);

					XnSkeletonJointPosition right_hand_joint;
					userGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[i], XN_SKEL_RIGHT_HAND, right_hand_joint);
					XnPoint3D right_hand_spt;
					right_hand_spt.X = right_hand_joint.position.X;
					right_hand_spt.Y = right_hand_joint.position.Y;
					right_hand_spt.Z = right_hand_joint.position.Z;
					depthGenerator.ConvertRealWorldToProjective(1, &right_hand_spt, &right_hand_spt);

					// Send first user hands positions
					sendWrapperUser(USER1_HAND_POSITION,left_hand_spt,right_hand_spt);
				}
				if(aUsers[i] == skeleton2){
					XnSkeletonJointPosition left_hand_joint;
					userGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[i], XN_SKEL_LEFT_HAND, left_hand_joint);
					XnPoint3D left_hand_spt;
					left_hand_spt.X = left_hand_joint.position.X;
					left_hand_spt.Y = left_hand_joint.position.Y;
					left_hand_spt.Z = left_hand_joint.position.Z;
					depthGenerator.ConvertRealWorldToProjective(1, &left_hand_spt, &left_hand_spt);

					XnSkeletonJointPosition right_hand_joint;
					userGenerator.GetSkeletonCap().GetSkeletonJointPosition(aUsers[i], XN_SKEL_RIGHT_HAND, right_hand_joint);
					XnPoint3D right_hand_spt;
					right_hand_spt.X = right_hand_joint.position.X;
					right_hand_spt.Y = right_hand_joint.position.Y;
					right_hand_spt.Z = right_hand_joint.position.Z;
					depthGenerator.ConvertRealWorldToProjective(1, &right_hand_spt, &right_hand_spt);

					// Send second user hands positions
					sendWrapperUser(USER2_HAND_POSITION,left_hand_spt,right_hand_spt);
				}

				if(skeleton1 == -1 && aUsers[i] != skeleton2){
					printf("Calibration ended: id: %i Sent as FIRST USER 25\n", aUsers[i]);
					skeleton1 = aUsers[i];
					sendWrapper(USER1_FOUND,com);
				}
				if(skeleton2 == -1 && aUsers[i] != skeleton1){
					printf("Calibration ended: id: %i Sent as SECOND USER 80\n", aUsers[i]);
					skeleton2 = aUsers[i];
					sendWrapper(USER2_FOUND,com);
				}
				//frames_to_skip++;
			}else{
				frames_to_skip++;
				if(frames_to_skip >= 2)
					frames_to_skip = 0;
			}
		}else{
			XnPoint3D spt;
			userGenerator.GetCoM(aUsers[i], spt);
			depthGenerator.ConvertRealWorldToProjective(1, &spt, &spt);
			if(aUsers[i] == skeleton1 && skeleton1 != -1){
				skeleton1 = -1;
				sendWrapper(USER1_LOST,spt);
				printf("LOST user: id: %i Sent as FIRST USER 26\n", aUsers[i]);
			}
			if(aUsers[i] == skeleton2 && skeleton2 != -1){
				skeleton2 = -1;
				sendWrapper(USER2_LOST,spt);
				printf("LOST user: id: %i Sent as SECOND USER 81\n", aUsers[i]);
			}
		}
	}
}


// Calibration in progress
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE MyCalibrationInProgress(xn::SkeletonCapability&, XnUserID id, XnCalibrationStatus calibrationError, void*) {
	m_Errors[id].first = calibrationError;
}


// Pose in progress
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE MyPoseInProgress(xn::PoseDetectionCapability&, const XnChar*, XnUserID id, XnPoseDetectionStatus poseError, void*) {
	m_Errors[id].second = poseError;
}


// New user callback
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE User_NewUser(xn::UserGenerator&, XnUserID nId, void*) {
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
	printf("New User %d\n", epochTime, nId);
	// New user found
	if (g_bNeedPose) {
		userGenerator.GetPoseDetectionCap().StartPoseDetection("", nId);
	}
	else {
		userGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
	}

	XnPoint3D spt;

	spt.X = 0.f;
	spt.Y = 0.f;
	spt.Z = 0.f;
    sendWrapper(FOO_USER,spt);
}


// User lost callback
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE User_LostUser(xn::UserGenerator&, XnUserID nId, void* /*pCookie*/) {
XnPoint3D pt[2];
	getPosition(nId,XN_SKEL_NECK, XN_SKEL_LEFT_SHOULDER,pt);
	XnPoint3D spt;
	spt.X = pt[0].X;
	spt.Y = pt[0].Y;
	spt.Z = pt[0].Z;
	
	if(skeleton1 == nId && skeleton1 != -1){
		skeleton1 = -1;
		sendWrapper(USER1_LOST,spt);
		printf("LOST user: id: %i Sent as FIRST USER 26\n", (unsigned int)nId);
	}
	if(skeleton2 == nId && skeleton2 != -1){
		skeleton2 = -1;
		sendWrapper(USER2_LOST,spt);
		printf("LOST user: id: %i Sent as SECOND USER 81\n", (unsigned int)nId);
	}
}


// Pose detected callback
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE UserPose_PoseDetected(xn::PoseDetectionCapability& /*capability*/, const XnChar* strPose, XnUserID nId, void* /*pCookie*/) {
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
	printf("Pose %s detected for user %d\n",strPose, nId);
	userGenerator.GetPoseDetectionCap().StopPoseDetection(nId);
	userGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
}


// Calibration start callback
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE UserCalibration_CalibrationStart(xn::SkeletonCapability& /*capability*/, XnUserID nId, void* /*pCookie*/)
{
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
	printf("Calibration started for user %d\n",nId);
}


// Finished calibration callback
//-----------------------------------------------------------------------------
void XN_CALLBACK_TYPE UserCalibration_CalibrationComplete(xn::SkeletonCapability&, XnUserID nId, XnCalibrationStatus eStatus, void*) {
	XnUInt32 epochTime = 0;
	xnOSGetEpochTime(&epochTime);
	if (eStatus == XN_CALIBRATION_STATUS_OK) {
		// Calibration succeeded
		printf("Calibration complete, start tracking user %d\n",nId);		
		userGenerator.GetSkeletonCap().StartTracking(nId);

		XnPoint3D pt[2];
		getPosition(nId,XN_SKEL_NECK, XN_SKEL_LEFT_SHOULDER,pt);
		XnPoint3D spt;
		spt.X = pt[0].X;
		spt.Y = pt[0].Y;
		spt.Z = pt[0].Z;

		if(skeleton1!=-1 && nId == skeleton1) {
			// Send first user found
			sendWrapper(USER1_FOUND,spt);
		}
		else if(skeleton2!=-1 && nId == skeleton2) {
			// Send second user found
			sendWrapper(USER2_FOUND,spt);
		}
	}
	else {
		// Calibration failed
		printf("Calibration failed for user %d\n",nId);
        if(eStatus==XN_CALIBRATION_STATUS_MANUAL_ABORT) {
            printf("Manual abort occured, stop attempting to calibrate!");
            return;
        }
		if (g_bNeedPose) {
			userGenerator.GetPoseDetectionCap().StartPoseDetection("", nId);
		}
		else {
			userGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
		}
	}
}


// Save calibration to file
//-----------------------------------------------------------------------------
void SaveCalibration() {
	XnUserID aUserIDs[20] = {0};
	XnUInt16 nUsers = 20;
	userGenerator.GetUsers(aUserIDs, nUsers);
	for (int i = 0; i < nUsers; ++i) {
		// Find a user who is already calibrated
		if (userGenerator.GetSkeletonCap().IsCalibrated(aUserIDs[i])) {
			// Save user's calibration to file
			userGenerator.GetSkeletonCap().SaveCalibrationDataToFile(aUserIDs[i], XN_CALIBRATION_FILE_NAME);
			break;
		}
	}
}


// Load calibration from file
//-----------------------------------------------------------------------------
void LoadCalibration() {
	XnUserID aUserIDs[20] = {0};
	XnUInt16 nUsers = 20;
	userGenerator.GetUsers(aUserIDs, nUsers);
	for (int i = 0; i < nUsers; ++i) {
		// Find a user who isn't calibrated or currently in pose
		if (userGenerator.GetSkeletonCap().IsCalibrated(aUserIDs[i])) continue;
		if (userGenerator.GetSkeletonCap().IsCalibrating(aUserIDs[i])) continue;

		// Load user's calibration from file
		XnStatus rc = userGenerator.GetSkeletonCap().LoadCalibrationDataFromFile(aUserIDs[i], XN_CALIBRATION_FILE_NAME);
		if (rc == XN_STATUS_OK) {
			// Make sure state is coherent
			userGenerator.GetPoseDetectionCap().StopPoseDetection(aUserIDs[i]);
			userGenerator.GetSkeletonCap().StartTracking(aUserIDs[i]);
		}
		break;
	}
}




//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// MAIN
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


XnBool fileExists(const char *fn) {
	XnBool exists;
	xnOSDoesFileExist(fn, &exists);
	return exists;
}


int main(int argc, char** argv) {
	
	
	// SOCKET UDP CONFIGURATION
	//-----------------------------------------------------------------------------
	//-----------------------------------------------------------------------------
	
	// CREATE SOCKET
	//-----------------------------------------------------------------------------
	#ifdef WINDOWS
		WSAData data;
		WSAStartup(MAKEWORD(2,2),&data);
	#endif
	rsocket = socket(AF_INET,SOCK_DGRAM,0);	
	if(rsocket<=0) {
		printf("Failed to create socket \n");
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}
	else {
		int unused = true;
		setsockopt(rsocket,SOL_SOCKET,SO_REUSEADDR,(char*)&unused,sizeof(unused));
	}
	
	// SET NONBLOCKING IO
	//-----------------------------------------------------------------------------
	#ifdef WINDOWS
		u_long nonblocking_enabled = TRUE;
		if(ioctlsocket( rsocket, FIONBIO, &nonblocking_enabled )!=0) {
			printf( "Failed to set non-blocking socket\n" );
			close(rsocket);
			WSACleanup();
			return false;
		}
	#else
		int nonBlocking = 1;
		if(fcntl(rsocket,F_SETFL,O_NONBLOCK,nonBlocking)==-1) {
			printf( "Failed to set non-blocking socket\n" );
			close(rsocket);
			return false;
		}
	#endif
	
	
	// KINECT INITIALIZATION, GENERATION AND DETECTION
	//-----------------------------------------------------------------------------
	//-----------------------------------------------------------------------------
	
	xn::Context context;
	xn::ScriptNode scriptNode;
	XnVSessionGenerator* pSessionGenerator;
	XnBool bRemoting = FALSE;
	
	
	// CONTEXT CREATION
	//-----------------------------------------------------------------------------
	const char *fn = NULL;
	if (fileExists(SAMPLE_XML_FILE_LOCAL))
		fn = SAMPLE_XML_FILE_LOCAL;
	else {
		printf("Could not find '%s'. Aborting.\n", SAMPLE_XML_FILE_LOCAL);
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return XN_STATUS_ERROR;
	}
	XnStatus rc = context.InitFromXmlFile(fn, scriptNode);
	if (rc != XN_STATUS_OK) {
		printf("Couldn't initialize: %s\n", xnGetStatusString(rc));
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}
	
	
	// DEPTH GENERATOR
	//-----------------------------------------------------------------------------
	rc = context.FindExistingNode(XN_NODE_TYPE_DEPTH, depthGenerator);
	if (rc != XN_STATUS_OK) {
		printf("Depth manager couldn't initialize: %s\n", xnGetStatusString(rc));
		delete depthGenerator;
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}

	
	// GESTURE GENERATOR
	//-----------------------------------------------------------------------------
	rc = context.FindExistingNode(XN_NODE_TYPE_GESTURE, gestureGenerator);
	if (rc != XN_STATUS_OK) {
		printf("Gesture manager couldn't initialize: %s\n", xnGetStatusString(rc));
		delete depthGenerator;
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}
	
	
	// USER GENERATOR
	//-----------------------------------------------------------------------------
	printf("starting user generator...\n");
    rc = context.FindExistingNode(XN_NODE_TYPE_USER, userGenerator);
    if (rc != XN_STATUS_OK) {
		printf("User generator couldn't initialize: %s\n", xnGetStatusString(rc));
		delete depthGenerator;
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}

	XnCallbackHandle hUserCallbacks, hCalibrationStart, hCalibrationComplete, hPoseDetected, hCalibrationInProgress, hPoseInProgress;

	if (!userGenerator.IsCapabilitySupported(XN_CAPABILITY_SKELETON)) {
		printf("Supplied user generator doesn't support skeleton\n");
		return 1;
	}

	rc = userGenerator.RegisterUserCallbacks(User_NewUser, User_LostUser, NULL, hUserCallbacks);
	if (rc != XN_STATUS_OK) {
		printf("Register to use callback error: %s\n", xnGetStatusString(rc));
		delete depthGenerator;
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}
	rc = userGenerator.GetSkeletonCap().RegisterToCalibrationStart(UserCalibration_CalibrationStart, NULL, hCalibrationStart);
	if (rc != XN_STATUS_OK) {
		printf("Register to calibration start\n", xnGetStatusString(rc));
		delete depthGenerator;
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}
	rc = userGenerator.GetSkeletonCap().RegisterToCalibrationComplete(UserCalibration_CalibrationComplete, NULL, hCalibrationComplete);
	if (rc != XN_STATUS_OK) {
		printf("Register to calibration start\n", xnGetStatusString(rc));
		delete depthGenerator;
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}

	if (userGenerator.GetSkeletonCap().NeedPoseForCalibration()) {
		g_bNeedPose = TRUE;
		if (!userGenerator.IsCapabilitySupported(XN_CAPABILITY_POSE_DETECTION)) {
			printf("Pose required, but not supported\n");
			return 1;
		}
		rc = userGenerator.GetPoseDetectionCap().RegisterToPoseDetected(UserPose_PoseDetected, NULL, hPoseDetected);
		if (rc != XN_STATUS_OK) {
			printf("Register to pose complete\n", xnGetStatusString(rc));
			delete depthGenerator;
			close(rsocket);
			#ifdef WINDOWS
				WSACleanup();
			#endif
			return 1;
		}
		userGenerator.GetSkeletonCap().GetCalibrationPose("");

		rc = userGenerator.GetPoseDetectionCap().RegisterToPoseInProgress(MyPoseInProgress, NULL, hPoseInProgress);
		if (rc != XN_STATUS_OK) {
			printf("Register to pose in progress\n", xnGetStatusString(rc));
			delete depthGenerator;
			close(rsocket);
			#ifdef WINDOWS
				WSACleanup();
			#endif
			return 1;
		}
	}

	userGenerator.GetSkeletonCap().SetSkeletonProfile(XN_SKEL_PROFILE_ALL);

	rc = userGenerator.GetSkeletonCap().RegisterToCalibrationInProgress(MyCalibrationInProgress, NULL, hCalibrationInProgress);
	if (rc != XN_STATUS_OK) {
		printf("Register to calibration in progress\n", xnGetStatusString(rc));
		delete depthGenerator;
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}

	rc = context.StartGeneratingAll();
	if (rc != XN_STATUS_OK) {
		printf("Start generating error\n", xnGetStatusString(rc));
		delete depthGenerator;
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}

	
	// SESSION MANAGER
	//-----------------------------------------------------------------------------
	pSessionGenerator = new XnVSessionManager();
	rc = ((XnVSessionManager*)pSessionGenerator)->Initialize(&context,"Click,Wave","RaiseHand");
	if (rc != XN_STATUS_OK) {
		printf("Session Manager couldn't initialize: %s\n", xnGetStatusString(rc));
		delete pSessionGenerator;
		close(rsocket);
		#ifdef WINDOWS
			WSACleanup();
		#endif
		return 1;
	}
	pSessionGenerator->RegisterSession(NULL, &SessionStart, &SessionEnd, &SessionProgress);
	
	
	// POINT CONTROL
	//-----------------------------------------------------------------------------
	pHandler = new XnVPointControl("PointControl"); 
	pHandler->RegisterPrimaryPointUpdate(&context,OnPointUpdate);
	pHandler->RegisterPointCreate(&context,OnPointCreate);
	pHandler->RegisterPointDestroy(&context,OnPointDestroy);
	flowRouter = new XnVFlowRouter;
	flowRouter->SetActive(pHandler);
	
	pSessionGenerator->AddListener(flowRouter);
	

	// PUSH CONTROL
	//-----------------------------------------------------------------------------
	XnVPushDetector pc;
	pc.RegisterPush(NULL,OnPush);
	pSessionGenerator->AddListener(&pc);
		
	
	// START GENERATING
	//-----------------------------------------------------------------------------
	rc = context.StartGeneratingAll();
	
	printf("Awaiting for:\n");
	printf("1. Hand focus gesture (Click or Wave) to start session\n");
	printf("2. Psi human position for person detection\n");
	printf("Hit any key to exit this Kinect gesture detector\n");
	
	
	// MAIN LOOP
	//-----------------------------------------------------------------------------
	while(!xnOSWasKeyboardHit()) {
		//context.WaitAnyUpdateAll();
		context.WaitOneUpdateAll(depthGenerator);
		((XnVSessionManager*)pSessionGenerator)->Update(&context);
		getUserData();
	}
	
	
	// DESTRUCTION OF RESOURCES	
	//-----------------------------------------------------------------------------
	depthGenerator.Release();
	delete pSessionGenerator;
	delete userGenerator;
	delete depthGenerator;
	
	close(rsocket);
	#ifdef WINDOWS
		WSACleanup();
	#endif
	
	return 0;
}
