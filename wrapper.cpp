﻿#include <pcl/PCLPointCloud2.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>
#include <pcl/visualization/pcl_visualizer.h>
#include "build/wrapper.h"
#include <fstream>
#include <pcl/search/search.h>
#include <pcl/search/kdtree.h>
#include <pcl/features/normal_3d.h>
#include <pcl/filters/passthrough.h>
#include <pcl/segmentation/region_growing.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/visualization/keyboard_event.h>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
//#include <pcl/io/openni_grabber.h>
#include <pcl/console/parse.h>
#include <pcl/common/time.h>
#include <pcl/common/centroid.h>

#include <pcl/visualization/cloud_viewer.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/io/pcd_io.h>

#include <pcl/filters/passthrough.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/approximate_voxel_grid.h>

#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>

#include <pcl/search/pcl_search.h>
#include <pcl/common/transforms.h>
#include <pcl/registration/icp.h>

#include <boost/format.hpp>

typedef pcl::PointXYZ RefPointType;
typedef pcl::PointCloud<pcl::PointXYZ> Cloud;
typedef Cloud::Ptr CloudPtr;
typedef Cloud::ConstPtr CloudConstPtr;


using namespace std;

extern "C" {
	void filterPassThrough(const CloudConstPtr &cloud, Cloud &result)
	{
		pcl::PassThrough<pcl::PointXYZ> pass;
		pass.setFilterFieldName("z");
		pass.setFilterLimits(0.0, 10.0);
		pass.setKeepOrganized(false);
		pass.setInputCloud(cloud);
		pass.filter(result);
	}

	void gridSampleApprox(const CloudConstPtr &cloud, Cloud &result, double leaf_size)
	{
		pcl::ApproximateVoxelGrid<pcl::PointXYZ> grid;
		grid.setLeafSize(static_cast<float> (leaf_size), static_cast<float> (leaf_size), static_cast<float> (leaf_size));
		grid.setInputCloud(cloud);
		grid.filter(result);
	}

	int initialGuess(PointCloudT::Ptr object, PointCloudT::Ptr scene, Eigen::Matrix4f &transformation)
	{
		// Point clouds
		//PointCloudT::Ptr object(new PointCloudT);
		PointCloudT::Ptr object_aligned(new PointCloudT);
		//PointCloudT::Ptr scene(new PointCloudT);
		FeatureCloudT::Ptr object_features(new FeatureCloudT);
		FeatureCloudT::Ptr scene_features(new FeatureCloudT);

		//pcl::io::loadPCDFile<PointNT>("chef.pcd", *object);
		//pcl::io::loadPCDFile<PointNT>("rs1.pcd", *scene);
		//pcl::io::savePLYFileASCII<PointNT>("chef.ply", *object);
		//pcl::io::savePLYFileASCII<PointNT>("rs1.ply", *scene);

		std::cout << "object size: " << object->size() << std::endl;
		std::cout << "scene size : " << scene->size() << std::endl;

		// Downsample
		pcl::console::print_highlight("Downsampling...\n");
		pcl::VoxelGrid<PointNT> grid;
		const float leaf = 0.005f;
		grid.setLeafSize(leaf, leaf, leaf);
		grid.setInputCloud(object);
		grid.filter(*object);
		grid.setInputCloud(scene);
		grid.filter(*scene);

		std::cout << "object size: " << object->size() << std::endl;
		std::cout << "scene size : " << scene->size() << std::endl;

		// Estimate normals for scene
		pcl::console::print_highlight("Estimating scene normals...\n");
		pcl::NormalEstimationOMP<PointNT, PointNT> nest;
		nest.setRadiusSearch(0.01);
		nest.setInputCloud(scene);
		nest.compute(*scene);

		// Estimate features
		pcl::console::print_highlight("Estimating features...\n");
		FeatureEstimationT fest;
		fest.setRadiusSearch(0.025);
		fest.setInputCloud(object);
		fest.setInputNormals(object);
		fest.compute(*object_features);
		fest.setInputCloud(scene);
		fest.setInputNormals(scene);
		fest.compute(*scene_features);

		// Perform alignment
		pcl::console::print_highlight("Starting alignment...\n");
		pcl::SampleConsensusPrerejective<PointNT, PointNT, FeatureT> align;
		align.setInputSource(object);
		align.setSourceFeatures(object_features);
		align.setInputTarget(scene);
		align.setTargetFeatures(scene_features);
		align.setMaximumIterations(50000); // Number of RANSAC iterations
		align.setNumberOfSamples(3); // Number of points to sample for generating/prerejecting a pose
		align.setCorrespondenceRandomness(5); // Number of nearest features to use
		align.setSimilarityThreshold(0.9f); // Polygonal edge length similarity threshold
		align.setMaxCorrespondenceDistance(2.5f * leaf); // Inlier threshold
		align.setInlierFraction(0.25f); // Required inlier fraction for accepting a pose hypothesis
		{
			pcl::ScopeTime t("Alignment");
			align.align(*object_aligned);
		}

		if (align.hasConverged())
		{
			// Print results
			printf("\n");
			transformation = align.getFinalTransformation();

		}
		else
		{
			pcl::console::print_error("Alignment failed!\n");
			return (1);
		}

		return (0);
	}

	//void tracking(CloudPtr &cloud)
	//{
	//	CloudPtr cloud_pass_;
	//	CloudPtr cloud_pass_downsampled_;
	//	CloudPtr target_cloud;
	//	boost::shared_ptr<ParticleFilter> tracker_;
	//	bool new_cloud_;
	//	target_cloud.reset(new Cloud());
	//	if (pcl::io::loadPCDFile("pc.pcd", *target_cloud) == -1) {
	//		std::cout << "pcd file not found" << std::endl;
	//		exit(-1);
	//	}

	//	new_cloud_ = false;
	//	double downsampling_grid_size_ = 0.002;

	//	std::vector<double> default_step_covariance = std::vector<double>(6, 0.015 * 0.015);
	//	default_step_covariance[3] *= 40.0;
	//	default_step_covariance[4] *= 40.0;
	//	default_step_covariance[5] *= 40.0;

	//	std::vector<double> initial_noise_covariance = std::vector<double>(6, 0.00001);
	//	std::vector<double> default_initial_mean = std::vector<double>(6, 0.0);

	//	boost::shared_ptr<KLDAdaptiveParticleFilterOMPTracker<RefPointType, ParticleT> > tracker
	//	(new KLDAdaptiveParticleFilterOMPTracker<RefPointType, ParticleT>(8));

	//	ParticleT bin_size;
	//	bin_size.x = 0.1f;
	//	bin_size.y = 0.1f;
	//	bin_size.z = 0.1f;
	//	bin_size.roll = 0.1f;
	//	bin_size.pitch = 0.1f;
	//	bin_size.yaw = 0.1f;


	//	//Set all parameters for  KLDAdaptiveParticleFilterOMPTracker
	//	tracker->setMaximumParticleNum(1000);
	//	tracker->setDelta(0.99);
	//	tracker->setEpsilon(0.2);
	//	tracker->setBinSize(bin_size);

	//	//Set all parameters for  ParticleFilter
	//	tracker_ = tracker;
	//	tracker_->setTrans(Eigen::Affine3f::Identity());
	//	tracker_->setStepNoiseCovariance(default_step_covariance);
	//	tracker_->setInitialNoiseCovariance(initial_noise_covariance);
	//	tracker_->setInitialNoiseMean(default_initial_mean);
	//	tracker_->setIterationNum(1);
	//	tracker_->setParticleNum(600);
	//	tracker_->setResampleLikelihoodThr(0.00);
	//	tracker_->setUseNormal(false);
	//	ApproxNearestPairPointCloudCoherence<RefPointType>::Ptr coherence = ApproxNearestPairPointCloudCoherence<RefPointType>::Ptr
	//	(new ApproxNearestPairPointCloudCoherence<RefPointType>());

	//	boost::shared_ptr<DistanceCoherence<RefPointType> > distance_coherence
	//		= boost::shared_ptr<DistanceCoherence<RefPointType> >(new DistanceCoherence<RefPointType>());
	//	coherence->addPointCoherence(distance_coherence);

	//	boost::shared_ptr<pcl::search::Octree<RefPointType> > search(new pcl::search::Octree<RefPointType>(0.01));
	//	coherence->setSearchMethod(search);
	//	coherence->setMaximumDistance(0.01);

	//	tracker_->setCloudCoherence(coherence);

	//	//prepare the model of tracker's target
	//	Eigen::Vector4f c;
	//	Eigen::Affine3f trans = Eigen::Affine3f::Identity();
	//	CloudPtr transed_ref(new Cloud);
	//	CloudPtr transed_ref_downsampled(new Cloud);

	//	pcl::compute3DCentroid<RefPointType>(*target_cloud, c);
	//	trans.translation().matrix() = Eigen::Vector3f(c[0], c[1], c[2]);
	//	pcl::transformPointCloud<RefPointType>(*target_cloud, *transed_ref, trans.inverse());
	//	gridSampleApprox(transed_ref, *transed_ref_downsampled, downsampling_grid_size_);

	//	//set reference model and trans
	//	tracker_->setReferenceCloud(transed_ref_downsampled);
	//	tracker_->setTrans(trans);
	//	cloud_pass_.reset(new Cloud);
	//	cloud_pass_downsampled_.reset(new Cloud);
	//	filterPassThrough(cloud, *cloud_pass_);
	//	gridSampleApprox(cloud_pass_, *cloud_pass_downsampled_, downsampling_grid_size_);
	//	tracker_->setInputCloud(cloud_pass_downsampled_);
	//	tracker_->compute();
	//	new_cloud_ = true;
	//	ParticleFilter::PointCloudStatePtr particles = tracker_->getParticles();
	//	if (particles && new_cloud_)
	//	{
	//		//Set pointCloud with particle's points
	//		pcl::PointCloud<pcl::PointXYZ>::Ptr particle_cloud(new pcl::PointCloud<pcl::PointXYZ>());
	//		for (size_t i = 0; i < particles->points.size(); i++) {
	//			pcl::PointXYZ point;

	//			point.x = particles->points[i].x;
	//			point.y = particles->points[i].y;
	//			point.z = particles->points[i].z;
	//			particle_cloud->points.push_back(point);
	//		}

	//		pcl::visualization::PCLVisualizer::Ptr viewer(new pcl::visualization::PCLVisualizer);
	//		uint8_t pc_r = 255, pc_g = 0, pc_b = 0;
	//		uint32_t pc_red = ((uint32_t)pc_r << 16 | (uint32_t)pc_g << 8 | (uint32_t)pc_b);
	//		pc_r = 0, pc_g = 255, pc_b = 0;
	//		uint32_t pc_green = ((uint32_t)pc_r << 16 | (uint32_t)pc_g << 8 | (uint32_t)pc_b);
	//		pcl::PointCloud<pcl::PointXYZRGB>::Ptr filtered_cloud_color(new pcl::PointCloud<pcl::PointXYZRGB>);
	//		pcl::copyPointCloud(*particle_cloud, *filtered_cloud_color);
	//		for (size_t i = 0; i < filtered_cloud_color->size(); i++)
	//		{
	//			filtered_cloud_color->points[i].rgb = *reinterpret_cast<float*>(&pc_green);
	//		}
	//		pcl::io::savePCDFileASCII("particle.pcd", *filtered_cloud_color);
	//		pcl::io::savePCDFileASCII("cloud.pcd", *cloud);
	//		pcl::io::savePCDFileASCII("sampled.pcd", *cloud_pass_downsampled_);
	//		viewer->addPointCloud(filtered_cloud_color, "particle");
	//		viewer->addPointCloud(cloud_pass_downsampled_, "cloud");
	//		viewer->addCoordinateSystem(0.2);
	//		
	//	}

	//}

	int detectplane(float* source, int size, float* transInfo)
	{
		//pcl::visualization::PCLVisualizer::Ptr viewer(new pcl::visualization::PCLVisualizer);
		//pcl::visualization::PCLVisualizer *viewer = static_cast<pcl::visualization::PCLVisualizer *> (viewer_void);
		pcl::PCLPointCloud2 cloud;
		cloud.data.clear();
		cloud.data.resize(size * sizeof(float));

		//uint8_t *start = reinterpret_cast<uint8_t*> (source);

		pcl::PCDReader reader;
		if (reader.readHeader("bunny.pcd", cloud) == -1) {
			//cout << "read header error" << endl;
			//fout << "read header error\n";
			return (-1);
		}

		memcpy(&cloud.data[0], source, size * sizeof(float));
		cloud.width = (uint32_t)(size / 3);
		pcl::PointCloud<pcl::PointXYZ>::Ptr pc(new pcl::PointCloud<pcl::PointXYZ>);
		pcl::fromPCLPointCloud2(cloud, *pc);

		//tracking(pc);

		//fout << "after pc2" << pc->points.size() << endl;
		// ransac plane detection
		//创建一个模型参数对象，用于记录结果
		pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients);
		//inliers表示误差能容忍的点 记录的是点云的序号
		pcl::PointIndices::Ptr inliers(new pcl::PointIndices);
		// 创建一个分割器
		pcl::SACSegmentation<pcl::PointXYZ> seg;
		seg.setMaxIterations(100);
		// Optional
		seg.setOptimizeCoefficients(true);
		// Mandatory-设置目标几何形状
		seg.setModelType(pcl::SACMODEL_PLANE);
		//分割方法：随机采样法
		seg.setMethodType(pcl::SAC_RANSAC);
		//设置误差容忍范围
		seg.setDistanceThreshold(0.0250);
		//输入点云
		seg.setInputCloud(pc);
		//分割点云
		seg.segment(*inliers, *coefficients);

		pcl::PointCloud<pcl::PointXYZ>::Ptr segmentCloud(new pcl::PointCloud<pcl::PointXYZ>);
		pcl::ExtractIndices<pcl::PointXYZ> extract;
		extract.setInputCloud(pc);
		extract.setIndices(inliers);
		extract.setNegative(true);
		extract.filter(*segmentCloud);

		pcl::PassThrough<pcl::PointXYZ> pass;
		pass.setFilterFieldName("z");
		pass.setFilterLimits(0.0, 0.4);
		pass.setFilterFieldName("x");
		pass.setFilterLimits(-0.4, 0.4);
		pass.setKeepOrganized(false);
		pass.setInputCloud(segmentCloud);
		pass.filter(*segmentCloud);
		//pcl::visualization::PCLVisualizer::Ptr viewer(new pcl::visualization::PCLVisualizer);
		//viewer->updatePointCloud(segmentCloud,"cloud");
		//viewer->addCoordinateSystem(0.2);




		////======= Visualization of plane ======
		//pcl::PointCloud<pcl::PointXYZRGB>::Ptr pc_color(new pcl::PointCloud<pcl::PointXYZRGB>);
		//pcl::copyPointCloud(*pc, *pc_color);
		//uint8_t pc_r = 255, pc_g = 0, pc_b = 0;
		//uint32_t pc_red = ((uint32_t)pc_r << 16 | (uint32_t)pc_g << 8 | (uint32_t)pc_b);
		//pc_r = 0, pc_g = 255, pc_b = 0;
		//uint32_t pc_green = ((uint32_t)pc_r << 16 | (uint32_t)pc_g << 8 | (uint32_t)pc_b);
		/*for (size_t i = 0; i < pc_color->size(); i++)
		{
			pc_color->points[i].rgb = *reinterpret_cast<float*>(&pc_green);
		}
		*/
		float sum_x=0, sum_y=0, sum_z=0;

		for (size_t i = 0; i < inliers->indices.size(); i++)
		{
			sum_x += pc->points[inliers->indices[i]].x;
			sum_y += pc->points[inliers->indices[i]].y;
			sum_z += pc->points[inliers->indices[i]].z;
		}
		float avg_x = sum_x / inliers->indices.size();
		float avg_y = sum_y / inliers->indices.size();
		float avg_z = sum_z / inliers->indices.size();
		Eigen::Vector3d u(0, 1, 0);
		Eigen::Vector3d v(-coefficients->values[0], -coefficients->values[1], -coefficients->values[2]);
		Eigen::Vector3d temp = u.cross(v);
		double quat_w = u.norm()*v.norm() + u.dot(v);
		Eigen::Vector4d quat;
		quat << temp, quat_w;
		quat.normalize();

		transInfo[0] = avg_x;
		transInfo[1] = avg_y;
		transInfo[2] = avg_z;
		transInfo[3] = quat[0];
		transInfo[4] = quat[1];
		transInfo[5] = quat[2];
		transInfo[6] = quat[3];

		//pcl::visualization::PCLVisualizer::Ptr viewer(new pcl::visualization::PCLVisualizer);
		//viewer->addPointCloud(pc,"cloud");
		//viewer->addCoordinateSystem(0.2);
		Eigen::Matrix4f pose;

		Eigen::Matrix3f rotm;
		rotm = Eigen::Quaternionf((float)quat[3], (float)quat[0], (float)quat[1], (float)quat[2]).toRotationMatrix();
		pose.block<3, 3>(0, 0) = rotm;
		pose.block<3, 1>(0, 3) = Eigen::Vector3f(avg_x, avg_y, avg_z);
		pose.bottomRows(1).setZero();
		
		pose(3, 3) = 1;
		Eigen::Affine3f pose_aff;
		pose_aff.matrix() = pose;

		/*
		viewer->addCoordinateSystem(0.2,pose_aff);
		pcl::PointXYZ p1(avg_x,avg_y,avg_z);
		pcl::PointXYZ p2(avg_x+ coefficients->values[0], avg_y+ coefficients->values[1], avg_z+ coefficients->values[2]);
		//viewer->addArrow<pcl::PointXYZ,pcl::PointXYZ>(p1,p2,1,0,0,"arrow",0);
		viewer->addLine<pcl::PointXYZ, pcl::PointXYZ>(p1, p2);
		//viewer->spinOnce(100);
		*/
		return 0;
	}


	int dataConverter(float* source, int size, float* vir_pose, float* initial_guess, float* output_pose, bool isFirst)
	{
		ofstream fout;
		fout.open("test.txt");
		fout << "size: " << size << endl;
		pcl::visualization::PCLVisualizer::Ptr viewer(new pcl::visualization::PCLVisualizer);
		pcl::PCLPointCloud2 cloud;
		cloud.data.clear();
		cloud.data.resize(size * sizeof(float));
		
		//uint8_t *start = reinterpret_cast<uint8_t*> (source);

		pcl::PCDReader reader;
		if (reader.readHeader("bunny.pcd", cloud) == -1) {
			//cout << "read header error" << endl;
			fout << "read header error\n";
			return (-1);
		}

		//cout << "point step" << cloud.point_step << endl;
		fout << "point step:\t" << cloud.point_step << endl;
		//for test
		for (auto f : cloud.fields) {
			//cout << f;
			//cout << "next field" << endl;
			fout << f;
			fout << "next field\n";
		}
		//fout.close();
		
		memcpy(&cloud.data[0], source, size * sizeof(float));
		cloud.width = (uint32_t)(size / 3);
		pcl::PointCloud<pcl::PointXYZ>::Ptr pc(new pcl::PointCloud<pcl::PointXYZ>);
		pcl::fromPCLPointCloud2(cloud, *pc);
		fout << "after pc2" << pc->points.size() << endl;


		// ransac plane detection
		//创建一个模型参数对象，用于记录结果
		pcl::ModelCoefficients::Ptr coefficients(new pcl::ModelCoefficients);
		//inliers表示误差能容忍的点 记录的是点云的序号
		pcl::PointIndices::Ptr inliers(new pcl::PointIndices);
		// 创建一个分割器
		pcl::SACSegmentation<pcl::PointXYZ> seg;
		seg.setMaxIterations(100);

		// Optional
		seg.setOptimizeCoefficients(true);
		// Mandatory-设置目标几何形状
		seg.setModelType(pcl::SACMODEL_PLANE);
		//分割方法：随机采样法
		seg.setMethodType(pcl::SAC_RANSAC);
		//设置误差容忍范围
		seg.setDistanceThreshold(0.0250);
		//输入点云
		seg.setInputCloud(pc);
		//分割点云
		seg.segment(*inliers, *coefficients);

		pcl::PointCloud<pcl::PointXYZRGB>::Ptr pc_color(new pcl::PointCloud<pcl::PointXYZRGB>);
		pcl::copyPointCloud(*pc, *pc_color);
		
		
		uint8_t pc_r = 255, pc_g = 0, pc_b = 0;
		uint32_t pc_red = ((uint32_t)pc_r << 16 | (uint32_t)pc_g << 8 | (uint32_t)pc_b);
		pc_r = 0, pc_g = 255, pc_b = 0;
		uint32_t pc_green = ((uint32_t)pc_r << 16 | (uint32_t)pc_g << 8 | (uint32_t)pc_b);
		
		for (size_t i = 0; i < pc_color->size(); i++)
		{
			pc_color->points[i].rgb = *reinterpret_cast<float*>(&pc_green);
		}

		for (size_t i = 0; i < inliers->indices.size(); i++)
		{
			pc_color->points[inliers->indices[i]].rgb = *reinterpret_cast<float*>(&pc_red);
		}
		viewer->addPointCloud(pc_color, "pc_rgb");
		//viewer->addCoordinateSystem(0.2);
		
		
		pcl::PointCloud<pcl::PointXYZ>::Ptr above_plane(new pcl::PointCloud<pcl::PointXYZ>);
		above_plane->clear();
		for (int i = 0; i < pc->points.size();i++)
		{
			auto point = pc->points[i];
			float classifier = point.x*coefficients->values[0] + point.y*coefficients->values[1] + point.z*coefficients->values[2] + coefficients->values[3];
			if (coefficients->values[2]>0)
			{
				classifier = -classifier;
			}
			if (classifier > 0.005 && point.z < 1.0f)
			{
				above_plane->push_back(point);
			}
		}
		pcl::PointCloud<pcl::PointXYZ>::Ptr cad_bunny(new pcl::PointCloud<pcl::PointXYZ>);
		pcl::io::loadPCDFile("bunny.pcd", *cad_bunny);
		
		viewer->addPointCloud(cad_bunny, "bunny");
		pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> rgb(above_plane, 255, 255, 80);
		viewer->addPointCloud(above_plane, rgb, "above_plane");
		viewer->addCoordinateSystem(0.2);
		return 0;

		Eigen::Matrix4f pose = Eigen::Matrix4f::Identity();
		if (!isFirst)
		{
			/*pose.block<3, 1>(0, 3) = Eigen::Vector3f(initial_guess[0], initial_guess[1], initial_guess[2]);
			Eigen::Quaternionf q(initial_guess[3], initial_guess[4], initial_guess[5], initial_guess[6]);
			pose.block<3, 3>(0, 0) = q.normalized().toRotationMatrix();*/
			memcpy(pose.data(), initial_guess,16*sizeof(float));
			
		}
		else
		{
			PointCloudT::Ptr scene(new PointCloudT);
			PointCloudT::Ptr object(new PointCloudT);

			pcl::copyPointCloud(*pc, *scene);
			pcl::copyPointCloud(*cad_bunny, *object);
			Eigen::Matrix4f transformation;
			
			if (initialGuess(object, scene, transformation) == 0)
			{
				pose = transformation;
			}
		}

		pcl::transformPointCloud(*cad_bunny, *cad_bunny, pose);

		pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
		icp.setInputCloud(cad_bunny);
		icp.setInputTarget(above_plane);
		// Set the max correspondence distance to 5cm (e.g., correspondences with higher distances will be ignored)
		icp.setMaxCorrespondenceDistance(0.1);
		// Set the maximum number of iterations (criterion 1)
		icp.setMaximumIterations(50);
		// Set the transformation epsilon (criterion 2)
		icp.setTransformationEpsilon(1e-10);
		// Set the euclidean distance difference epsilon (criterion 3)
		icp.setEuclideanFitnessEpsilon(1);
		
		pcl::PointCloud<pcl::PointXYZ> Final;
		icp.align(Final);

		Eigen::Matrix4f d_pose = icp.getFinalTransformation();
		pcl::transformPointCloud(*cad_bunny, *cad_bunny, d_pose);
		pose = d_pose * pose;

		/*viewer->addPointCloud(cad_bunny,"bunny");
		pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> rgb(above_plane, 0, 0, 255);
		viewer->addPointCloud(above_plane, rgb,"above_plane");*/

		memcpy(output_pose, pose.data(), 16*sizeof(float) );
        /*
		pcl::PLYWriter writer;
		//pcl::io::savePCDFileASCII("pc.pcd", *pc);
		pcl::io::savePLYFile("seg_cloud.ply", *colored_cloud);
		*/
		pcl::io::savePCDFileASCII("pc.pcd", *pc);
		fout.close();
		return 0;
		
	}
}

//int main() {
//	float* pF = nullptr;
//	int size = 0;
//	if (dataConverter(pF, size)==0) {
//		cout << "Success!\n";
//	}
//}