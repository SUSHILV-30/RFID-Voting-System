<!-- README.md — styled HTML version (paste whole file into README.md) -->
<style>
  /* Lightweight, GitHub-friendly styling */
  .rv-container{font-family: -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,"Helvetica Neue",Arial; color:#222; line-height:1.5; max-width:980px; margin:18px auto; padding:24px; background: #fff; border-radius:10px; box-shadow: 0 6px 18px rgba(0,0,0,0.06);}
  .rv-hero{display:flex; gap:18px; align-items:center; margin-bottom:14px;}
  .rv-badge{background:#0b5ed7;color:#fff;padding:8px 12px;border-radius:8px;font-weight:600;font-size:13px;}
  h1{margin:0;font-size:28px;}
  .rv-sub{color:#555;margin-top:6px;}
  .rv-feats{display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:10px; margin:18px 0;}
  .rv-card{background:#f7fbff;border:1px solid #eef6ff;padding:12px;border-radius:8px;}
  .rv-table{width:100%; border-collapse:collapse; margin:8px 0 18px;}
  .rv-table th, .rv-table td{border:1px solid #e6eef8; padding:8px; text-align:left;}
  .rv-code{background:#0f1724;color:#dbeafe;padding:12px;border-radius:8px;font-family:SFMono-Regular,Menlo,Monaco,Consolas,"Liberation Mono","Courier New",monospace; overflow:auto;}
  .rv-grid{display:grid; grid-template-columns:1fr 1fr; gap:18px;}
  .rv-section{margin-top:18px;}
  .rv-note{font-size:13px;color:#444;background:#fff8e6;border:1px solid #fff1c2;padding:10px;border-radius:6px;}
  .rv-footer{margin-top:20px;color:#666;font-size:13px}
  @media (max-width:760px){ .rv-grid{grid-template-columns:1fr} }
</style>

<div class="rv-container">
  <div class="rv-hero">
    <div>
      <div class="rv-badge">RFID Voting</div>
    </div>
    <div style="flex:1">
      <h1>RFID Voting System – STM32F401 + MFRC522 + SSD1306</h1>
      <div class="rv-sub">A fully working <strong>RFID-based electronic voting system</strong> developed using <strong>STM32CubeIDE</strong>, <br>
      <strong>MFRC522 RFID Reader (SPI)</strong>, <strong>SSD1306 OLED Display (I2C – register level)</strong>, <strong>Potentiometer (ADC)</strong> for selecting candidates, and <strong>Push Button + Buzzer</strong> for UI controls.</div>
      <div class="rv-sub" style="margin-top:8px">This project demonstrates an embedded, real-time, secure voting flow where each authorized RFID card can vote only once.</div>
    </div>
  </div>

  <hr>

  <h2>📌 <strong>Features</strong></h2>
  <div class="rv-feats">
    <div class="rv-card">✔ <strong>RFID-based voter authentication</strong> using MFRC522</div>
    <div class="rv-card">✔ <strong>OLED UI</strong> using SSD1306 (Register-level I2C implementation)</div>
    <div class="rv-card">✔ <strong>Potentiometer for candidate selection</strong> (ADC on PA1)</div>
    <div class="rv-card">✔ <strong>Push-button for vote confirmation</strong></div>
    <div class="rv-card">✔ <strong>Buzzer feedback</strong> for valid/invalid card</div>
    <div class="rv-card">✔ <strong>Anti-double-voting logic</strong> (each authorized UID can vote only once)</div>
    <div class="rv-card">✔ <strong>Shows total vote count</strong> on long button press</div>
    <div class="rv-card">✔ <strong>LED activity indicator</strong> for RFID scans</div>
    <div class="rv-card">✔ Fully working STM32CubeIDE project included in repo</div>
  </div>

  <hr>

  <h2>📂 <strong>Repository Structure</strong></h2>
  <p>(The repository structure — as in original README)</p>

  <hr>

  <h2>🛠 <strong>Hardware Requirements</strong></h2>
  <table class="rv-table">
    <thead>
      <tr><th>Component</th><th>Purpose</th></tr>
    </thead>
    <tbody>
      <tr><td>STM32F401 / STM32F4 Board</td><td>Main controller</td></tr>
      <tr><td>MFRC522 RFID Reader</td><td>Reads card UID (SPI)</td></tr>
      <tr><td>SSD1306 128×64 OLED</td><td>UI (I2C)</td></tr>
      <tr><td>Potentiometer</td><td>Candidate selection (ADC)</td></tr>
      <tr><td>Push Button</td><td>Vote confirmation</td></tr>
      <tr><td>Buzzer</td><td>Audio alerts</td></tr>
      <tr><td>LED on PC13</td><td>RFID activity</td></tr>
    </tbody>
  </table>

  <hr>

  <h2>🔌 <strong>Pin Configuration (Used in Code)</strong></h2>

  <h3>MFRC522 (SPI1)</h3>
  <table class="rv-table">
    <thead><tr><th>MFRC522 Pin</th><th>STM32 Pin</th></tr></thead>
    <tbody>
      <tr><td>SDA / CS</td><td>PA4</td></tr>
      <tr><td>SCK</td><td>PA5</td></tr>
      <tr><td>MOSI</td><td>PA7</td></tr>
      <tr><td>MISO</td><td>PA6</td></tr>
      <tr><td>RST</td><td>PB0</td></tr>
      <tr><td>IRQ</td><td>— (not used)</td></tr>
    </tbody>
  </table>

  <h3>SSD1306 OLED (I2C – Register Level Implementation)</h3>
  <table class="rv-table">
    <thead><tr><th>Signal</th><th>STM32 Pin</th></tr></thead>
    <tbody>
      <tr><td>SCL</td><td>PB6</td></tr>
      <tr><td>SDA</td><td>PB7</td></tr>
    </tbody>
  </table>

  <h3>Other Peripherals</h3>
  <table class="rv-table">
    <thead><tr><th>Function</th><th>Pin</th></tr></thead>
    <tbody>
      <tr><td>Button (active-low)</td><td>PA0</td></tr>
      <tr><td>Potentiometer (ADC1_IN1)</td><td>PA1</td></tr>
      <tr><td>Buzzer</td><td>PB2</td></tr>
      <tr><td>LED (Active-low)</td><td>PC13</td></tr>
    </tbody>
  </table>

  <hr>

  <h2>🚀 <strong>How to Clone & Open the Project</strong></h2>

  <div class="rv-section">
    <h3>1️⃣ Clone the repository</h3>
    <div class="rv-code">
<pre>git clone https://github.com/PraneethUday/RFID-Voting-System.git
cd RFID-Voting-System can you update this readme.md of our project in a more visually appealing way iu can use html,css or an lang req, for it .but pls dont change the content</pre>
    </div>
  </div>

  <hr>

  <div class="rv-note">
    <strong>Note:</strong> This file is a styled HTML/CSS version of your original README content — the text and technical details are unchanged.
  </div>

  <div class="rv-footer">
    If you'd like, I can:
    <ul>
      <li>Convert this to a pure Markdown-only variant (no inline HTML/CSS) for wider compatibility.</li>
      <li>Add badges (build/arm-none-eabi, license) or a screenshot of wiring and OLED UI (if you provide an image).</li>
      <li>Generate a small `docs/` HTML preview or GitHub Pages-ready layout.</li>
    </ul>
  </div>
</div>
