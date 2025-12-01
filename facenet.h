#ifndef FACENET_H
#define FACENET_H

#include <opencv4/opencv2/opencv.hpp>
#include <opencv4/opencv2/dnn.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

class FaceNet {
private:
    cv::dnn::Net net;
    bool is_loaded = false;

    // ---------------------------
    // Chuẩn hóa preprocessing theo InsightFace/ArcFace
    // ---------------------------
    cv::Mat preprocessFaceStandard(const cv::Mat& face_img) {
        if (face_img.empty()) return cv::Mat();

        cv::Mat processed;
        
        // 1. Resize về 112x112 (chuẩn MobileFaceNet)
        cv::resize(face_img, processed, cv::Size(112, 112), 0, 0, cv::INTER_LINEAR);
        
        // 2. Convert sang RGB (quan trọng!)
        cv::cvtColor(processed, processed, cv::COLOR_BGR2RGB);
        
        // 3. Normalize theo chuẩn InsightFace: (img - 127.5) / 128.0
        processed.convertTo(processed, CV_32FC3);
        processed = (processed - 127.5) / 128.0;
        
        return processed;
    }

    // ---------------------------
    // Data Augmentation cho đăng ký
    // ---------------------------
    std::vector<cv::Mat> augmentFace(const cv::Mat& face_img) {
        std::vector<cv::Mat> augmented;
        
        if (face_img.empty()) return augmented;
        
        // Original
        augmented.push_back(face_img.clone());
        
        // 1. Flip ngang (mirror)
        cv::Mat flipped;
        cv::flip(face_img, flipped, 1);
        augmented.push_back(flipped);
        
        // 2. Điều chỉnh độ sáng (+/- 15%)
        cv::Mat brighter, darker;
        face_img.convertTo(brighter, -1, 1.15, 10);  // Sáng hơn
        face_img.convertTo(darker, -1, 0.85, -10);   // Tối hơn
        augmented.push_back(brighter);
        augmented.push_back(darker);
        
        // 3. Xoay nhẹ (+/- 5 độ)
        cv::Point2f center(face_img.cols / 2.0f, face_img.rows / 2.0f);
        
        cv::Mat rot_mat_pos = cv::getRotationMatrix2D(center, 5, 1.0);
        cv::Mat rot_mat_neg = cv::getRotationMatrix2D(center, -5, 1.0);
        
        cv::Mat rotated_pos, rotated_neg;
        cv::warpAffine(face_img, rotated_pos, rot_mat_pos, face_img.size());
        cv::warpAffine(face_img, rotated_neg, rot_mat_neg, face_img.size());
        
        augmented.push_back(rotated_pos);
        augmented.push_back(rotated_neg);
        
        // 4. Gaussian Blur nhẹ (mô phỏng camera mờ)
        cv::Mat blurred;
        cv::GaussianBlur(face_img, blurred, cv::Size(3, 3), 0.8);
        augmented.push_back(blurred);
        
        return augmented;
    }

    // ---------------------------
    // Đánh giá chất lượng ảnh
    // ---------------------------
    float assessFaceQuality(const cv::Mat& face_img) {
        if (face_img.empty() || face_img.cols < 40 || face_img.rows < 40) {
            return 0.0f;
        }

        float score = 0.0f;

        // 1. Kích thước
        float size_score = std::min(1.0f, (face_img.cols * face_img.rows) / 8000.0f);
        score += size_score * 0.25f;

        // 2. Độ sắc nét (Laplacian variance)
        cv::Mat gray, laplacian;
        if (face_img.channels() == 3) {
            cv::cvtColor(face_img, gray, cv::COLOR_BGR2GRAY);
        } else {
            gray = face_img.clone();
        }
        
        cv::Laplacian(gray, laplacian, CV_64F);
        cv::Scalar mean, stddev;
        cv::meanStdDev(laplacian, mean, stddev);
        float sharpness = stddev[0] * stddev[0];
        float sharpness_score = std::min(1.0f, sharpness / 400.0f);
        score += sharpness_score * 0.35f;

        // 3. Độ sáng (không quá tối hoặc quá sáng)
        cv::Scalar avg_brightness = cv::mean(gray);
        float brightness = avg_brightness[0];
        float brightness_score = 1.0f - std::abs(brightness - 127.0f) / 127.0f;
        score += brightness_score * 0.25f;

        // 4. Độ tương phản
        double minVal, maxVal;
        cv::minMaxLoc(gray, &minVal, &maxVal);
        float contrast = (maxVal - minVal) / 255.0f;
        float contrast_score = std::min(1.0f, contrast);
        score += contrast_score * 0.15f;

        return score;
    }

public:
    FaceNet() {}

    // ---------------------------
    // Load Model
    // ---------------------------
    void loadModel(const std::string& modelPath) {
        try {
            net = cv::dnn::readNetFromONNX(modelPath);

            if (net.empty()) {
                std::cerr << "[FaceNet] Model loaded but EMPTY\n";
                is_loaded = false;
                return;
            }

            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

            is_loaded = true;
            std::cout << "[FaceNet] Model loaded: " << modelPath << std::endl;

        } catch (const cv::Exception& e) {
            std::cerr << "[FaceNet] Error: " << e.what() << std::endl;
            is_loaded = false;
        }
    }

    // ---------------------------
    // Lấy Embedding (chuẩn InsightFace)
    // ---------------------------
    cv::Mat getEmbedding(const cv::Mat& face_img) {
        if (!is_loaded || net.empty() || face_img.empty()) {
            return cv::Mat();
        }

        // Preprocessing chuẩn
        cv::Mat processed = preprocessFaceStandard(face_img);
        if (processed.empty()) return cv::Mat();

        // Tạo blob (CHÚ Ý: swapRB = false vì đã convert RGB ở trên)
        cv::Mat blob = cv::dnn::blobFromImage(
            processed,
            1.0,              // scale = 1.0 vì đã normalize rồi
            cv::Size(112, 112),
            cv::Scalar(0, 0, 0), // mean = 0 vì đã trừ 127.5
            false,            // swapRB = false (đã RGB)
            false             // crop
        );

        net.setInput(blob);
        cv::Mat emb = net.forward();
        
        // L2 Normalization (QUAN TRỌNG!)
        cv::Mat emb_normalized;
        double norm = cv::norm(emb, cv::NORM_L2);
        if (norm > 1e-6) {
            emb_normalized = emb / norm;
        } else {
            emb_normalized = emb.clone();
        }
        
        return emb_normalized;
    }

    // ---------------------------
    // Đăng ký chủ nhân với augmentation
    // ---------------------------
    cv::Mat registerOwner(const std::vector<cv::Mat>& face_samples) {
        if (face_samples.empty()) return cv::Mat();

        std::vector<cv::Mat> all_embeddings;
        std::vector<float> quality_scores;

        printf("[FaceNet] Processing %zu samples...\n", face_samples.size());

        for (size_t i = 0; i < face_samples.size(); i++) {
            float quality = assessFaceQuality(face_samples[i]);
            
            // Chỉ lấy mẫu chất lượng cao
            if (quality < 0.4f) {
                printf("[FaceNet] Sample %zu rejected (quality: %.2f)\n", i, quality);
                continue;
            }

            // Augmentation: tạo thêm 7 biến thể
            std::vector<cv::Mat> augmented = augmentFace(face_samples[i]);
            
            for (const auto& aug_face : augmented) {
                cv::Mat emb = getEmbedding(aug_face);
                if (!emb.empty() && emb.total() > 0) {
                    all_embeddings.push_back(emb);
                    quality_scores.push_back(quality);
                }
            }
        }

        if (all_embeddings.empty()) {
            printf("[FaceNet] ERROR: No valid embeddings!\n");
            return cv::Mat();
        }

        printf("[FaceNet] Generated %zu embeddings from %zu samples\n", 
               all_embeddings.size(), face_samples.size());

        // Tính trung bình có trọng số
        cv::Mat avg_embedding = cv::Mat::zeros(all_embeddings[0].size(), CV_32F);
        float total_weight = 0.0f;

        for (size_t i = 0; i < all_embeddings.size(); i++) {
            avg_embedding += all_embeddings[i] * quality_scores[i % quality_scores.size()];
            total_weight += quality_scores[i % quality_scores.size()];
        }

        if (total_weight > 0) {
            avg_embedding /= total_weight;
        }

        // Normalize lại lần cuối
        double norm = cv::norm(avg_embedding, cv::NORM_L2);
        if (norm > 1e-6) {
            avg_embedding = avg_embedding / norm;
        }

        return avg_embedding;
    }

    // ---------------------------
    // Cosine Similarity (không dùng distance)
    // ---------------------------
    float cosineSimilarity(const cv::Mat& v1, const cv::Mat& v2) {
        if (v1.empty() || v2.empty()) return -1.0f;
        if (v1.size() != v2.size()) return -1.0f;

        // Nếu đã normalize, dot product = cosine similarity
        double dot = v1.dot(v2);
        
        // Clamp về [-1, 1]
        dot = std::max(-1.0, std::min(1.0, dot));
        
        return (float)dot;
    }

    // ---------------------------
    // Verify face với threshold rõ ràng
    // ---------------------------
    bool verifyFace(const cv::Mat& test_embedding, 
                    const cv::Mat& reference_embedding,
                    float& similarity,
                    float threshold = 0.9f) {  // Cosine Similarity >= 0.90
        
        similarity = cosineSimilarity(test_embedding, reference_embedding);
        
        // Debug
        printf("[Verify] Similarity: %.4f (Threshold: %.2f)\n", similarity, threshold);
        
        return similarity >= threshold;
    }

    // ---------------------------
    // So sánh 1-vs-1 chi tiết
    // ---------------------------
    struct ComparisonResult {
        float similarity;
        bool is_same_person;
        std::string confidence_level;
    };

    ComparisonResult compareDetailed(const cv::Mat& emb1, const cv::Mat& emb2) {
        ComparisonResult result;
        result.similarity = cosineSimilarity(emb1, emb2);
        
        // Phân loại theo cosine similarity
        if (result.similarity >= 0.60f) {
            result.is_same_person = true;
            result.confidence_level = "VERY HIGH";
        } else if (result.similarity >= 0.50f) {
            result.is_same_person = true;
            result.confidence_level = "HIGH";
        } else if (result.similarity >= 0.40f) {
            result.is_same_person = false;
            result.confidence_level = "UNCERTAIN";
        } else {
            result.is_same_person = false;
            result.confidence_level = "DIFFERENT";
        }
        
        return result;
    }

    bool isLoaded() const { return is_loaded; }
    float checkQuality(const cv::Mat& face_img) { return assessFaceQuality(face_img); }
};

#endif