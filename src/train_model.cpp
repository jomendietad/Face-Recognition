#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <map>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    std::string faces_dir = "assets/faces";
    std::string model_path = "assets/lbph_model.yml";
    std::string labels_path = "assets/labels.csv";

    // Permitir rutas personalizadas por argumentos
    if (argc > 1) faces_dir = argv[1];

    std::vector<cv::Mat> images;
    std::vector<int> labels;
    std::map<int, std::string> label_to_name;
    int current_id = 1;

    if (!fs::exists(faces_dir)) {
        std::cerr << "Error: Directory " << faces_dir << " not found." << std::endl;
        return 1;
    }

    std::cout << "--- STARTING TRAINING PROCESS ---" << std::endl;
    std::cout << "Reading faces from: " << faces_dir << std::endl;

    for (const auto & entry : fs::directory_iterator(faces_dir)) {
        if (entry.is_directory()) {
            std::string person_name = entry.path().filename().string();
            label_to_name[current_id] = person_name;
            
            int count = 0;
            for (const auto & img_entry : fs::directory_iterator(entry.path())) {
                std::string ext = img_entry.path().extension().string();
                if (ext == ".jpg" || ext == ".png" || ext == ".jpeg") {
                    cv::Mat img = cv::imread(img_entry.path().string(), cv::IMREAD_GRAYSCALE);
                    if (!img.empty()) {
                        images.push_back(img);
                        labels.push_back(current_id);
                        count++;
                    }
                }
            }
            std::cout << "Loaded " << count << " images for: " << person_name << " (ID: " << current_id << ")" << std::endl;
            current_id++;
        }
    }

    if (images.empty()) {
        std::cerr << "No images found. Add photos to assets/faces/Name/" << std::endl;
        return 1;
    }

    // 1. Entrenar el modelo
    std::cout << "Training LBPH model..." << std::endl;
    cv::Ptr<cv::face::LBPHFaceRecognizer> recognizer = cv::face::LBPHFaceRecognizer::create();
    recognizer->train(images, labels);

    // 2. Guardar el modelo binario
    recognizer->save(model_path);
    std::cout << "Model saved to: " << model_path << std::endl;

    // 3. Guardar el mapa de nombres (ID,Nombre)
    std::ofstream label_file(labels_path);
    if (label_file.is_open()) {
        for (auto const& [id, name] : label_to_name) {
            label_file << id << "," << name << "\n";
        }
        label_file.close();
        std::cout << "Labels saved to: " << labels_path << std::endl;
    } else {
        std::cerr << "Error saving labels file." << std::endl;
    }

    std::cout << "--- TRAINING COMPLETE ---" << std::endl;
    return 0;
}