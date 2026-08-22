const errorContainer = document.querySelector('#error-container');
const ui = new WebUI();
let pollingInterval;
let countdownTimer;

ui.on_connect(onUIConnected);
ui.on_disconnect(onUIDisconnected);
ui.on_message('dashboard_data', updateDashboard);

ui.on_message('camera_stream', (data) => {
    document.getElementById('live-video').src = data.image;
});

ui.on_message('ai_results', (data) => {
    document.getElementById('ai-crop').innerText = data.ai.crop;
    document.getElementById('ai-crop').style.color = "#1a1a1a";
    
    let healthEl = document.getElementById('ai-health');
    healthEl.innerText = data.ai.health;
    if (data.ai.health.includes("Disease") || data.ai.health.includes("Sick")) {
        healthEl.style.color = "#c0392b"; 
    } else {
        healthEl.style.color = "#2e7d32"; 
    }

    let statusEl = document.getElementById('ai-dosing');
    statusEl.innerText = data.ai.status;
    statusEl.style.color = data.ai.status.includes("DISEASED") ? "#c0392b" : "#008184";

    if (data.ai.targets) {
        document.getElementById('target-n').innerText = data.ai.targets.N + " mg/kg";
        document.getElementById('target-p').innerText = data.ai.targets.P + " mg/kg";
        document.getElementById('target-k').innerText = data.ai.targets.K + " mg/kg";
    } else {
        document.getElementById('target-n').innerText = "--";
        document.getElementById('target-p').innerText = "--";
        document.getElementById('target-k').innerText = "--";
    }
});

ui.on_message('alert_msg', (data) => {
    alert(data.msg); 
    let btn = document.getElementById('dose-btn');
    btn.innerText = "Dispense via Actuator";
    btn.style.background = "#2e7d32";
    btn.disabled = false;
});

// --- Live Sync Countdown Logic ---
ui.on_message('start_pump_countdown', (data) => {
    document.getElementById('dosing-desc').style.display = 'none';
    document.getElementById('countdown-container').style.display = 'flex';
    
    let btn = document.getElementById('dose-btn');
    btn.innerText = "Actuating (Batt: " + data.voltage + "mV)";
    btn.style.background = "#d35400"; 
    btn.disabled = true;

    let tN = data.n; let tP = data.p; let tK = data.k; let tW = data.w;
    updateCountdownUI(tN, tP, tK, tW);

    clearInterval(countdownTimer);
    countdownTimer = setInterval(() => {
        tN = Math.max(0, tN - 0.1);
        tP = Math.max(0, tP - 0.1);
        tK = Math.max(0, tK - 0.1);
        tW = Math.max(0, tW - 0.1);

        updateCountdownUI(tN, tP, tK, tW);

        if (tN === 0 && tP === 0 && tK === 0 && tW === 0) {
            clearInterval(countdownTimer);
            btn.innerText = "Dispense via Actuator";
            btn.style.background = "#2e7d32";
            btn.disabled = false;
            document.getElementById('dosing-desc').style.display = 'block';
            document.getElementById('countdown-container').style.display = 'none';
        }
    }, 100);
});

function updateCountdownUI(n, p, k, w) {
    document.getElementById('cd-n').innerText = n.toFixed(1);
    document.getElementById('cd-p').innerText = p.toFixed(1);
    document.getElementById('cd-k').innerText = k.toFixed(1);
    document.getElementById('cd-w').innerText = w.toFixed(1);
}

// Button Events
document.getElementById('capture-btn').addEventListener('click', () => {
    document.getElementById('ai-crop').innerText = "Analyzing frame...";
    document.getElementById('ai-crop').style.color = "#d35400"; 
    document.getElementById('ai-health').innerText = "Running Cascade...";
    document.getElementById('ai-health').style.color = "#d35400"; 
    ui.send_message('analyze_frame', {});
});

document.getElementById('restart-cam-btn').addEventListener('click', () => {
    ui.send_message('restart_camera', {});
    let btn = document.getElementById('restart-cam-btn');
    btn.style.background = "#7f8c8d";
    document.getElementById('live-video').src = ""; 
    setTimeout(() => { btn.style.background = "#c0392b"; }, 1500); 
});

document.getElementById('dose-btn').addEventListener('click', () => {
    ui.send_message('trigger_dose', {});
    let btn = document.getElementById('dose-btn');
    btn.innerText = "Transmitting & Waiting for ACK...";
    btn.style.background = "#008184";
    btn.disabled = true;
});

function onUIConnected() {
  errorContainer.style.display = 'none';
  ui.send_message('fetch_data');
  pollingInterval = setInterval(() => { ui.send_message('fetch_data'); }, 2000);
}

function onUIDisconnected() {
  errorContainer.style.display = 'block';
  errorContainer.textContent = 'Connection to the board lost. Please wait...';
  clearInterval(pollingInterval);
}

function updateDashboard(data) {
  document.getElementById('val-n').innerText = data.n;
  document.getElementById('val-p').innerText = data.p;
  document.getElementById('val-k').innerText = data.k;
  document.getElementById('val-moist').innerText = data.moist;
  document.getElementById('val-temp').innerText = data.temp;
  document.getElementById('val-hum').innerText = data.hum;
}