#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <map>
#include <mutex> // NECESARIO PARA COMPARTIR VIDEO

// Linux System Headers
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <security/pam_appl.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------
// GLOBAL CONFIGURATION
// ---------------------------------------------------------
std::atomic<bool> keep_running{true};
std::string GLOBAL_LOG_PATH; 
std::map<int, std::string> label_to_name;

// --- NUEVO: VIDEO COMPARTIDO ---
cv::Mat global_frame;           // El frame actual para la web
std::mutex global_frame_mutex;  // Candado para proteger la lectura/escritura
std::vector<uchar> global_jpeg; // Buffer JPEG comprimido (optimización)
// ------------------------------

// ---------------------------------------------------------
// HELPER FUNCTIONS
// ---------------------------------------------------------

static std::string iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&tt, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::string base64_decode(const std::string &in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) { out.push_back(char((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}

// ---------------------------------------------------------
// PAM AUTHENTICATION
// ---------------------------------------------------------
struct PamCredentials { std::string user; std::string password; };

int pam_conversation(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr) {
    auto *creds = static_cast<PamCredentials*>(appdata_ptr);
    *resp = (struct pam_response *)calloc(num_msg, sizeof(struct pam_response));
    for (int i = 0; i < num_msg; ++i) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) (*resp)[i].resp = strdup(creds->password.c_str());
        else if (msg[i]->msg_style == PAM_PROMPT_ECHO_ON) (*resp)[i].resp = strdup(creds->user.c_str());
    }
    return PAM_SUCCESS;
}

bool authenticate_system_user(const std::string& username, const std::string& password) {
    PamCredentials creds = {username, password};
    struct pam_conv conv = {pam_conversation, &creds};
    pam_handle_t *pamh = nullptr;
    int retval = pam_start("login", username.c_str(), &conv, &pamh);
    if (retval == PAM_SUCCESS) retval = pam_authenticate(pamh, 0);
    if (retval == PAM_SUCCESS) retval = pam_acct_mgmt(pamh, 0);
    bool success = (retval == PAM_SUCCESS);
    if (pamh) pam_end(pamh, retval);
    return success;
}

// ---------------------------------------------------------
// WEB SERVER CLIENT HANDLER (NUEVO LÓGICA MJPEG)
// ---------------------------------------------------------
void handle_client(int client_socket) {
    char buffer[4096] = {0};
    read(client_socket, buffer, 4096);
    std::string request(buffer);

    // 1. Autenticación
    bool authenticated = false;
    std::string auth_prefix = "Authorization: Basic ";
    size_t auth_pos = request.find(auth_prefix);
    if (auth_pos != std::string::npos) {
        size_t end_pos = request.find("\r\n", auth_pos);
        std::string encoded = request.substr(auth_pos + auth_prefix.length(), end_pos - (auth_pos + auth_prefix.length()));
        std::string decoded = base64_decode(encoded);
        size_t colon = decoded.find(':');
        if (colon != std::string::npos) {
            if (authenticate_system_user(decoded.substr(0, colon), decoded.substr(colon + 1))) authenticated = true;
        }
    }

    if (!authenticated) {
        std::string response = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"RPi Security\"\r\nContent-Length: 0\r\n\r\n";
        send(client_socket, response.c_str(), response.size(), MSG_NOSIGNAL);
        close(client_socket);
        return;
    }

    // 2. Enrutamiento
    std::string response_header;
    
    // RUTA: /video_feed (MJPEG Stream)
    if (request.find("GET /video_feed") != std::string::npos) {
        response_header = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
        send(client_socket, response_header.c_str(), response_header.size(), MSG_NOSIGNAL);

        while (keep_running) {
            std::vector<uchar> current_jpeg;
            
            // Copiar el frame comprimido de forma segura
            {
                std::lock_guard<std::mutex> lock(global_frame_mutex);
                if (global_jpeg.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                current_jpeg = global_jpeg;
            }

            // Enviar Boundary y Headers del frame
            std::string part_header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " + 
                                      std::to_string(current_jpeg.size()) + "\r\n\r\n";
            
            if (send(client_socket, part_header.c_str(), part_header.size(), MSG_NOSIGNAL) < 0) break; // Cliente desconectado
            if (send(client_socket, current_jpeg.data(), current_jpeg.size(), MSG_NOSIGNAL) < 0) break;
            if (send(client_socket, "\r\n", 2, MSG_NOSIGNAL) < 0) break;

            // Limitar FPS del stream para no saturar WiFi (aprox 10fps)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } 
    // RUTA: / (Página Principal)
    else {
        std::ifstream f(GLOBAL_LOG_PATH);
        std::string logs((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        
        std::string html = "<html><head><title>RPi Cam</title><style>"
                           "body{font-family:sans-serif; background:#222; color:#fff; text-align:center;}"
                           "img{border: 2px solid #0f0; max-width:100%;}"
                           "pre{background:#333; padding:10px; text-align:left; max-height:300px; overflow:auto;}"
                           "</style></head><body>"
                           "<h1>🔴 Live Secure Feed</h1>"
                           "<img src='/video_feed' alt='Live Stream' /><br>" // AQUÍ SE CARGA EL VIDEO
                           "<h3>Access Logs</h3>"
                           "<pre>" + logs + "</pre>"
                           "<p><a href='/' style='color:#0f0'>Refresh Logs</a></p>"
                           "</body></html>";

        response_header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + 
                          std::to_string(html.size()) + "\r\n\r\n" + html;
        send(client_socket, response_header.c_str(), response_header.size(), MSG_NOSIGNAL);
    }

    close(client_socket);
}

void web_server_thread() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) return;
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) return;
    if (listen(server_fd, 5) < 0) return; // Cola de espera aumentada

    while (keep_running) {
        int new_socket = accept(server_fd, nullptr, nullptr);
        if (new_socket < 0) continue;
        
        // LANZAR HILO POR CADA CLIENTE (Para que video y web funcionen a la vez)
        std::thread(handle_client, new_socket).detach();
    }
    close(server_fd);
}

// ---------------------------------------------------------
// FRAMEBUFFER INIT
// ---------------------------------------------------------
struct FramebufferInfo {
    int fbfd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    long int screensize;
    char *fbp;
};

FramebufferInfo init_fb() {
    FramebufferInfo fb;
    fb.fbfd = -1; fb.fbp = nullptr;
    fb.fbfd = open("/dev/fb0", O_RDWR);
    if (fb.fbfd == -1) {
        std::cerr << "⚠️  FB not available. Running in WEB-STREAM mode." << std::endl;
        return fb;
    }
    if (ioctl(fb.fbfd, FBIOGET_FSCREENINFO, &fb.finfo) == -1) return fb;
    if (ioctl(fb.fbfd, FBIOGET_VSCREENINFO, &fb.vinfo) == -1) return fb;
    fb.screensize = fb.vinfo.xres * fb.vinfo.yres * fb.vinfo.bits_per_pixel / 8;
    fb.fbp = (char *)mmap(0, fb.screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fbfd, 0);
    if ((intptr_t)fb.fbp == -1) { close(fb.fbfd); fb.fbfd = -1; }
    return fb;
}

// ---------------------------------------------------------
// CARGA DEL MODELO Y NOMBRES
// ---------------------------------------------------------
bool load_resources(cv::Ptr<cv::face::LBPHFaceRecognizer> &model) {
    std::string model_path = "assets/lbph_model.yml";
    std::string labels_path = "assets/labels.csv";

    // 1. Cargar modelo binario
    if (fs::exists(model_path)) {
        try {
            model->read(model_path);
            std::cout << "Model loaded from " << model_path << std::endl;
        } catch (const cv::Exception& e) {
            std::cerr << "Error loading model: " << e.what() << std::endl;
            return false;
        }
    } else {
        std::cerr << "Warning: Model file not found (" << model_path << "). Recognition disabled." << std::endl;
        return false; // Sin modelo no hay reconocimiento, solo detección
    }

    // 2. Cargar nombres (Labels)
    std::ifstream label_file(labels_path);
    if (label_file.is_open()) {
        std::string line;
        while (std::getline(label_file, line)) {
            size_t comma_pos = line.find(',');
            if (comma_pos != std::string::npos) {
                int id = std::stoi(line.substr(0, comma_pos));
                std::string name = line.substr(comma_pos + 1);
                label_to_name[id] = name;
            }
        }
        std::cout << "Labels loaded: " << label_to_name.size() << " people." << std::endl;
    } else {
        std::cerr << "Warning: Labels file not found. IDs will be shown instead of names." << std::endl;
    }
    
    return true;
}

// ---------------------------------------------------------
// MAIN APPLICATION
// ---------------------------------------------------------
int main(int argc, char** argv) {
    std::string cascade_path = "assets/haarcascades/haarcascade_frontalface_default.xml";
    std::string faces_dir = "assets/faces"; 
    std::string log_path = "logs/event_log.csv";
    std::string input_source = "0";

    if (argc > 1) input_source = argv[1];
    GLOBAL_LOG_PATH = log_path;

    cv::VideoCapture cap;
    if (input_source.length() == 1 && isdigit(input_source[0])) cap.open(std::stoi(input_source));
    else cap.open(input_source);

    if (!cap.isOpened()) { std::cerr << "Error input: " << input_source << std::endl; return 1; }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, 15);

    cv::CascadeClassifier face_detector;
    if (!face_detector.load(cascade_path)) { std::cerr << "Error cascade." << std::endl; return 1; }

    cv::Ptr<cv::face::LBPHFaceRecognizer> recognizer = cv::face::LBPHFaceRecognizer::create();
    bool model_loaded = load_resources(recognizer);

    FramebufferInfo fb = init_fb();
    std::thread web_thread(web_server_thread);
    web_thread.detach();

    cv::Mat frame, gray, fb_frame;
    int frame_count = 0;
    
    std::cout << "--- STREAMING SYSTEM RUNNING ---" << std::endl;
    std::cout << "Access: http://<raspberry-ip>:8080" << std::endl;

    while (keep_running) {
        cap >> frame;
        if (frame.empty()) continue;

        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        std::vector<cv::Rect> faces;
        face_detector.detectMultiScale(gray, faces, 1.1, 4, 0, cv::Size(60, 60));

        std::string log_person = "";

        for (const auto& face : faces) {
            int label = -1;
            double confidence = 0.0;
            std::string name = "Desconocido";
            cv::Scalar color = cv::Scalar(0, 0, 255);

            if (model_loaded && !label_to_name.empty()) {
                cv::Mat face_roi = gray(face);
                try {
                    recognizer->predict(face_roi, label, confidence);
                    if (confidence < 90) {
                        name = label_to_name[label];
                        color = cv::Scalar(0, 255, 0);
                    }
                } catch (...) {}
            }

            cv::rectangle(frame, face, color, 2);
            cv::putText(frame, name, cv::Point(face.x, face.y - 10), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255), 2);
            
            if (!log_person.empty()) log_person += "; ";
            log_person += name;
        }

        // --- ACTUALIZAR STREAM WEB (MJPEG) ---
        // Comprimimos el frame a JPEG en memoria para enviarlo rápido
        {
            std::vector<uchar> buf;
            std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 70}; // Calidad 70% para velocidad
            cv::imencode(".jpg", frame, buf, params);

            std::lock_guard<std::mutex> lock(global_frame_mutex);
            global_jpeg = buf; // Actualizar buffer compartido
        }
        // -------------------------------------

        // Output to FB (Opcional)
        if (fb.fbfd != -1 && fb.fbp) {
            cv::resize(frame, fb_frame, cv::Size(fb.vinfo.xres, fb.vinfo.yres));
            if (fb.vinfo.bits_per_pixel == 32) cv::cvtColor(fb_frame, fb_frame, cv::COLOR_BGR2BGRA);
            else if (fb.vinfo.bits_per_pixel == 16) cv::cvtColor(fb_frame, fb_frame, cv::COLOR_BGR2BGR565);
            if (!fb_frame.empty()) memcpy(fb.fbp, fb_frame.data, fb.screensize);
        }

        // Logging
        frame_count++;
        if (frame_count % 30 == 0 && !faces.empty()) {
             std::ofstream log_file(log_path, std::ios::app);
             if (log_file.is_open()) log_file << iso_timestamp() << "," << faces.size() << "," << log_person << std::endl;
        }

        if (cv::waitKey(1) == 27) break;
    }

    keep_running = false;
    cap.release();
    if (fb.fbfd != -1 && fb.fbp) { munmap(fb.fbp, fb.screensize); close(fb.fbfd); }
    return 0;
}