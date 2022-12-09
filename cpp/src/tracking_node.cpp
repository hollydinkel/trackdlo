#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>

#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>

#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_types.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/filters/conditional_removal.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/filters/voxel_grid.h>
#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>

#include <ctime>
#include <chrono>
#include <thread>

#include "../include/trackdlo.h"

using cv::Mat;

ros::Publisher pc_pub;
ros::Publisher results_pub;
ros::Publisher guide_nodes_pub;
ros::Publisher priors_pub;

using Eigen::MatrixXd;
using Eigen::MatrixXf;
using Eigen::RowVectorXf;
using Eigen::RowVectorXd;

MatrixXf Y;
double sigma2;
bool initialized = false;
std::vector<double> converted_node_coord = {0.0};
Mat occlusion_mask;
bool updated_opencv_mask = false;

bool use_eval_rope = false;
int num_of_nodes = 20;

void update_opencv_mask (const sensor_msgs::ImageConstPtr& opencv_mask_msg) {
    occlusion_mask = cv_bridge::toCvShare(opencv_mask_msg, "bgr8")->image;
    if (!occlusion_mask.empty()) {
        updated_opencv_mask = true;
    }
}

// node color and object color are in rgba format and range from 0-1
visualization_msgs::MarkerArray MatrixXf2MarkerArray (MatrixXf Y, std::string marker_frame, std::string marker_ns, std::vector<float> node_color, std::vector<float> line_color) {
    // publish the results as a marker array
    visualization_msgs::MarkerArray results = visualization_msgs::MarkerArray();
    for (int i = 0; i < Y.rows(); i ++) {
        visualization_msgs::Marker cur_node_result = visualization_msgs::Marker();
    
        // add header
        cur_node_result.header.frame_id = marker_frame;
        // cur_node_result.header.stamp = ros::Time::now();
        cur_node_result.type = visualization_msgs::Marker::SPHERE;
        cur_node_result.action = visualization_msgs::Marker::ADD;
        cur_node_result.ns = marker_ns + std::to_string(i);
        cur_node_result.id = i;

        // add position
        cur_node_result.pose.position.x = Y(i, 0);
        cur_node_result.pose.position.y = Y(i, 1);
        cur_node_result.pose.position.z = Y(i, 2);

        // add orientation
        cur_node_result.pose.orientation.w = 1.0;
        cur_node_result.pose.orientation.x = 0.0;
        cur_node_result.pose.orientation.y = 0.0;
        cur_node_result.pose.orientation.z = 0.0;

        // set scale
        cur_node_result.scale.x = 0.01;
        cur_node_result.scale.y = 0.01;
        cur_node_result.scale.z = 0.01;

        // set color
        cur_node_result.color.r = node_color[0];
        cur_node_result.color.g = node_color[1];
        cur_node_result.color.b = node_color[2];
        cur_node_result.color.a = node_color[3];

        results.markers.push_back(cur_node_result);

        // don't add line if at the last node
        if (i == Y.rows()-1) {
            break;
        }

        visualization_msgs::Marker cur_line_result = visualization_msgs::Marker();

        // add header
        cur_line_result.header.frame_id = "camera_color_optical_frame";
        cur_line_result.type = visualization_msgs::Marker::CYLINDER;
        cur_line_result.action = visualization_msgs::Marker::ADD;
        cur_line_result.ns = "line_results" + std::to_string(i);
        cur_line_result.id = i;

        // add position
        cur_line_result.pose.position.x = (Y(i, 0) + Y(i+1, 0)) / 2.0;
        cur_line_result.pose.position.y = (Y(i, 1) + Y(i+1, 1)) / 2.0;
        cur_line_result.pose.position.z = (Y(i, 2) + Y(i+1, 2)) / 2.0;

        // add orientation
        Eigen::Quaternionf q;
        Eigen::Vector3f vec1(0.0, 0.0, 1.0);
        Eigen::Vector3f vec2(Y(i+1, 0) - Y(i, 0), Y(i+1, 1) - Y(i, 1), Y(i+1, 2) - Y(i, 2));
        q.setFromTwoVectors(vec1, vec2);

        cur_line_result.pose.orientation.w = q.w();
        cur_line_result.pose.orientation.x = q.x();
        cur_line_result.pose.orientation.y = q.y();
        cur_line_result.pose.orientation.z = q.z();

        // set scale
        cur_line_result.scale.x = 0.005;
        cur_line_result.scale.y = 0.005;
        cur_line_result.scale.z = pt2pt_dis(Y.row(i), Y.row(i+1));

        // set color
        cur_line_result.color.r = line_color[0];
        cur_line_result.color.g = line_color[1];
        cur_line_result.color.b = line_color[2];
        cur_line_result.color.a = line_color[3];

        results.markers.push_back(cur_line_result);
    }

    return results;
}

// overload function
visualization_msgs::MarkerArray MatrixXf2MarkerArray (std::vector<MatrixXf> Y, std::string marker_frame, std::string marker_ns, std::vector<float> node_color, std::vector<float> line_color) {
    // publish the results as a marker array
    visualization_msgs::MarkerArray results = visualization_msgs::MarkerArray();
    for (int i = 0; i < Y.size(); i ++) {
        visualization_msgs::Marker cur_node_result = visualization_msgs::Marker();

        int dim = Y[0].cols();
    
        // add header
        cur_node_result.header.frame_id = marker_frame;
        // cur_node_result.header.stamp = ros::Time::now();
        cur_node_result.type = visualization_msgs::Marker::SPHERE;
        cur_node_result.action = visualization_msgs::Marker::ADD;
        cur_node_result.ns = marker_ns + std::to_string(i);
        cur_node_result.id = i;

        // add position
        cur_node_result.pose.position.x = Y[i](0, dim-3);
        cur_node_result.pose.position.y = Y[i](0, dim-2);
        cur_node_result.pose.position.z = Y[i](0, dim-1);

        // add orientation
        cur_node_result.pose.orientation.w = 1.0;
        cur_node_result.pose.orientation.x = 0.0;
        cur_node_result.pose.orientation.y = 0.0;
        cur_node_result.pose.orientation.z = 0.0;

        // set scale
        cur_node_result.scale.x = 0.01;
        cur_node_result.scale.y = 0.01;
        cur_node_result.scale.z = 0.01;

        // set color
        cur_node_result.color.r = node_color[0];
        cur_node_result.color.g = node_color[1];
        cur_node_result.color.b = node_color[2];
        cur_node_result.color.a = node_color[3];

        results.markers.push_back(cur_node_result);

        // don't add line if at the last node
        if (i == Y.size()-1) {
            break;
        }

        visualization_msgs::Marker cur_line_result = visualization_msgs::Marker();

        // add header
        cur_line_result.header.frame_id = "camera_color_optical_frame";
        cur_line_result.type = visualization_msgs::Marker::CYLINDER;
        cur_line_result.action = visualization_msgs::Marker::ADD;
        cur_line_result.ns = "line_results" + std::to_string(i);
        cur_line_result.id = i;

        // add position
        cur_line_result.pose.position.x = (Y[i](0, dim-3) + Y[i+1](0, dim-3)) / 2.0;
        cur_line_result.pose.position.y = (Y[i](0, dim-2) + Y[i+1](0, dim-2)) / 2.0;
        cur_line_result.pose.position.z = (Y[i](0, dim-1) + Y[i+1](0, dim-1)) / 2.0;

        // add orientation
        Eigen::Quaternionf q;
        Eigen::Vector3f vec1(0.0, 0.0, 1.0);
        Eigen::Vector3f vec2(Y[i+1](0, dim-3) - Y[i](0, dim-3), Y[i+1](0, dim-2) - Y[i](0, dim-2), Y[i+1](0, dim-1) - Y[i](0, dim-1));
        q.setFromTwoVectors(vec1, vec2);

        cur_line_result.pose.orientation.w = q.w();
        cur_line_result.pose.orientation.x = q.x();
        cur_line_result.pose.orientation.y = q.y();
        cur_line_result.pose.orientation.z = q.z();

        // set scale
        cur_line_result.scale.x = 0.005;
        cur_line_result.scale.y = 0.005;
        cur_line_result.scale.z = sqrt(pow(Y[i+1](0, dim-3) - Y[i](0, dim-3), 2) + pow(Y[i+1](0, dim-2) - Y[i](0, dim-2), 2) + pow(Y[i+1](0, dim-1) - Y[i](0, dim-1), 2));

        // set color
        cur_line_result.color.r = line_color[0];
        cur_line_result.color.g = line_color[1];
        cur_line_result.color.b = line_color[2];
        cur_line_result.color.a = line_color[3];

        results.markers.push_back(cur_line_result);
    }

    return results;
}

sensor_msgs::ImagePtr Callback(const sensor_msgs::ImageConstPtr& image_msg, const sensor_msgs::PointCloud2ConstPtr& pc_msg) {
    
    // log time
    std::chrono::steady_clock::time_point cur_time_cb = std::chrono::steady_clock::now();

    sensor_msgs::ImagePtr tracking_img_msg = nullptr;

    Mat mask_blue, mask_red_1, mask_red_2, mask_red, mask, mask_rgb;
    Mat cur_image_orig = cv_bridge::toCvShare(image_msg, "bgr8")->image;

    Mat cur_image_hsv;

    // convert color
    cv::cvtColor(cur_image_orig, cur_image_hsv, cv::COLOR_BGR2HSV);

    std::vector<int> lower_blue = {90, 90, 90};
    std::vector<int> upper_blue = {130, 255, 255};

    std::vector<int> lower_red_1 = {130, 60, 40};
    std::vector<int> upper_red_1 = {255, 255, 255};

    std::vector<int> lower_red_2 = {0, 60, 40};
    std::vector<int> upper_red_2 = {10, 255, 255};

    Mat mask_without_occlusion_block;

    if (use_eval_rope) {
        // filter blue
        cv::inRange(cur_image_hsv, cv::Scalar(lower_blue[0], lower_blue[1], lower_blue[2]), cv::Scalar(upper_blue[0], upper_blue[1], upper_blue[2]), mask_blue);

        // filter red
        cv::inRange(cur_image_hsv, cv::Scalar(lower_red_1[0], lower_red_1[1], lower_red_1[2]), cv::Scalar(upper_red_1[0], upper_red_1[1], upper_red_1[2]), mask_red_1);
        cv::inRange(cur_image_hsv, cv::Scalar(lower_red_2[0], lower_red_2[1], lower_red_2[2]), cv::Scalar(upper_red_2[0], upper_red_2[1], upper_red_2[2]), mask_red_2);

        // combine red mask
        cv::bitwise_or(mask_red_1, mask_red_2, mask_red);
        // combine overall mask
        cv::bitwise_or(mask_red, mask_blue, mask_without_occlusion_block);
    }
    else {
        // filter blue
        cv::inRange(cur_image_hsv, cv::Scalar(lower_blue[0], lower_blue[1], lower_blue[2]), cv::Scalar(upper_blue[0], upper_blue[1], upper_blue[2]), mask_blue);

        mask_blue.copyTo(mask_without_occlusion_block);
    }

    // update cur image for visualization
    Mat cur_image;
    Mat occlusion_mask_gray;
    if (updated_opencv_mask) {
        cv::cvtColor(occlusion_mask, occlusion_mask_gray, cv::COLOR_BGR2GRAY);
        cv::bitwise_and(mask_without_occlusion_block, occlusion_mask_gray, mask);
        cv::bitwise_and(cur_image_orig, occlusion_mask, cur_image);
    }
    else {
        mask_without_occlusion_block.copyTo(mask);
        cur_image_orig.copyTo(cur_image);
    }

    // simple blob detector
    std::vector<cv::KeyPoint> keypoints;
    if (use_eval_rope) {
        cv::SimpleBlobDetector::Params blob_params;
        blob_params.filterByColor = false;
        blob_params.filterByArea = true;
        blob_params.filterByCircularity = false;
        blob_params.filterByInertia = true;
        blob_params.filterByConvexity = false;
        cv::Ptr<cv::SimpleBlobDetector> detector = cv::SimpleBlobDetector::create(blob_params);
        // detect
        detector->detect(mask_red, keypoints);
    }

    cv::cvtColor(mask, mask_rgb, cv::COLOR_GRAY2BGR);

    // distance transform
    Mat bmask_transformed;
    cv::distanceTransform((255 - mask), bmask_transformed, cv::DIST_L2, 3);
    double mat_min, mat_max;
    cv::minMaxLoc(bmask_transformed, &mat_min, &mat_max);
    // std::cout << mat_min << ", " << mat_max << std::endl;
    Mat bmask_transformed_normalized = bmask_transformed/mat_max * 255;
    bmask_transformed_normalized.convertTo(bmask_transformed_normalized, CV_8U);
    double mask_dist_threshold = 10;

    // Mat tracking_img;
    // cur_image.copyTo(tracking_img);
    // for (cv::KeyPoint key_point : keypoints) {
    //     cv::circle(tracking_img, key_point.pt, 5, cv::Scalar(0, 150, 255), -1);
    // }

    // // publish image
    // tracking_img_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", tracking_img).toImageMsg();

    // pcl test
    sensor_msgs::PointCloud2 output;
    pcl::PCLPointCloud2* cloud = new pcl::PCLPointCloud2;

    // Convert to PCL data type
    pcl_conversions::toPCL(*pc_msg, *cloud);   // cloud is 720*1280 (height*width) now, however is a ros pointcloud2 message. 
                                               // see message definition here: http://docs.ros.org/en/melodic/api/sensor_msgs/html/msg/PointCloud2.html

    if (cloud->width != 0 && cloud->height != 0) {
        // convert to xyz point
        pcl::PointCloud<pcl::PointXYZRGB> cloud_xyz;
        pcl::fromPCLPointCloud2(*cloud, cloud_xyz);
        // now create objects for cur_pc
        pcl::PCLPointCloud2* cur_pc = new pcl::PCLPointCloud2;
        pcl::PointCloud<pcl::PointXYZRGB> cur_pc_xyz;
        pcl::PointCloud<pcl::PointXYZRGB> cur_nodes_xyz;
        pcl::PointCloud<pcl::PointXYZRGB> downsampled_xyz;

        // filter point cloud from mask
        for (int i = 0; i < cloud->height; i ++) {
            for (int j = 0; j < cloud->width; j ++) {
                if (mask.at<uchar>(i, j) != 0) {
                    cur_pc_xyz.push_back(cloud_xyz(j, i));   // note: this is (j, i) not (i, j)
                }
            }
        }

        // convert back to pointcloud2 message
        pcl::toPCLPointCloud2(cur_pc_xyz, *cur_pc);
        // Perform downsampling
        pcl::PCLPointCloud2ConstPtr cloudPtr(cur_pc);
        pcl::PCLPointCloud2 cur_pc_downsampled;
        pcl::VoxelGrid<pcl::PCLPointCloud2> sor;
        sor.setInputCloud (cloudPtr);
        sor.setLeafSize (0.005, 0.005, 0.005);
        sor.filter (cur_pc_downsampled);

        pcl::fromPCLPointCloud2(cur_pc_downsampled, downsampled_xyz);
        MatrixXf X = downsampled_xyz.getMatrixXfMap().topRows(3).transpose();
        std::cout << "num of points: " << X.rows() << std::endl;

        if (use_eval_rope) {
            for (cv::KeyPoint key_point : keypoints) {
                cur_nodes_xyz.push_back(cloud_xyz(static_cast<int>(key_point.pt.x), static_cast<int>(key_point.pt.y)));
            }
        }

        // std::cout << Y_0_sorted.rows() << ", " << Y_0_sorted.cols() << std::endl;

        // log time
        std::chrono::steady_clock::time_point cur_time = std::chrono::steady_clock::now();

        MatrixXf guide_nodes;
        std::vector<MatrixXf> priors;

        if (!initialized) {
            if (use_eval_rope) {
                MatrixXf Y_0 = cur_nodes_xyz.getMatrixXfMap().topRows(3).transpose();
                MatrixXf Y_0_sorted = sort_pts(Y_0);
                Y = Y_0_sorted.replicate(1, 1);
                sigma2 = 0;

                // record geodesic coord
                double cur_sum = 0;
                for (int i = 0; i < Y_0_sorted.rows()-1; i ++) {
                    cur_sum += (Y_0_sorted.row(i+1) - Y_0_sorted.row(i)).norm();
                    converted_node_coord.push_back(cur_sum);
                }

                // use ecpd to help initialize
                std::vector<MatrixXf> priors_vec;
                for (int i = 0; i < Y_0_sorted.rows(); i ++) {
                    MatrixXf temp = MatrixXf::Zero(1, 4);
                    temp(0, 0) = i;
                    temp(0, 1) = Y_0_sorted(i, 0);
                    temp(0, 2) = Y_0_sorted(i, 1);
                    temp(0, 3) = Y_0_sorted(i, 2);
                    priors_vec.push_back(temp);
                }

                ecpd_lle(X, Y, sigma2, 1, 1, 1, 0.05, 50, 0.00001, true, true, false, true, priors_vec, 0.1);
            }
            else {
                reg(X, Y, sigma2, num_of_nodes, 0.05, 50);
                Y = sort_pts(Y);
                // record geodesic coord
                double cur_sum = 0;
                for (int i = 0; i < Y.rows()-1; i ++) {
                    cur_sum += (Y.row(i+1) - Y.row(i)).norm();
                    converted_node_coord.push_back(cur_sum);
                }
            }

            initialized = true;
        } 
        else {
            // ecpd_lle (X, Y, sigma2, 0.5, 1, 1, 0.05, 50, 0.00001, true, true, false, false);
            tracking_step(X, Y, sigma2, guide_nodes, priors, converted_node_coord, 0, mask, bmask_transformed_normalized, mask_dist_threshold, mat_max);
        }

        std::cout << "Registration time difference = " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cur_time).count() << "[ms]" << std::endl;

        MatrixXf nodes_h = Y.replicate(1, 1);
        nodes_h.conservativeResize(nodes_h.rows(), nodes_h.cols()+1);
        nodes_h.col(nodes_h.cols()-1) = MatrixXf::Ones(nodes_h.rows(), 1);

        // project and pub image
        MatrixXf proj_matrix(3, 4);
        proj_matrix << 918.359130859375, 0.0, 645.8908081054688, 0.0,
                       0.0, 916.265869140625, 354.02392578125, 0.0,
                       0.0, 0.0, 1.0, 0.0;
        MatrixXf image_coords = (proj_matrix * nodes_h.transpose()).transpose();

        Mat tracking_img;
        tracking_img = 0.5*cur_image_orig + 0.5*cur_image;

        // cur_image.copyTo(tracking_img);

        // draw points
        for (int i = 0; i < image_coords.rows(); i ++) {

            int row = static_cast<int>(image_coords(i, 0)/image_coords(i, 2));
            int col = static_cast<int>(image_coords(i, 1)/image_coords(i, 2));

            cv::Scalar point_color;
            cv::Scalar line_color;

            
            // std::cout << "bmask dist = " << static_cast<int>(bmask_transformed_normalized.at<uchar>(col, row)) << std::endl;
            // std::cout << "mask val = " << static_cast<int>(mask.at<uchar>(col, row)) << std::endl;
            
            if (static_cast<int>(bmask_transformed_normalized.at<uchar>(col, row)) < mask_dist_threshold / mat_max * 255) {
                point_color = cv::Scalar(0, 150, 255);
                line_color = cv::Scalar(0, 255, 0);
            }
            else {
                point_color = cv::Scalar(0, 0, 255);
                line_color = cv::Scalar(0, 0, 255);
            }

            cv::circle(tracking_img, cv::Point(row, col), 5, point_color, -1);

            if (i != image_coords.rows()-1) {
                cv::line(tracking_img, cv::Point(row, col),
                                       cv::Point(static_cast<int>(image_coords(i+1, 0)/image_coords(i+1, 2)), 
                                                 static_cast<int>(image_coords(i+1, 1)/image_coords(i+1, 2))),
                                                 line_color, 2);
            }
        }

        // // visualize distance transform result
        // cv::imshow("frame", bmask_transformed_normalized);
        // cv::waitKey(3);

        // publish image
        tracking_img_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", tracking_img).toImageMsg();
        // tracking_img_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", bmask_transformed_rgb).toImageMsg();

        // fill in header
        cur_pc->header.frame_id = "camera_color_optical_frame";
        cur_pc->header.seq = cloud->header.seq;
        cur_pc->fields = cloud->fields;

        cur_pc_downsampled.header.frame_id = "camera_color_optical_frame";
        cur_pc_downsampled.header.seq = cloud->header.seq;
        cur_pc_downsampled.fields = cloud->fields;

        // Convert to ROS data type
        // pcl_conversions::moveFromPCL(*cur_pc, output);
        pcl_conversions::moveFromPCL(cur_pc_downsampled, output);

        // publish the results as a marker array
        visualization_msgs::MarkerArray results = MatrixXf2MarkerArray(Y, "camera_color_optical_frame", "node_results", {1.0, 150.0/255.0, 0.0, 0.75}, {0.0, 1.0, 0.0, 0.75});
        visualization_msgs::MarkerArray guide_nodes_results = MatrixXf2MarkerArray(guide_nodes, "camera_color_optical_frame", "guide_node_results", {0.0, 0.0, 0.0, 0.5}, {0.0, 0.0, 1.0, 0.5});
        visualization_msgs::MarkerArray priors_results = MatrixXf2MarkerArray(priors, "camera_color_optical_frame", "priors_results", {1.0, 1.0, 1.0, 0.5}, {1.0, 1.0, 0.0, 0.5});

        results_pub.publish(results);
        guide_nodes_pub.publish(guide_nodes_results);
        priors_pub.publish(priors_results);
    }
    else {
        ROS_ERROR("empty pointcloud!");
    }

    // std::this_thread::sleep_for(std::chrono::milliseconds(150));

    pc_pub.publish(output);

    std::cout << "total time difference = " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cur_time_cb).count() << "[ms]" << std::endl;
    
    return tracking_img_msg;
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "image_listener");
    ros::NodeHandle nh;

    image_transport::ImageTransport it(nh);
    image_transport::Subscriber opencv_mask_sub = it.subscribe("/mask_with_occlusion", 1, update_opencv_mask);
    image_transport::Publisher mask_pub = it.advertise("/mask", 1);
    image_transport::Publisher tracking_img_pub = it.advertise("/tracking_img", 1);
    pc_pub = nh.advertise<sensor_msgs::PointCloud2>("/pts", 1);
    results_pub = nh.advertise<visualization_msgs::MarkerArray>("/results", 1);
    guide_nodes_pub = nh.advertise<visualization_msgs::MarkerArray>("/guide_nodes", 1);
    priors_pub = nh.advertise<visualization_msgs::MarkerArray>("/priors", 1);

    // image_transport::Subscriber sub = it.subscribe("/camera/color/image_raw", 1, [&](const sensor_msgs::ImageConstPtr& msg){
    //     sensor_msgs::ImagePtr test_image = imageCallback(msg);
    //     mask_pub.publish(test_image);
    // });

    message_filters::Subscriber<sensor_msgs::Image> image_sub(nh, "/camera/color/image_raw", 1);
    message_filters::Subscriber<sensor_msgs::PointCloud2> pc_sub(nh, "/camera/depth/color/points", 1);
    message_filters::TimeSynchronizer<sensor_msgs::Image, sensor_msgs::PointCloud2> sync(image_sub, pc_sub, 1);

    sync.registerCallback<std::function<void(const sensor_msgs::ImageConstPtr&, 
                                             const sensor_msgs::PointCloud2ConstPtr&,
                                             const boost::shared_ptr<const message_filters::NullType>,
                                             const boost::shared_ptr<const message_filters::NullType>,
                                             const boost::shared_ptr<const message_filters::NullType>,
                                             const boost::shared_ptr<const message_filters::NullType>,
                                             const boost::shared_ptr<const message_filters::NullType>,
                                             const boost::shared_ptr<const message_filters::NullType>,
                                             const boost::shared_ptr<const message_filters::NullType>)>>
    (
        [&](const sensor_msgs::ImageConstPtr& img_msg, 
            const sensor_msgs::PointCloud2ConstPtr& pc_msg,
            const boost::shared_ptr<const message_filters::NullType> var1,
            const boost::shared_ptr<const message_filters::NullType> var2,
            const boost::shared_ptr<const message_filters::NullType> var3,
            const boost::shared_ptr<const message_filters::NullType> var4,
            const boost::shared_ptr<const message_filters::NullType> var5,
            const boost::shared_ptr<const message_filters::NullType> var6,
            const boost::shared_ptr<const message_filters::NullType> var7)
        {
            // sensor_msgs::ImagePtr test_image = imageCallback(msg, _);
            // mask_pub.publish(test_image);
            sensor_msgs::ImagePtr tracking_img = Callback(img_msg, pc_msg);
            tracking_img_pub.publish(tracking_img);
        }
    );
    
    ros::spin();
}