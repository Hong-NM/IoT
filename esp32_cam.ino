#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h>      // Thêm thư viện WiFiManager
#include <Preferences.h>     // Thêm thư viện Preferences để lưu cấu hình
#include "esp_camera.h"      // Thư viện Camera

// ============== CẤU HÌNH CAMERA (AI THINKER) ================
// Đảm bảo bạn đã chọn đúng board "AI Thinker ESP32-CAM" trong Arduino IDE
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1 // -1 nếu không dùng chân reset
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26 // SDA
#define SIOC_GPIO_NUM     27 // SCL

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
// ============================================================

// ============== CẤU HÌNH LƯU TRỮ & MẶC ĐỊNH ===============
Preferences preferences;         // Đối tượng để lưu/đọc cấu hình
const char* CONFIG_NAMESPACE = "app-config"; // Tên namespace trong Preferences
const char* FREQ_KEY = "sendFreq"; // Key để lưu tần suất gửi (giây)

// Giá trị mặc định nếu chưa có cấu hình
const char* DEFAULT_FREQUENCY_SECONDS = "60"; // Mặc định gửi mỗi 60 giây
unsigned long sendIntervalMillis = 60000; // Biến lưu tần suất gửi thực tế (ms)
// ============================================================

// ============== CẤU HÌNH SERVER (Có thể cấu hình qua Portal sau) ================
// Bạn nên xem xét việc đưa các thông tin này vào cấu hình qua WiFiManager luôn
const char* server = "192.168.136.127"; // IP máy chạy server Node.js
const int port = 3000;                  // Port của server
const char* uploadPath = "/upload";         // Endpoint nhận ảnh
const char* boundary = "----ESP32CamBoundary"; // Boundary cho multipart form data
// ============================================================

// ============== BIẾN TOÀN CỤC CHO WIFI MANAGER ===============
WiFiManager wm;                          // Đối tượng WiFiManager
bool shouldSaveConfig = false;           // Cờ báo hiệu cần lưu cấu hình mới
char customFrequency[6];                 // Buffer lưu giá trị tần suất nhập từ portal (VD: "120")
// ============================================================

// Khai báo trước các hàm (forward declarations)
void saveConfigCallback();
void captureAndSend();
bool initCamera();
bool loadConfig();
void saveConfig();

// ========================== SETUP ===========================
void setup() {
    Serial.begin(115200);
    Serial.println("\n\nKhởi động ESP32-CAM - Dự đoán sâu bệnh");

    // --- 1. Tải cấu hình đã lưu ---
    if (!loadConfig()) {
        Serial.println("Không tải được cấu hình, sử dụng giá trị mặc định.");
        sendIntervalMillis = atoi(DEFAULT_FREQUENCY_SECONDS) * 1000;
        strncpy(customFrequency, DEFAULT_FREQUENCY_SECONDS, sizeof(customFrequency) - 1);
        customFrequency[sizeof(customFrequency) - 1] = '\0'; // Đảm bảo null-terminated
    } else {
        Serial.printf("Đã tải cấu hình: Tần suất gửi = %lu ms\n", sendIntervalMillis);
        // Cập nhật buffer customFrequency để hiển thị giá trị hiện tại trong portal
        snprintf(customFrequency, sizeof(customFrequency), "%lu", sendIntervalMillis / 1000);
    }

    // --- 2. Cấu hình WiFi Manager ---
    // Đặt callback sẽ được gọi khi người dùng nhấn Save trong portal
    wm.setSaveConfigCallback(saveConfigCallback);

    // Thêm trường nhập liệu tùy chỉnh cho tần suất gửi (giây)
    WiFiManagerParameter custom_frequency_param("frequency", "Gửi mỗi (giây)", customFrequency, 5); // id, label, default value, length
    wm.addParameter(&custom_frequency_param);

    // Tùy chỉnh giao diện portal (tùy chọn)
    wm.setClass("invert"); // Giao diện tối
    wm.setConfigPortalTimeout(180); // Thời gian chờ portal (giây), sau đó sẽ tiếp tục hoặc khởi động lại

    // --- 3. Kết nối WiFi hoặc Mở Portal Cấu hình ---
    Serial.println("Đang kết nối WiFi...");
    // Thử kết nối vào mạng WiFi đã lưu. Nếu thất bại, mở Access Point tên "ESP32-CAM-Config"
    if (!wm.autoConnect("ESP32-CAM-Config")) { // Tên Access Point nếu cần cấu hình
        Serial.println("Kết nối thất bại và hết thời gian chờ portal. Khởi động lại...");
        delay(3000);
        ESP.restart(); // Khởi động lại nếu không kết nối được
    } else {
        // Kết nối thành công!
        Serial.println("\n✅ WiFi đã kết nối!");
        Serial.print("   Địa chỉ IP: ");
        Serial.println(WiFi.localIP());

        // Kiểm tra xem người dùng có nhấn Save trong portal không
        if (shouldSaveConfig) {
            // Lấy giá trị tần suất mới từ parameter object
            strcpy(customFrequency, custom_frequency_param.getValue());
            Serial.printf("   Tần suất mới từ portal: %s giây\n", customFrequency);

            // Chuyển đổi và kiểm tra giá trị
            unsigned long newIntervalSeconds = atol(customFrequency);
            if (newIntervalSeconds > 0 && newIntervalSeconds < (0xFFFFFFFF / 1000)) { // Kiểm tra hợp lệ
                 sendIntervalMillis = newIntervalSeconds * 1000;
                 saveConfig(); // Lưu giá trị mới vào Preferences
            } else {
                Serial.println("   Tần suất nhập không hợp lệ, giữ nguyên giá trị cũ.");
            }
            shouldSaveConfig = false; // Reset cờ
        }
    }

    // --- 4. Khởi tạo Camera ---
    // Nên khởi tạo camera SAU KHI đã kết nối WiFi để tránh xung đột tài nguyên nếu có
    if (!initCamera()) {
        Serial.println("❌ Khởi tạo Camera thất bại! Khởi động lại...");
        delay(5000);
        ESP.restart();
    }
    Serial.println("📷 Camera đã sẵn sàng.");
}
// ======================== END SETUP =========================


// =========================== LOOP ===========================
void loop() {
    // Kiểm tra kết nối WiFi trước khi làm việc
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Chuẩn bị chụp và gửi ảnh... (Lần tiếp theo sau %lu giây)\n", sendIntervalMillis / 1000);
        captureAndSend(); // Gọi hàm chụp và gửi ảnh
    } else {
        Serial.println("⚠️ Mất kết nối WiFi! Đang thử kết nối lại...");
        // WiFiManager thường tự động xử lý kết nối lại trong nền.
        // Nếu muốn ép kết nối lại ngay lập tức (có thể bị block):
        // wm.autoConnect("ESP32-CAM-Config");
        delay(5000); // Chờ một chút trước khi kiểm tra lại
    }

    // Chờ khoảng thời gian đã cấu hình trước khi lặp lại
    Serial.printf("Chờ %lu ms...\n", sendIntervalMillis);
    delay(sendIntervalMillis);
}
// ========================= END LOOP =========================


// ==================== CÁC HÀM PHỤ TRỢ =====================

/**
 * @brief Callback được gọi khi người dùng nhấn nút Save trong WiFiManager portal.
 * Chỉ đặt cờ `shouldSaveConfig` thành true. Việc đọc và lưu giá trị
 * sẽ được thực hiện trong setup() sau khi kết nối thành công.
 */
void saveConfigCallback() {
    Serial.println("WiFiManager: Yêu cầu lưu cấu hình.");
    shouldSaveConfig = true;
}

/**
 * @brief Tải cấu hình (tần suất gửi) từ bộ nhớ Preferences.
 * @return true nếu tải thành công, false nếu có lỗi hoặc chưa có cấu hình.
 */
bool loadConfig() {
    if (!preferences.begin(CONFIG_NAMESPACE, true)) { // Mở namespace ở chế độ chỉ đọc (true)
         Serial.println("Lỗi mở Preferences để đọc.");
         return false;
    }
    // Đọc giá trị tần suất. Nếu key không tồn tại, trả về 0.
    unsigned long loadedIntervalSec = preferences.getULong(FREQ_KEY, 0);
    preferences.end(); // Đóng Preferences

    if (loadedIntervalSec > 0) { // Nếu đọc được giá trị hợp lệ
        sendIntervalMillis = loadedIntervalSec * 1000;
        return true;
    } else {
        // Nếu giá trị = 0 (hoặc key không tồn tại), dùng mặc định
        Serial.println("Không tìm thấy cấu hình tần suất, sẽ dùng mặc định.");
        return false;
    }
}

/**
 * @brief Lưu cấu hình hiện tại (tần suất gửi) vào bộ nhớ Preferences.
 */
void saveConfig() {
     if (!preferences.begin(CONFIG_NAMESPACE, false)) { // Mở namespace ở chế độ đọc/ghi (false)
         Serial.println("Lỗi mở Preferences để ghi.");
         return;
     }
     // Lưu tần suất (tính bằng giây)
     preferences.putULong(FREQ_KEY, sendIntervalMillis / 1000);
     preferences.end(); // Đóng Preferences
     Serial.printf("Đã lưu cấu hình: Tần suất gửi = %lu giây\n", sendIntervalMillis / 1000);
}

/**
 * @brief Khởi tạo camera với cấu hình đã định nghĩa.
 * @return true nếu khởi tạo thành công, false nếu thất bại.
 */
bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM; // SCCB SDA = I2C SDA
    config.pin_sccb_scl = SIOC_GPIO_NUM; // SCCB SCL = I2C SCL
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;      // Tần số XCLK 20MHz
    config.pixel_format = PIXFORMAT_JPEG; // Định dạng ảnh JPEG (đã nén)

    // Chọn kích thước ảnh nhỏ hơn để tiết kiệm băng thông
    // FRAMESIZE_QVGA: 320x240
    // FRAMESIZE_CIF:  400x296
    // FRAMESIZE_VGA:  640x480
    // FRAMESIZE_SVGA: 800x600
    // FRAMESIZE_XGA: 1024x768
    // FRAMESIZE_SXGA: 1280x1024
    // FRAMESIZE_UXGA: 1600x1200
    config.frame_size = FRAMESIZE_QVGA;  // <-- THAY ĐỔI KÍCH THƯỚC Ở ĐÂY

    config.jpeg_quality = 12; // Chất lượng JPEG (0-63), số nhỏ hơn = chất lượng cao hơn (file lớn hơn)
    config.fb_count = 1;      // Chỉ dùng 1 frame buffer khi chụp ảnh tĩnh
    // Nếu gặp lỗi Brownout (sụt áp), thử tăng fb_count lên 2 và đảm bảo nguồn cấp đủ mạnh
    // config.fb_location = CAMERA_FB_IN_PSRAM; // Sử dụng PSRAM nếu có
    // config.grab_mode = CAMERA_GRAB_LATEST;   // Chỉ lấy frame mới nhất

    // Khởi tạo camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Lỗi khởi tạo camera: 0x%x\n", err);
        return false;
    }

    // Tùy chỉnh thêm cho camera (tùy chọn)
    // sensor_t * s = esp_camera_sensor_get();
    // s->set_brightness(s, 0);     // -2 to 2
    // s->set_contrast(s, 0);       // -2 to 2
    // s->set_saturation(s, 0);     // -2 to 2
    // s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect)
    // s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
    // s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    // s->set_wb_mode(s, 0);        // 0 to 4 - if awb_gain enabled (0 - Auto)
    // s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
    // s->set_aec2(s, 0);           // 0 = disable , 1 = enable
    // s->set_ae_level(s, 0);       // -2 to 2
    // s->set_aec_value(s, 300);    // 0 to 1200
    // s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
    // s->set_agc_gain(s, 0);       // 0 to 30
    // s->set_gainceiling(s, (gainceiling_t)0); // 0 to 6
    // s->set_bpc(s, 0);            // 0 = disable , 1 = enable
    // s->set_wpc(s, 1);            // 0 = disable , 1 = enable
    // s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
    // s->set_lenc(s, 1);           // 0 = disable , 1 = enable
    // s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
    // s->set_vflip(s, 0);          // 0 = disable , 1 = enable
    // s->set_dcw(s, 1);            // 0 = disable , 1 = enable
    // s->set_colorbar(s, 0);       // 0 = disable , 1 = enable

    return true;
}

/**
 * @brief Chụp ảnh từ camera và gửi lên server qua HTTP POST multipart/form-data.
 */
void captureAndSend() {
    camera_fb_t * fb = NULL; // Frame buffer pointer

    // --- 1. Chụp ảnh ---
    fb = esp_camera_fb_get(); // Lấy frame buffer từ camera
    if (!fb) {
        Serial.println("❌ Chụp ảnh thất bại (không lấy được frame buffer)");
        // Có thể thử khởi tạo lại camera ở đây nếu lỗi thường xuyên
        // esp_camera_deinit(); initCamera();
        delay(1000); // Chờ trước khi thử lại ở vòng lặp sau
        return;
    }
    Serial.printf("✅ Chụp ảnh thành công: Kích thước = %zu bytes, Định dạng = %d\n", fb->len, fb->format);

    // --- 2. Kết nối tới Server ---
    WiFiClient client; // Tạo đối tượng client
    Serial.printf("Đang kết nối tới server %s:%d...\n", server, port);
    if (!client.connect(server, port)) {
        Serial.println("❌ Kết nối đến server thất bại.");
        esp_camera_fb_return(fb); // **Quan trọng:** Trả lại buffer trước khi thoát
        return;                   // Thoát khỏi hàm, chờ lần gửi sau
    }
    Serial.println("✅ Kết nối server thành công. Đang gửi ảnh...");

    // --- 3. Tạo và Gửi HTTP Request ---
    // Tạo phần header của multipart
    String head = "--" + String(boundary) + "\r\n";
    // Đặt tên file ảnh độc nhất bằng cách dùng timestamp (millis)
    head += "Content-Disposition: form-data; name=\"image\"; filename=\"esp32_" + String(millis()) + ".jpg\"\r\n";
    head += "Content-Type: image/jpeg\r\n\r\n"; // Kiểu dữ liệu là ảnh JPEG

    // Tạo phần đuôi của multipart
    String tail = "\r\n--" + String(boundary) + "--\r\n";

    // Tính tổng chiều dài nội dung request
    size_t imageLen = fb->len;
    size_t totalLen = head.length() + imageLen + tail.length();

    // Gửi các dòng header của HTTP POST request
    client.println(String("POST ") + uploadPath + " HTTP/1.1"); // Dòng request POST
    client.println(String("Host: ") + server);                 // Host server
    // Kiểu nội dung là multipart, chỉ định boundary
    client.println("Content-Type: multipart/form-data; boundary=" + String(boundary));
    client.println("Content-Length: " + String(totalLen));     // Tổng chiều dài nội dung
    client.println("Connection: close");                       // Đóng kết nối sau khi xong
    client.println();                                          // Dòng trống kết thúc phần header

    // Gửi phần header của multipart
    client.print(head);

    // Gửi dữ liệu ảnh (quan trọng: gửi trực tiếp từ buffer)
    client.write(fb->buf, imageLen);

    // Gửi phần đuôi của multipart
    client.print(tail);

    // --- 4. Trả lại Frame Buffer ---
    // **Quan trọng:** Trả lại buffer ngay sau khi gửi xong dữ liệu,
    // không cần chờ phản hồi server để giải phóng bộ nhớ sớm nhất có thể.
    esp_camera_fb_return(fb);
    fb = NULL; // Đánh dấu buffer đã được trả lại

    Serial.println("✅ Dữ liệu ảnh đã gửi. Chờ phản hồi từ server...");

    // --- 5. Đọc Phản Hồi Từ Server ---
    unsigned long timeout = millis();
    bool headerEnded = false;
    while (client.connected() || client.available()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim(); // Xóa khoảng trắng thừa
            // Nếu là dòng trống sau header thì đánh dấu
            if (line.length() == 0 && !headerEnded) {
                 headerEnded = true;
                 Serial.println("-- Hết Header Phản Hồi --");
            }
            // Chỉ in nội dung sau header (nếu có) hoặc toàn bộ nếu không quan tâm tách bạch
             Serial.print("   << "); // Ký hiệu dòng phản hồi
             Serial.println(line);

            timeout = millis(); // Reset timeout khi có dữ liệu
        }
        // Kiểm tra timeout (ví dụ: 5 giây không nhận được gì)
        if (millis() - timeout > 5000) {
            Serial.println("⚠️ Timeout khi chờ phản hồi từ server!");
            break;
        }
        delay(10); // Chờ một chút để tránh CPU load cao
    }

    // --- 6. Đóng Kết Nối ---
    client.stop();
    Serial.println("Kết nối đã đóng.");
}

// ================= END CÁC HÀM PHỤ TRỢ =====================