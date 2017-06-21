#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/LaserScan.h>
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <cv_bridge/cv_bridge.h>
#include <iomanip>
#include <kobuki_msgs/Led.h>
#include <kobuki_msgs/SensorState.h>
#include <sensor_msgs/Imu.h>
#include <no_look_pass_robot/laserScan.h>
#include <boost/thread/mutex.hpp>


using namespace cv;

#define USE_X_EAST_Y_NORTH      (1)

bool drawLRF = true;
vector<Vec2d> laserScanData;
float rangeMax;
boost::mutex laserScanMutex;

// A template method to check 'nan'
template<typename T>
inline bool isnan(T value)
{
    return value != value;
}


// Node subscriber =================================================================
class perceptionSubscriber
{
private:
	ros::NodeHandle _nh;

	ros::Subscriber _subKinectRgb;
	ros::Subscriber _subKinectDetphRaw;
	ros::Subscriber _subKinectLRF;
	ros::Subscriber _subTurtlebotSensors;
	ros::Subscriber _subIMU;

	void makeDepthImage(Mat &inXyz, Mat &outImage, unsigned short minDepth, unsigned short maxDepth);
	void poseMessageReceivedRGB(const sensor_msgs::Image& msg);
	void poseMessageReceivedDepthRaw(const sensor_msgs::Image& msg);
	void poseMessageReceivedLRF(const sensor_msgs::LaserScan& msg);
	void drawImage(int rangeSize, vector<Vec2d> coord, float rangeMax);
	void turtlebotSensorCallback(const kobuki_msgs::SensorState::ConstPtr& msg);
	void imuCallback(const sensor_msgs::Imu::ConstPtr& imusmsg);
	
public:
	perceptionSubscriber()
	{
		// Decleation of subscriber
		_subKinectRgb = _nh.subscribe(
			"/camera/rgb/image_color", 10,
			&perceptionSubscriber::poseMessageReceivedRGB, this
		);
		_subKinectDetphRaw = _nh.subscribe(
			"/camera/depth_registered/image_raw", 10,
			&perceptionSubscriber::poseMessageReceivedDepthRaw, this
		);
		_subKinectLRF = _nh.subscribe(
			"/scan", 10,
			&perceptionSubscriber::poseMessageReceivedLRF, this
		);
		_subTurtlebotSensors = _nh.subscribe(
			"/mobile_base/sensors/core", 100,
			&perceptionSubscriber::turtlebotSensorCallback, this
		);
//		_subIMU = _nh.subscribe(
//			"/mobile_base/sensors/imu_data", 100,
//			&perceptionSubscriber::imuCallback, this
//		);
	}
	
	void subscribe()
	{
		ros::Rate rate (30);
		while(ros::ok())
		{
			ros::spinOnce();
			rate.sleep();
		}
	}
};


// Node publisher ==================================================================
class perceptionPublisher
{
private:
	ros::NodeHandle _nh;
	ros::Publisher _pubLaserScan;

public:
	perceptionPublisher()
	{
		_pubLaserScan = _nh.advertise<no_look_pass_robot::laserScan>("/laser_scan", 100, this);
	}

	void publish()
	{
		ros::Rate rate(10);

		while(ros::ok())
		{
			ros::spinOnce();
			laserScanMutex.lock(); {
				no_look_pass_robot::laserScan scanData;

				for(int i = 0; i < laserScanData.size(); i++)
				{
					no_look_pass_robot::vec2d vec;
					vec.x = laserScanData[i][0];
					vec.y = laserScanData[i][1];
					scanData.laser_data.push_back(vec);
				}
				scanData.rangeMax = rangeMax;

				_pubLaserScan.publish(scanData);
			} laserScanMutex.unlock();

			rate.sleep();
		}
	}

	ros::Publisher getPublisher()
	{
		return _pubLaserScan;
	}
};


// KINECT IMAGE view ==========================================================================================
// make a pseudo color depth
void
perceptionSubscriber::makeDepthImage(Mat &inXyz, Mat &outImage, unsigned short minDepth, unsigned short maxDepth)
{
	if(outImage.rows != inXyz.rows || outImage.cols != inXyz.cols || outImage.dims != CV_8UC3) {
		outImage = Mat::zeros(inXyz.rows, inXyz.cols, CV_8UC3);
	}

	unsigned short len = maxDepth-minDepth;
	int nTotal = inXyz.rows*inXyz.cols;
	unsigned short *pSrc = (unsigned short*) inXyz.data;
	unsigned char *pDst = (unsigned char*) outImage.data;

	for(int i=0; i<nTotal; i++, pSrc++, pDst+=3) {
	if(pSrc[2] == 0.) {
		pDst[0] = 0;
		pDst[1] = 0;
		pDst[2] = 0;
		//pDst[3] = 0;
	} else {
		int v = (int) (255*6*(1. - ((double) (maxDepth-pSrc[2]))/len));

		if (v < 0)
		v = 0;

		int lb = v & 0xff;

		switch (v / 256) {
			case 0:
				//pDst[3] = 0;
				pDst[2] = 255;
				pDst[1] = 255-lb;
				pDst[0] = 255-lb;
			break;
			case 1:
				//pDst[3] = 0;
				pDst[2] = 255;
				pDst[1] = lb;
				pDst[0] = 0;
			break;
			case 2:
				//pDst[3] = 0;
				pDst[2] = 255-lb;
				pDst[1] = 255;
				pDst[0] = 0;
			break;
			case 3:
				//pDst[3] = 0;
				pDst[2] = 0;
				pDst[1] = 255;
				pDst[0] = lb;
			break;
			case 4:
				//pDst[3] = 0;
				pDst[2] = 0;
				pDst[1] = 255-lb;
				pDst[0] = 255;
			break;
			case 5:
				//pDst[3] = 0;
				pDst[2] = 0;
				pDst[1] = 0;
				pDst[0] = 255-lb;
			break;
			default:
				//pDst[3] = 0;
				pDst[2] = 0;
				pDst[1] = 0;
				pDst[0] = 0;
			break;
			}

			if (v == 0) {
				//pDst[3] = 0;
				pDst[2] = 0;
				pDst[1] = 0;
				pDst[0] = 0;
			}
		}
	}
}

// handle RGB Data. A callback function. Executed eack time a new pose message arrives.
void perceptionSubscriber::poseMessageReceivedRGB(const sensor_msgs::Image& msg) {
	ROS_INFO("seq = %d / width = %d / height = %d / step = %d", msg.header.seq, msg.width, msg.height, msg.step);
	ROS_INFO("encoding = %s", msg.encoding.c_str());
	//vector<unsigned char> data = msg.data;

	Mat image = Mat(msg.height, msg.width, CV_8UC3);
	memcpy(image.data, &msg.data[0], sizeof(unsigned char) * msg.data.size());
	imshow("RGB Preview", image);
	waitKey(30);
}

// handle Depth Data.  A callback function. Executed eack time a new pose message arrives.
void perceptionSubscriber::poseMessageReceivedDepthRaw(const sensor_msgs::Image& msg) {
	ROS_INFO("seq = %d / width = %d / height = %d / step = %d", msg.header.seq, msg.width, msg.height, msg.step);
	ROS_INFO("encoding = %s", msg.encoding.c_str());
	//vector<unsigned char> data = msg.data;

	Mat rawDepth = Mat(msg.height, msg.width, CV_16UC1);
	memcpy(rawDepth.data, &msg.data[0], sizeof(unsigned char) * msg.data.size());

	Mat pseduoColorDepth;
	makeDepthImage(rawDepth, pseduoColorDepth, 0, 4096);

	imshow("Depth Preview", pseduoColorDepth);
	waitKey(30);
}
// KINECT image view END ======================================================================================




// KINECT LRF sensing =========================================================================================
void perceptionSubscriber::drawImage(int rangeSize, vector<Vec2d> coord, float rangeMax)
{
	// draw the 'coord' in image plane
	const int nImageSize = 801;
	const int nImageHalfSize = nImageSize/2;
	const int nAxisSize = nImageSize/16;

	const Vec2i imageCenterCooord = Vec2i(nImageHalfSize, nImageHalfSize);
	Mat image = Mat::zeros(nImageHalfSize, nImageSize, CV_8UC3);
	line(
		image,
		Point(imageCenterCooord[0], 0),
		Point(imageCenterCooord[0]+nAxisSize, 0),
		Scalar(0, 0, 255),
		2
	);
	line(
		image,
		Point(imageCenterCooord[0], 0),
		Point(imageCenterCooord[0], 0 + nAxisSize),
		Scalar(0, 255, 0),
		2
	);

	for(int i=0; i < rangeSize; i++) {
		int x_image = imageCenterCooord[0] + cvRound((coord[i][0] / rangeMax) * nImageSize);
		int y_image = 0 + cvRound((coord[i][1] / rangeMax) * nImageSize);

		if(x_image >= 0 && x_image < nImageSize && y_image >= 0 && y_image < nImageHalfSize) {
			image.at<Vec3b>(y_image, x_image) = Vec3b(255, 255, 0);
			//circle(image, Point(x_image, y_image), -1, Scalar(255, 255, 0), 2, CV_AA);
		}
	}

	// image coordinate transformation
	flip(image, image, 0);

	imshow("Kinect LRF Preview", image);
	waitKey(30);
}

// LRF 데이터 (레이져 센싱) callback
void perceptionSubscriber::poseMessageReceivedLRF(const sensor_msgs::LaserScan& msg)
{
	// convert polar to Cartesian coordinate
	vector<Vec2d> coord;
	int nRangeSize = (int)msg.ranges.size();

	for(int i=0; i<nRangeSize; i++) {
		double dRange = msg.ranges[i];

		if(isnan(dRange)) {
			if(drawLRF)
				coord.push_back(Vec2d(0., 0.));
		} 
		else{
			if(USE_X_EAST_Y_NORTH) {
				// for Y-heading in world coordinate system (YX)
				double dAngle = msg.angle_max - i*msg.angle_increment;
				coord.push_back(Vec2d(dRange*sin(dAngle), dRange*cos(dAngle)));
			} else {
				// for X-heading in world coordinate system (XY)
				double dAngle = msg.angle_min + i*msg.angle_increment;
				coord.push_back(Vec2d(dRange*cos(dAngle), dRange*sin(dAngle)));
			}
		}
	}
	laserScanMutex.lock(); {
		laserScanData = coord;
		rangeMax = msg.range_max;
	} laserScanMutex.unlock();

	if(drawLRF) drawImage(nRangeSize, coord, msg.range_max);
}

// KINECT LRF sensing END =====================================================================================



// TURTLEBOT Sensor ===========================================================================================
void perceptionSubscriber::turtlebotSensorCallback(const kobuki_msgs::SensorState::ConstPtr& msg)
{
	int timeStamp = msg->time_stamp;
	int bumper = msg->bumper;
	int wheelDrop = msg->wheel_drop;
	int cliff = msg->cliff;
	int encoderLeft = msg->left_encoder;
	int encoderRight = msg->right_encoder;
	int pwmLeft = msg->left_pwm;
	int pwmRight = msg->right_pwm;
	int buttons = msg->buttons;
	int charger = msg->charger;
	int battery = msg->battery;
//	int bottom = msg->bottom;
//	int current = msg->current;
	int overCurrent = msg->over_current;
	int digitalInput = msg->digital_input;
//	int analogInput = msg->analog_input;
}

void perceptionSubscriber::imuCallback(const sensor_msgs::Imu::ConstPtr& imusmsg)
{
	printf("Imu data\n");
	printf("\tOrientation Data\n");  
	printf("\t\tCovariance x: %e %e %e\n",
		imusmsg->orientation_covariance[0],
		imusmsg->orientation_covariance[1],
		imusmsg->orientation_covariance[2]
	);
	printf("\t\tCovariance y: %e %e %e\n",
		imusmsg->orientation_covariance[3],
		imusmsg->orientation_covariance[4],
		imusmsg->orientation_covariance[5]
	);
	printf("\t\tCovariance z: %e %e %e\n",
		imusmsg->orientation_covariance[6],
		imusmsg->orientation_covariance[7],
		imusmsg->orientation_covariance[8]
	);
	printf("\tAngular velocity Data\n");  
	printf("\t\tCovariance x: %e %e %e\n",
		imusmsg->angular_velocity_covariance[0],
		imusmsg->angular_velocity_covariance[1],
		imusmsg->angular_velocity_covariance[2]
	);
	printf("\t\tCovariance y: %e %e %e\n",
		imusmsg->angular_velocity_covariance[3],
		imusmsg->angular_velocity_covariance[4],
		imusmsg->angular_velocity_covariance[5]
	);	
	printf("\t\tCovariance z: %e %e %e\n",
		imusmsg->angular_velocity_covariance[6],
		imusmsg->angular_velocity_covariance[7],
		imusmsg->angular_velocity_covariance[8]
	);
	printf("\tLinear Acceleration Data\n");  
	printf("\t\tCovariance x: %e %e %e\n",
		imusmsg->linear_acceleration_covariance[0],
		imusmsg->linear_acceleration_covariance[1],
		imusmsg->linear_acceleration_covariance[2]
	);
	printf("\t\tCovariance y: %e %e %e\n",
		imusmsg->linear_acceleration_covariance[3],
		imusmsg->linear_acceleration_covariance[4],
		imusmsg->linear_acceleration_covariance[5]
	);
	printf("\t\tCovariance z: %e %e %e\n",
		imusmsg->linear_acceleration_covariance[6],
		imusmsg->linear_acceleration_covariance[7],
		imusmsg->linear_acceleration_covariance[8]
	);
}
// TURTLEBOT Sensor END =======================================================================================



int main(int argc, char **argv)
{
	// Initialize the ROS system
	ros::init(argc, argv, "perception_node");
	
	perceptionSubscriber* subscriber = new perceptionSubscriber();

	perceptionPublisher* publisher = new perceptionPublisher();
	publisher->publish();

	// Let ROS take over
	ros::spin();

	return 0;
}
