#include <ros/ros.h>
#include <spencer_tracking_msgs/DetectedPersons.h>

#include <tf/transform_listener.h>
#include <tf/transform_datatypes.h>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <list>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

using namespace spencer_tracking_msgs;

// A DetectedPerson together with some occlusion-related information.
struct Occluder  {
    DetectedPerson detectedPerson;
    double distanceToSensor;
    Eigen::Vector3d position;
    Eigen::ParametrizedLine<double, 3> ray; // from origin of target frame to position
};

ros::Publisher g_resultingDetectedPersonsPublisher;
boost::shared_ptr<tf::TransformListener> g_transformListener;
std::string g_sensorTargetFrame;
double g_personRadius, g_sensorMaxRange;

bool g_hasWaitedForTransform = false;


void newDetectedPersonsReceived(const DetectedPersons::ConstPtr& detectedPersons)
{
    // Prepare output message
    DetectedPersons::Ptr resultingDetectedPersons(new DetectedPersons);
    resultingDetectedPersons->header = detectedPersons->header;
    if(!g_sensorTargetFrame.empty()) resultingDetectedPersons->header.frame_id = g_sensorTargetFrame;

    // This is where we store the detected persons that have been transformed into target frame,
    // along with and sorted by their distance to target frame origin
    std::list<Occluder> occluders;

    // Lookup transform into target frame
    tf::StampedTransform tfTransform;
    if(!g_sensorTargetFrame.empty()) {
        try {
            g_transformListener->waitForTransform(g_sensorTargetFrame, detectedPersons->header.frame_id, detectedPersons->header.stamp, ros::Duration(g_hasWaitedForTransform ? 0.05 : 3.0));
            g_hasWaitedForTransform = true;
            g_transformListener->lookupTransform(g_sensorTargetFrame, detectedPersons->header.frame_id, detectedPersons->header.stamp, tfTransform);
        }
        catch(tf::TransformException e) {
            ROS_WARN_STREAM_THROTTLE(5.0, "TF lookup failed. Reason: " << e.what());
            return;
        }
    }
    else {
        tfTransform.setData(tf::Transform::getIdentity());
    }

    // First iteration over all detected persons
    foreach(const DetectedPerson& detectedPerson, detectedPersons->detections)
    {
        //
        // Transform person pose into a coordinate frame relative to the robot (if specified)
        //

        // FIXME: Also need to rotate covariance if target frame has different coordinate axes
        tf::Pose sourcePose; tf::poseMsgToTF(detectedPerson.pose.pose, sourcePose);
        tf::Pose targetPose = tfTransform * sourcePose;

        // FIXME: Hack, make configurable!? Enforce z coordinate to be 0
        
        
        //
        // Save as potential occluder
        //
        Occluder occluder;
        occluder.detectedPerson = detectedPerson;

        tf::poseTFToMsg(targetPose, occluder.detectedPerson.pose.pose);   
        occluder.position = Eigen::Vector3d(targetPose.getOrigin().x(), targetPose.getOrigin().y(), 0.0 /* FIXME: Hack! */);

        occluder.distanceToSensor = occluder.position.norm();
        occluder.ray = Eigen::ParametrizedLine<double, 3>(Eigen::Vector3d::Zero(), occluder.position.normalized());

        // If too far away from sensor, discard detection
        if(occluder.distanceToSensor <= g_sensorMaxRange)
        {
            // Assure list of occluders is sorted by ascending distance to the frame origin
            std::list<Occluder>::iterator insertBefore;
            for(insertBefore = occluders.begin(); insertBefore != occluders.end(); insertBefore++) {
                if(occluder.distanceToSensor < insertBefore->distanceToSensor) break;
            }
            occluders.insert(insertBefore, occluder);
        }
    }

    // Second iteration, now check for occlusions
    foreach(Occluder& potentiallyOccludedPerson, occluders)
    {
        // Iterate over all persons which are closer to the sensor than this one (i.e. all preceding elements in the list of occluders)
        // Then check for occlusion via ray-point distance check
        bool foundOccluder = false;
        foreach(Occluder& potentialOccluder, occluders)
        {
            // Check if we can stop here since all following elements are further away
            if(&potentiallyOccludedPerson == &potentialOccluder) break; 

            // Check if our potentially occluded person is closeby the ray of the potentially occluding person
            double distanceToRay = potentialOccluder.ray.distance(potentiallyOccludedPerson.position);

            if(distanceToRay <= g_personRadius) {
                // Person is more than 50% occluded
                foundOccluder = true;
                break;
            }  
        }    

        // Person is not occluded, so push it into the result list!
        if(!foundOccluder) resultingDetectedPersons->detections.push_back(potentiallyOccludedPerson.detectedPerson);
    }

    // Publish resulting detections
    g_resultingDetectedPersonsPublisher.publish(resultingDetectedPersons);
}


int main(int argc, char **argv)
{
    ros::init(argc, argv, "simulate_occluded_detections_via_raytracing");
    ros::NodeHandle nodeHandle("");
    ros::NodeHandle privateHandle("~");

    privateHandle.param<double>("person_radius", g_personRadius, 0.25);
    privateHandle.param<double>("sensor_max_range", g_sensorMaxRange, 10.0);
    privateHandle.param<std::string>("sensor_target_frame", g_sensorTargetFrame, "base_footprint");

    std::string inputTopic = "input_detections";
    std::string outputTopic = "output_detections";

    g_transformListener.reset(new tf::TransformListener);

    ros::Subscriber detectedPersonsSubscriber = nodeHandle.subscribe<DetectedPersons>(inputTopic, 3, &newDetectedPersonsReceived);
    g_resultingDetectedPersonsPublisher = nodeHandle.advertise<DetectedPersons>(outputTopic, 3);

    ROS_INFO_STREAM("Simulating occlusions for detected persons on topic " << ros::names::resolve(inputTopic) << " and publishing to output topic " << ros::names::resolve(outputTopic)
        << ". Assuming person radius of " << g_personRadius << " m and will transform detections into frame '" << g_sensorTargetFrame << "' beforehand.");

    ros::spin();
}
