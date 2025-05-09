const express = require("express");
const multer = require("multer");
const fs = require("fs");
const path = require("path");

const app = express();
const port = 3000;

// === Tạo thư mục uploads nếu chưa có ===
const uploadFolder = path.join(__dirname, "uploads");
if (!fs.existsSync(uploadFolder)) {
  console.log("📁 Tạo thư mục uploads...");
  fs.mkdirSync(uploadFolder, { recursive: true });
}

// === Cấu hình Multer để lưu ảnh ===
const storage = multer.diskStorage({
  destination: (req, file, cb) => cb(null, uploadFolder),
  filename: (req, file, cb) => cb(null, Date.now() + ".jpg"),
});

const upload = multer({
  storage: storage,
  limits: { fileSize: 5 * 1024 * 1024 }, // Tối đa 5MB
}).single("image"); // Trường file: image

// === Cho phép truy cập thư mục /uploads qua web ===
app.use("/uploads", express.static(uploadFolder));

// === Route gốc ===
app.get("/", (req, res) => {
  res.send(
    "✅ Server đang chạy! Gửi ảnh lên /upload hoặc xem ảnh tại /gallery"
  );
});

// === Endpoint nhận ảnh từ ESP32 ===
app.post("/upload", upload, (req, res, next) => {
  if (req.file) {
    console.log("📸 Ảnh nhận được:", req.file.filename);
    res.sendStatus(200);
  } else {
    console.error("❌ Không có ảnh trong yêu cầu");
    res.status(400).send("Không có file ảnh trong yêu cầu");
  }
});

// === Giao diện xem ảnh ===
app.get("/gallery", (req, res) => {
  fs.readdir(uploadFolder, (err, files) => {
    if (err) {
      console.error("❌ Không thể đọc thư mục ảnh:", err);
      return res.status(500).send("Lỗi đọc ảnh");
    }

    const images = files
      .filter((file) => /\.(jpg|jpeg|png)$/i.test(file))
      .sort(
        (a, b) =>
          fs.statSync(path.join(uploadFolder, b)).mtime -
          fs.statSync(path.join(uploadFolder, a)).mtime
      );

    const html = `
      <html>
        <head>
          <title>📷 Gallery ảnh ESP32</title>
          <meta http-equiv="refresh" content="10">
        </head>
        <body>
          <h2>📸 Ảnh đã nhận:</h2>
          <div style="display:flex;flex-wrap:wrap;gap:10px;">
            ${images
              .map(
                (file) => `
                <div style="border:1px solid #ccc;padding:10px">
                  <img src="/uploads/${file}" width="320"/><br/>
                  <small>${file}</small>
                </div>`
              )
              .join("")}
          </div>
        </body>
      </html>
    `;
    res.send(html);
  });
});

// === Khởi động server ===
app.listen(port, () => {
  const ip = require("ip").address();
  console.log(`🚀 Server đang chạy tại: http://${ip}:${port}`);
});
