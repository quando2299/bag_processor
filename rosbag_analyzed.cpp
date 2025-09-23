#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <memory>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>
#include <chrono>
#include <ctime>

// ROS includes
#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <sensor_msgs/Image.h>
#include <cv_bridge/cv_bridge.h>

// OpenCV includes
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>

// Boost for filesystem (C++14 compatible)
#include <boost/filesystem.hpp>

// Helper function to generate timestamp string
std::string generate_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}

class BagProcessor {
private:
    std::string bag_path_;
    std::string output_dir_;
    std::string timestamp_;
    
    struct TopicInfo {
        std::string topic_name;
        std::string msg_type;
        int msg_count;
    };
    
    std::vector<TopicInfo> image_topics_;
    std::map<std::string, std::string> topic_directories_;
    std::map<std::string, int> extraction_counts_;
    
    bool convertImagesToVideo(const std::string& images_dir, const std::string& output_video_path) {
        std::cout << "🎬 Converting images to H264 video..." << std::endl;
        std::cout << "  Input: " << images_dir << std::endl;
        std::cout << "  Output: " << output_video_path << std::endl;

        // First, create a raw H264 stream without container
        std::string h264_raw_path = output_video_path + ".h264";

        // ffmpeg command to convert images to raw H264 stream
        std::ostringstream cmd;
        cmd << "ffmpeg -y "  // -y to overwrite output file
            << "-framerate 30 "  // Input framerate
            << "-pattern_type glob "  // Use glob pattern
            << "-i '" << images_dir << "/*.jpg' "  // Input pattern
            << "-vf 'scale=trunc(iw/2)*2:trunc(ih/2)*2' "  // Ensure even dimensions
            << "-c:v libx264 "  // H264 codec
            << "-pix_fmt yuv420p "  // Pixel format
            << "-r 30 "  // Output framerate
            << "-bsf:v h264_mp4toannexb "  // Convert to Annex B format
            << "-f h264 "  // Raw H264 output
            << "'" << h264_raw_path << "'";

        std::cout << "Running: " << cmd.str() << std::endl;

        int result = system(cmd.str().c_str());

        if (result == 0) {
            std::cout << "✅ H264 stream creation successful: " << h264_raw_path << std::endl;

            // Now inject timestamps into the H264 stream
            std::string h264_timestamped_path = output_video_path + ".timestamped.h264";
            std::cout << "💉 Injecting timestamps into H264 stream..." << std::endl;

            std::ostringstream inject_cmd;
            inject_cmd << "./h264_timestamp_injector "
                      << "'" << h264_raw_path << "' "
                      << "'" << h264_timestamped_path << "' "
                      << "'" << images_dir << "'";

            int inject_result = system(inject_cmd.str().c_str());

            if (inject_result == 0) {
                std::cout << "✅ Timestamp injection successful: " << h264_timestamped_path << std::endl;

                // Package the timestamped H264 stream into MP4 container
                std::ostringstream package_cmd;
                package_cmd << "ffmpeg -y "
                           << "-f h264 "
                           << "-i '" << h264_timestamped_path << "' "
                           << "-c:v copy "
                           << "'" << output_video_path << "'";

                int package_result = system(package_cmd.str().c_str());

                if (package_result == 0) {
                    std::cout << "✅ Final MP4 packaging successful: " << output_video_path << std::endl;

                    // Generate H264 files for streaming
                    std::string h264_output_dir = "h264/" + timestamp_ + "/" + boost::filesystem::path(images_dir).filename().string() + "_30fps";
                    if (generateH264FilesForStreaming(h264_timestamped_path, h264_output_dir)) {
                        std::cout << "✅ H264 streaming files generated: " << h264_output_dir << std::endl;
                    } else {
                        std::cout << "⚠️  H264 streaming file generation failed" << std::endl;
                    }

                    // Clean up intermediate files
                    std::remove(h264_raw_path.c_str());
                    std::remove(h264_timestamped_path.c_str());
                } else {
                    std::cout << "⚠️  MP4 packaging failed, keeping raw H264 files" << std::endl;
                }
            } else {
                std::cout << "⚠️  Timestamp injection failed, creating standard MP4 without timestamps" << std::endl;

                // Fall back to creating MP4 without timestamps
                std::ostringstream fallback_cmd;
                fallback_cmd << "ffmpeg -y "
                            << "-f h264 "
                            << "-i '" << h264_raw_path << "' "
                            << "-c:v copy "
                            << "'" << output_video_path << "'";
                int fallback_result = system(fallback_cmd.str().c_str());

                if (fallback_result == 0) {
                    // Still generate H264 files for streaming (without timestamps)
                    std::string h264_output_dir = "h264/" + timestamp_ + "/" + boost::filesystem::path(images_dir).filename().string() + "_30fps";
                    if (generateH264FilesForStreaming(h264_raw_path, h264_output_dir)) {
                        std::cout << "✅ H264 streaming files generated (without timestamps): " << h264_output_dir << std::endl;
                    }
                }

                std::remove(h264_raw_path.c_str());
            }

            return true;
        } else {
            std::cout << "❌ Video conversion failed (exit code: " << result << ")" << std::endl;
            return false;
        }
    }

    // Helper function to replace filesystem functionality
    bool file_exists(const std::string& path) {
        struct stat buffer;
        return (stat(path.c_str(), &buffer) == 0);
    }

    void create_directories(const std::string& path) {
        boost::filesystem::create_directories(path);
    }

    bool generateH264FilesForStreaming(const std::string& timestamped_h264_path, const std::string& output_dir) {
        std::cout << "🎬 Generating H264 files for streaming..." << std::endl;
        std::cout << "  Input: " << timestamped_h264_path << std::endl;
        std::cout << "  Output: " << output_dir << std::endl;

        // Create output directory
        create_directories(output_dir);

        // Use the existing generate_h264.py script from the workspace directory
        std::ostringstream cmd;
        cmd << "python3 /workspace/generate_h264.py "
            << "-i '" << timestamped_h264_path << "' "
            << "-o '" << output_dir << "/'";

        std::cout << "Running: " << cmd.str() << std::endl;

        int result = system(cmd.str().c_str());

        if (result == 0) {
            std::cout << "✅ H264 streaming files generated successfully" << std::endl;
            return true;
        } else {
            std::cout << "❌ H264 streaming file generation failed (exit code: " << result << ")" << std::endl;
            return false;
        }
    }

public:
    BagProcessor(const std::string& bag_path, const std::string& output_dir = "extracted_images", const std::string& timestamp = "")
        : bag_path_(bag_path), output_dir_(output_dir), timestamp_(timestamp) {}

    bool analyzeBag() {
        std::cout << "=== ANALYZING BAG FILE ===" << std::endl;
        std::cout << "Bag file: " << bag_path_ << std::endl;
        std::cout << "==============================" << std::endl;

        try {
            rosbag::Bag bag;
            bag.open(bag_path_, rosbag::bagmode::Read);

            // Get bag info
            rosbag::View view(bag);
            
            // Count total messages and get duration
            int total_messages = 0;
            ros::Time start_time = ros::TIME_MAX;
            ros::Time end_time = ros::TIME_MIN;
            
            std::map<std::string, int> topic_counts;
            std::map<std::string, std::string> topic_types;

            // First pass: collect metadata
            for (const rosbag::MessageInstance& msg : view) {
                total_messages++;
                
                if (msg.getTime() < start_time) start_time = msg.getTime();
                if (msg.getTime() > end_time) end_time = msg.getTime();
                
                std::string topic = msg.getTopic();
                topic_counts[topic]++;
                topic_types[topic] = msg.getDataType();
            }

            double duration = (end_time - start_time).toSec();
            
            for (const auto& topic_pair : topic_counts) {
                const std::string& topic_name = topic_pair.first;
                int count = topic_pair.second;
                const std::string& msg_type = topic_types[topic_name];
                
                // Check if this is an image topic
                if (msg_type.find("Image") != std::string::npos || 
                    topic_name.find("image") != std::string::npos) {
                    
                    TopicInfo info;
                    info.topic_name = topic_name;
                    info.msg_type = msg_type;
                    info.msg_count = count;
                    image_topics_.push_back(info);
                }
            }

            // Display found image topics
            if (!image_topics_.empty()) {
                std::cout << "Found " << image_topics_.size() << " image topics:" << std::endl;
                // for (const auto& topic : image_topics_) {
                //     std::cout << "  - " << topic.topic_name << ": " << topic.msg_count << " images" << std::endl;
                // }
            } else {
                std::cout << "No image topics found!" << std::endl;
                bag.close();
                return false;
            }

            bag.close();
            std::cout << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Error analyzing bag file: " << e.what() << std::endl;
            return false;
        }
    }

    bool createOutputDirectories() {
        try {
            // Create main output directory
            create_directories(output_dir_);
            
            // Create directories for each image topic
            for (const auto& topic : image_topics_) {
                // Clean topic name for directory (replace / with _)
                std::string dir_name = topic.topic_name;
                std::replace(dir_name.begin(), dir_name.end(), '/', '_');
                std::replace(dir_name.begin(), dir_name.end(), ':', '_');
                
                // Remove leading/trailing underscores
                if (!dir_name.empty() && dir_name[0] == '_') {
                    dir_name = dir_name.substr(1);
                }
                
                std::string topic_dir = output_dir_ + "/" + dir_name;
                create_directories(topic_dir);
                
                topic_directories_[topic.topic_name] = topic_dir;
                extraction_counts_[topic.topic_name] = 0;
            }
            
            std::cout << std::endl;
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Error creating directories: " << e.what() << std::endl;
            return false;
        }
    }

    bool extractImages() {
        try {
            rosbag::Bag bag;
            bag.open(bag_path_, rosbag::bagmode::Read);

            // Create view for image topics only
            std::vector<std::string> image_topic_names;
            for (const auto& topic : image_topics_) {
                image_topic_names.push_back(topic.topic_name);
            }
            
            rosbag::View view(bag, rosbag::TopicQuery(image_topic_names));
            
            int processed_messages = 0;
            std::map<std::string, int> success_counts;
            std::map<std::string, int> attempt_counts;
            
            // Initialize counters
            for (const auto& topic : image_topics_) {
                success_counts[topic.topic_name] = 0;
                attempt_counts[topic.topic_name] = 0;
            }

            for (const rosbag::MessageInstance& msg : view) {
                std::string topic_name = msg.getTopic();
                attempt_counts[topic_name]++;
                processed_messages++;

                try {
                    // Convert ROS message to sensor_msgs::Image
                    sensor_msgs::ImageConstPtr image_msg = msg.instantiate<sensor_msgs::Image>();
                    
                    if (image_msg) {
                        // Convert to OpenCV image using cv_bridge
                        cv_bridge::CvImagePtr cv_ptr;
                        
                        try {
                            // Try to convert the image
                            if (image_msg->encoding == "bgr8" || image_msg->encoding == "rgb8") {
                                cv_ptr = cv_bridge::toCvCopy(image_msg, "bgr8");
                            } else if (image_msg->encoding == "mono8") {
                                cv_ptr = cv_bridge::toCvCopy(image_msg, "mono8");
                            } else if (image_msg->encoding == "mono16") {
                                cv_ptr = cv_bridge::toCvCopy(image_msg, "mono16");
                                // Convert 16-bit to 8-bit
                                cv_ptr->image.convertTo(cv_ptr->image, CV_8UC1, 1.0/256.0);
                            } else {
                                // Try default conversion
                                cv_ptr = cv_bridge::toCvCopy(image_msg, "bgr8");
                            }
                        } catch (cv_bridge::Exception& e) {
                            // If conversion fails, try with original encoding
                            cv_ptr = cv_bridge::toCvCopy(image_msg);
                        }

                        if (cv_ptr && !cv_ptr->image.empty()) {
                            // Generate filename with timestamp
                            double timestamp = msg.getTime().toSec();
                            
                            std::ostringstream filename_stream;
                            filename_stream << "image_" 
                                          << std::setfill('0') << std::setw(4) << success_counts[topic_name]
                                          << "_" << std::fixed << std::setprecision(3) << timestamp
                                          << ".jpg";
                            
                            std::string filepath = topic_directories_[topic_name] + "/" + filename_stream.str();
                            
                            // Save image
                            if (cv::imwrite(filepath, cv_ptr->image)) {
                                success_counts[topic_name]++;
                                
                                // // Progress update every 50 images
                                // if (success_counts[topic_name] % 50 == 0) {
                                //     std::cout << "  " << topic_name << ": saved " 
                                //              << success_counts[topic_name] << " images" << std::endl;
                                // }
                            } else {
                                std::cerr << "Failed to save image: " << filepath << std::endl;
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    if (attempt_counts[topic_name] <= 5) {  // Only show first few errors
                        std::cerr << "Error processing image " << attempt_counts[topic_name] 
                                 << " from " << topic_name << ": " << e.what() << std::endl;
                    }
                }
            }

            bag.close();

            // Print final results
            std::cout << std::endl << "Extraction completed:" << std::endl;
            std::cout << "--------------------------------------------------" << std::endl;
            
            int total_attempted = 0;
            int total_extracted = 0;
            
            for (const auto& topic : image_topics_) {
                int attempted = attempt_counts[topic.topic_name];
                int extracted = success_counts[topic.topic_name];
                double success_rate = attempted > 0 ? (double(extracted) / attempted * 100.0) : 0.0;
                
                total_attempted += attempted;
                total_extracted += extracted;
                
                std::cout << topic.topic_name << ":" << std::endl;
                std::cout << "  Attempted: " << attempted << std::endl;
                std::cout << "  Successful: " << extracted << std::endl;
                std::cout << "  Success rate: " << std::fixed << std::setprecision(1) 
                         << success_rate << "%" << std::endl;
            }
            
            double overall_success = total_attempted > 0 ? (double(total_extracted) / total_attempted * 100.0) : 0.0;
            std::cout << std::endl << "Overall Results:" << std::endl;
            std::cout << "  Total attempted: " << total_attempted << std::endl;
            std::cout << "  Total extracted: " << total_extracted << std::endl;
            std::cout << "  Overall success rate: " << std::fixed << std::setprecision(1) 
                     << overall_success << "%" << std::endl;

            return total_extracted > 0;

        } catch (const std::exception& e) {
            std::cerr << "Error extracting images: " << e.what() << std::endl;
            return false;
        }
    }

    bool process() {
        std::cout << "Starting bag file processing..." << std::endl;
        std::cout << "Bag file: " << bag_path_ << std::endl;
        std::cout << "Output directory: " << output_dir_ << std::endl << std::endl;

        // Step 1: Analyze bag file
        if (!analyzeBag()) {
            std::cerr << "Failed to analyze bag file" << std::endl;
            return false;
        }

        // Step 2: Create output directories
        if (!createOutputDirectories()) {
            std::cerr << "Failed to create output directories" << std::endl;
            return false;
        }

        // Step 3: Extract images
        if (!extractImages()) {
            std::cerr << "Failed to extract images" << std::endl;
            return false;
        }

        // Step 4: Convert images to videos
        std::cout << std::endl << "=== CONVERTING IMAGES TO VIDEOS ===" << std::endl;
        
        bool all_conversions_success = true;
        for (const auto& topic_dir_pair : topic_directories_) {
            const std::string& topic_name = topic_dir_pair.first;
            const std::string& images_dir = topic_dir_pair.second;
            
            // Generate output video filename based on directory name
            std::string dir_name = boost::filesystem::path(images_dir).filename().string();
            std::string video_filename = dir_name + "_30fps.mp4";
            std::string output_video_path = output_dir_ + "/" + video_filename;
            
            std::cout << std::endl << "Converting topic: " << topic_name << std::endl;
            
            if (!convertImagesToVideo(images_dir, output_video_path)) {
                std::cout << "⚠️  Video conversion failed for " << topic_name << std::endl;
                all_conversions_success = false;
            }
        }

        std::cout << std::endl << "✅ Bag processing completed successfully!" << std::endl;
        std::cout << "Images extracted to: " << output_dir_ << std::endl;
        
        if (all_conversions_success) {
            std::cout << "✅ All videos converted successfully!" << std::endl;
        } else {
            std::cout << "⚠️  Some video conversions failed" << std::endl;
        }
        
        return true;
    }
};

int main(int argc, char** argv) {
    // Initialize ROS (required for rosbag)
    ros::init(argc, argv, "bag_processor");

    std::string bag_file;
    std::string timestamp = generate_timestamp();
    std::string output_dir = "output/extracted_images_" + timestamp;

    // Auto-find bag file in /workspace/jetson/ directory
    boost::filesystem::path jetson_dir("/workspace/jetson");
    bool found = false;
    
    try {
        if (boost::filesystem::exists(jetson_dir) && boost::filesystem::is_directory(jetson_dir)) {
            for (auto& file : boost::filesystem::directory_iterator(jetson_dir)) {
                if (file.path().extension() == ".bag") {
                    bag_file = file.path().string();
                    found = true;
                    std::cout << "🔍 Found bag file: " << bag_file << std::endl;
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error searching for bag files: " << e.what() << std::endl;
    }

    if (!found) {
        std::cerr << "❌ Error: No .bag file found in /workspace/jetson/" << std::endl;
        std::cerr << "Available files:" << std::endl;
        try {
            for (auto& file : boost::filesystem::directory_iterator(jetson_dir)) {
                std::cerr << "  " << file.path().filename() << std::endl;
            }
        } catch (...) {
            std::cerr << "Could not list directory contents" << std::endl;
        }
        return 1;
    }

    // Create and run bag processor
    BagProcessor processor(bag_file, output_dir, timestamp);
    
    if (!processor.process()) {
        std::cerr << "Bag processing failed!" << std::endl;
        return 1;
    }

    return 0;
}