import sys
import os
import subprocess
import base64
import time
import threading
import numpy as np
import cv2

# --- PUMP FLOW RATES & TUNING ---
FLOW_RATE_NPK = 15      # ml per second
FLOW_RATE_WATER = 50    # ml per second
ML_PER_NPK_UNIT = 50    # Need 50ml of fertilizer mix per 1 mg/kg deficit
WATER_DOSE_ML = 2000    # Default amount of water (2L) applied if soil is dry
# --------------------------------

current_dir = os.path.dirname(os.path.abspath(__file__))
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

try:
    import ai_edge_litert.interpreter as tflite
except ModuleNotFoundError:
    print("[SYSTEM] Installing missing TFLite libraries...")
    subprocess.check_call([
        sys.executable, "-m", "pip", "install", 
        "ai-edge-litert", "opencv-python-headless", "numpy<2.5", "-t", current_dir
    ])
    import ai_edge_litert.interpreter as tflite

from arduino.app_utils import *
from arduino.app_bricks.web_ui import WebUI

ui = WebUI()

ICAR_DB = {
    "Tomato": {"N": 120, "P": 60, "K": 60},
    "Potato": {"N": 150, "P": 100, "K": 120},
    "Maize":  {"N": 135, "P": 62, "K": 50},
    "Rice":   {"N": 100, "P": 50, "K": 50},
    "Cotton": {"N": 120, "P": 60, "K": 60},
    "Wheat":  {"N": 120, "P": 60, "K": 40}
}

class SichaiMasterHub:
    def __init__(self):
        print("[SYSTEM] Loading Cascaded AI Models...")
        # Model 1: Crop Species
        with open(os.path.join(current_dir, "models/crop_classes.txt"), "r") as f:
            self.crop_labels = [line.strip() for line in f.readlines()]
        self.crop_interp = tflite.Interpreter(model_path=os.path.join(current_dir, "models/model1_species_7class.tflite"))
        self.crop_interp.allocate_tensors()
        self.crop_input = self.crop_interp.get_input_details()[0]
        self.crop_output = self.crop_interp.get_output_details()[0]

        # Model 3: Disease Binary
        with open(os.path.join(current_dir, "models/disease_classes.txt"), "r") as f:
            self.disease_labels = [line.strip() for line in f.readlines()]
        self.disease_interp = tflite.Interpreter(model_path=os.path.join(current_dir, "models/model3_disease.tflite"))
        self.disease_interp.allocate_tensors()
        self.disease_input = self.disease_interp.get_input_details()[0]
        self.disease_output = self.disease_interp.get_output_details()[0]

    def _run_inference(self, frame, interpreter, input_det, output_det):
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        resized = cv2.resize(rgb, (224, 224))
        input_data = np.expand_dims(resized, axis=0)

        # INT8 Quantization handling
        if input_det['dtype'] == np.uint8:
            scale, zp = input_det['quantization']
            input_data = (input_data / 255.0 / scale + zp).astype(np.uint8)

        interpreter.set_tensor(input_det['index'], input_data)
        interpreter.invoke()
        output_data = interpreter.get_tensor(output_det['index'])[0]

        # Dequantize
        if output_det['dtype'] == np.uint8:
            scale, zp = output_det['quantization']
            output_data = scale * (output_data.astype(np.float32) - zp)
            
        top_idx = int(np.argmax(output_data))
        return top_idx, float(output_data[top_idx])

    def analyze_cascaded(self, frame):
        c_idx, c_conf = self._run_inference(frame, self.crop_interp, self.crop_input, self.crop_output)
        crop_name = self.crop_labels[c_idx]
        disease_name = "N/A"
        d_conf = 0.0
        
        # Cascaded Gating
        if c_conf >= 0.85 and "Background" not in crop_name:
            d_idx, d_conf = self._run_inference(frame, self.disease_interp, self.disease_input, self.disease_output)
            disease_name = self.disease_labels[d_idx]
            
        return crop_name, c_conf, disease_name, d_conf

# Global State
latest_telemetry = {"n": "0", "p": "0", "k": "0", "moist": "0", "temp": "0", "hum": "0"}
current_ai_targets = None 
current_health_status = "Unknown"
latest_frame = None  
ai_hub = None
restart_camera_flag = False

def fetch_telemetry():
    global latest_telemetry
    try:
        raw_data = Bridge.call("get_node1_data")
        if raw_data:
            parts = raw_data.split(',')
            if len(parts) == 6:
                latest_telemetry = {
                    "n": parts[0], "p": parts[1], "k": parts[2],
                    "moist": parts[3], "temp": parts[4], "hum": parts[5]
                }
    except: pass

def get_available_camera():
    for index in range(6): 
        cap = cv2.VideoCapture(index)
        if cap is None or not cap.isOpened():
            cap.release()
            continue
        ret, frame = cap.read()
        if ret:
            print(f"[SYSTEM] 🟢 Camera connected on /dev/video{index}")
            return cap
        cap.release()
    print("[ERROR] 🔴 No available camera found. Retrying in background...")
    return None

def run_camera_stream():
    global latest_frame, ai_hub, restart_camera_flag
    ai_hub = SichaiMasterHub() 
    cap = get_available_camera()
    
    while True:
        if restart_camera_flag:
            print("[SYSTEM] Restarting Camera hardware...")
            if cap is not None: cap.release()
            time.sleep(1) 
            cap = get_available_camera()
            restart_camera_flag = False

        if cap is not None:
            ret, frame = cap.read()
            if ret:
                latest_frame = frame.copy() 
                frame_web = cv2.resize(frame, (480, 360))
                _, buffer = cv2.imencode('.jpg', frame_web, [cv2.IMWRITE_JPEG_QUALITY, 50])
                frame_b64 = base64.b64encode(buffer).decode('utf-8')
                ui.send_message('camera_stream', {'image': 'data:image/jpeg;base64,' + frame_b64})
            else:
                cap.release()
                time.sleep(1)
                cap = get_available_camera()
        else:
            time.sleep(2)
            cap = get_available_camera()
        time.sleep(0.1) 

threading.Thread(target=run_camera_stream, daemon=True).start()

def handle_capture(client, data):
    global current_ai_targets, current_health_status, latest_frame, ai_hub
    
    if latest_frame is not None and ai_hub is not None:
        c_name, c_conf, d_name, d_conf = ai_hub.analyze_cascaded(latest_frame)
        current_health_status = d_name
        
        ai_data = {
            "crop": f"{c_name} ({c_conf*100:.1f}%)", 
            "health": f"{d_name} ({d_conf*100:.1f}%)" if d_name != "N/A" else "N/A",
            "status": "Not in DB", 
            "targets": None
        }
        
        if c_conf >= 0.85 and "Background" not in c_name and c_name in ICAR_DB:
            ai_data["status"] = "ICAR Protocol Active"
            ai_data["targets"] = ICAR_DB[c_name]
            
            if "disease" in d_name.lower() or "sick" in d_name.lower():
                ai_data["status"] = "DISEASED - FERTIGATION HALTED"
        
        current_ai_targets = ai_data["targets"]
        ui.send_message('ai_results', {'ai': ai_data}, client)

def handle_restart_camera(client, data):
    global restart_camera_flag
    restart_camera_flag = True

# --- MISSING FUNCTION ADDED HERE ---
def handle_fetch(client, data):
    fetch_telemetry()
    ui.send_message('dashboard_data', latest_telemetry, client)

def execute_auto_dose(client, data):
    global current_ai_targets, current_health_status, latest_telemetry
    
    if not current_ai_targets:
        ui.send_message('alert_msg', {'msg': 'Dosing aborted: No valid crop recognized.'}, client)
        return
        
    if "disease" in current_health_status.lower() or "sick" in current_health_status.lower():
        print("[SYSTEM] Dosing aborted - Disease detected. Safety interlock active.")
        ui.send_message('alert_msg', {'msg': 'FERTIGATION ABORTED: Plant is diseased!'}, client)
        return
        
    def_n = max(0, current_ai_targets["N"] - int(latest_telemetry["n"]))
    def_p = max(0, current_ai_targets["P"] - int(latest_telemetry["p"]))
    def_k = max(0, current_ai_targets["K"] - int(latest_telemetry["k"]))
    
    sec_n = int((def_n * ML_PER_NPK_UNIT) / FLOW_RATE_NPK)
    sec_p = int((def_p * ML_PER_NPK_UNIT) / FLOW_RATE_NPK)
    sec_k = int((def_k * ML_PER_NPK_UNIT) / FLOW_RATE_NPK)
    sec_w = int(WATER_DOSE_ML / FLOW_RATE_WATER) if int(latest_telemetry["moist"]) < 40 else 0
    
    command_str = f"{sec_n},{sec_p},{sec_k},{sec_w}"
    print(f"[SYSTEM] Transmitting Pump Dosing Times (N:{sec_n}s, P:{sec_p}s, K:{sec_k}s, W:{sec_w}s)...")
    
    try:
        response = Bridge.call("trigger_dosing", command_str)
        if response and response.startswith("OK"):
            parts = response.split(',')
            print(f"[ACK] Pump node telemetry received: status=OK, voltage={parts[1]}mV, packets={parts[2]}")
            
            # Start Live Countdown on UI
            ui.send_message('start_pump_countdown', {
                'n': sec_n, 
                'p': sec_p, 
                'k': sec_k, 
                'w': sec_w, 
                'voltage': parts[1]
            }, client)
        else:
            print("[ERROR] Actuator did not acknowledge command.")
            ui.send_message('alert_msg', {'msg': 'Actuator Node unreachable (No ACK).'}, client)
    except Exception as e:
        print(f"[ERROR] Bridge TX failed: {e}")

ui.on_message('fetch_data', handle_fetch)
ui.on_message('analyze_frame', handle_capture)
ui.on_message('restart_camera', handle_restart_camera)
ui.on_message('trigger_dose', execute_auto_dose)

print("[SYSTEM] SICHAI Cascaded AI Server Online.")
App.run()