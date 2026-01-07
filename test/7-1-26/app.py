import os
import json
import base64
import wave
import soundfile as sf
from scipy import signal
from datetime import datetime
from flask import Flask, request, jsonify, send_file
from flask_cors import CORS
from werkzeug.security import generate_password_hash, check_password_hash
from dotenv import load_dotenv
import google.generativeai as genai
from google.genai import types
import paho.mqtt.client as mqtt_client
import threading
import random
import numpy as np
from copy import deepcopy
import time

# Load environment variables
load_dotenv()
app = Flask(__name__)
CORS(app)

# Configure Gemini AI with fallback models
def initialize_gemini():
    """Initialize Gemini AI with multiple model fallbacks."""
    try:
        genai.configure(api_key=os.getenv('GEMINI_API_KEY'))
       
        model_names = [
            'gemini-2.5-flash', 'gemini-1.5-flash', 'gemini-1.5-pro', 'gemini-pro',
            'models/gemini-2.5-flash', 'models/gemini-1.5-flash', 'models/gemini-1.5-pro'
        ]
       
        available_models = []
        try:
            for model in genai.list_models():
                available_models.append(model.name)
                print(f"[AI] Available model: {model.name}")
        except Exception as e:
            print(f"[AI] Warning: Could not list models: {e}")
       
        for model_name in model_names:
            try:
                if available_models and model_name not in available_models:
                    full_model_name = f"models/{model_name}" if not model_name.startswith('models/') else model_name
                    if full_model_name not in available_models:
                        continue
                    model_name = full_model_name
               
                test_model = genai.GenerativeModel(model_name)
                test_response = test_model.generate_content("Hello")
                print(f"[AI] Successfully initialized Gemini AI with model: {model_name}")
                return test_model, model_name
               
            except Exception as model_error:
                print(f"[AI] Failed to initialize model {model_name}: {model_error}")
                continue
       
        print("[AI] No working Gemini model found")
        return None, None
       
    except Exception as e:
        print(f"[AI] Error configuring Gemini API: {e}")
        return None, None

# Initialize Gemini AI
model, model_name = initialize_gemini()

# Helper function to check model availability
def ensure_model_available():
    """Ensure model is available, reinitialize if necessary."""
    global model, model_name
    if model is None:
        print("[AI] Attempting to reinitialize Gemini AI...")
        model, model_name = initialize_gemini()
    return model is not None

# Helper Functions
def create_response(status, message, data=None):
    """Create standard JSON response."""
    return {
        'status': status,
        'message': message,
        'data': data if data is not None else {},
        'timestamp': datetime.now().isoformat()
    }

def get_indonesian_day(english_day):
    """Convert English day to Indonesian."""
    days = {
        'Monday': 'Senin', 'Tuesday': 'Selasa', 'Wednesday': 'Rabu',
        'Thursday': 'Kamis', 'Friday': 'Jumat', 'Saturday': 'Sabtu', 'Sunday': 'Minggu'
    }
    return days.get(english_day, english_day)

def validate_required_fields(data, required_fields):
    """Validate required fields."""
    if not isinstance(data, dict):
        return False, "Data harus berupa object JSON"
   
    missing_fields = [field for field in required_fields if field not in data or not str(data[field]).strip()]
    if missing_fields:
        return False, f"Field berikut wajib diisi: {', '.join(missing_fields)}"
    return True, ""

def validate_email(email):
    """Validate email format."""
    import re
    pattern = r'^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$'
    return re.match(pattern, email) is not None

# Genetic Algorithm for Schedule Optimization
def time_to_minutes(time_str):
    """Convert time string (HH:MM) to minutes for easier comparison."""
    try:
        hours, minutes = map(int, time_str.split(':'))
        return hours * 60 + minutes
    except:
        return 0

def check_time_conflict(schedule1, schedule2):
    """Check if two schedules conflict in time."""
    if schedule1['hari'] != schedule2['hari']:
        return False
    start1, end1 = map(time_to_minutes, schedule1['waktu'].split('-'))
    start2, end2 = map(time_to_minutes, schedule2['waktu'].split('-'))
    return not (end1 <= start2 or end2 <= start1)

def fitness_function(schedule_list, max_sks_per_day=12):
    """Calculate fitness of a schedule arrangement."""
    score = 100.0
    sks_per_day = {}
   
    # Penalize time conflicts
    for i, s1 in enumerate(schedule_list):
        for s2 in schedule_list[i+1:]:
            if check_time_conflict(s1, s2):
                score -= 20.0
   
    # Penalize unbalanced SKS per day
    for s in schedule_list:
        day = s['hari']
        sks_per_day[day] = sks_per_day.get(day, 0) + s['sks']
   
    for day, sks in sks_per_day.items():
        if sks > max_sks_per_day:
            score -= 10.0 * (sks - max_sks_per_day)
        elif sks == 0:
            score -= 5.0
   
    # Prefer morning schedules (optional, can be adjusted)
    for s in schedule_list:
        start_time = time_to_minutes(s['waktu'].split('-')[0])
        if start_time > 12 * 60: # After 12:00
            score -= 2.0
   
    return max(score, 0.0)

def crossover(parent1, parent2):
    """Perform crossover between two parents."""
    if len(parent1) <= 1:
        return deepcopy(parent1), deepcopy(parent2)
    split_point = random.randint(1, len(parent1)-1)
    child1 = deepcopy(parent1[:split_point]) + deepcopy(parent2[split_point:])
    child2 = deepcopy(parent2[:split_point]) + deepcopy(parent1[split_point:])
    return child1, child2

def mutate(schedule_list, available_times=['08:00-09:40', '10:00-11:40', '13:00-14:40', '15:00-16:40']):
    """Mutate a schedule by changing time or day."""
    if not schedule_list:
        return schedule_list
    schedule = random.choice(schedule_list)
    if random.random() < 0.5:
        schedule['hari'] = random.choice(['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday'])
    else:
        schedule['waktu'] = random.choice(available_times)
    return schedule_list

def genetic_algorithm(schedules, population_size=50, generations=100):
    """Run genetic algorithm to optimize schedules."""
    if not schedules:
        return schedules
   
    population = [deepcopy(schedules) for _ in range(population_size)]
    for i in range(population_size):
        for schedule in population[i]:
            schedule['hari'] = random.choice(['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday'])
            schedule['waktu'] = random.choice(['08:00-09:40', '10:00-11:40', '13:00-14:40', '15:00-16:40'])
   
    for _ in range(generations):
        fitness_scores = [fitness_function(ind) for ind in population]
        if not fitness_scores or max(fitness_scores) == 0:
            break
       
        new_population = []
        for _ in range(population_size // 2):
            parent1 = random.choices(population, weights=fitness_scores)[0]
            parent2 = random.choices(population, weights=fitness_scores)[0]
            child1, child2 = crossover(parent1, parent2)
            child1 = mutate(child1)
            child2 = mutate(child2)
            new_population.extend([child1, child2])
       
        population = new_population[:population_size]
   
    best_schedule = max(population, key=fitness_function)
    return best_schedule

# TTS Functions (from server_gemini_audio.py)
API_KEY = os.getenv("GEMINI_API_KEY")
client = None
model_tts_name = None

def initialize_gemini_tts():
    global client, model_tts_name
    try:
        model_names = [
            'gemini-2.5-flash-preview-tts',
            'gemini-2.5-pro-preview-tts',
            'models/gemini-2.5-flash-preview-tts',
            'models/gemini-2.5-pro-preview-tts'
        ]
        
        available_models = []
        client = genai.Client(api_key=API_KEY)
        
        # List models via Client
        try:
            models = client.list_models()
            for model in models:
                available_models.append(model.name)
                print(f"[TTS] Available model: {model.name}")
        except Exception as e:
            print(f"[TTS] Warning: Could not list models: {e}")
        
        for model_name in model_names:
            try:
                if available_models and model_name not in available_models:
                    full_model_name = f"models/{model_name.split('/')[-1]}" if not model_name.startswith('models/') else model_name
                    if full_model_name not in available_models:
                        continue
                    model_name = full_model_name
                
                test_config = types.GenerateContentConfig(
                    response_modalities=["AUDIO"],
                    speech_config=types.SpeechConfig(
                        voice_config=types.VoiceConfig(
                            prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name="Kore")
                        )
                    )
                )
                test_response = client.models.generate_content(
                    model=model_name,
                    contents="Test: Halo.",
                    config=test_config
                )
                if (test_response.candidates and 
                    test_response.candidates[0].content.parts and 
                    hasattr(test_response.candidates[0].content.parts[0].inline_data, 'data')):
                    print(f"[TTS] Successfully initialized with model: {model_name}")
                    model_tts_name = model_name
                    return
            except Exception as model_error:
                print(f"[TTS] Failed to initialize model {model_name}: {model_error}")
                continue
        
        print("[TTS] No working model found")
        
    except Exception as e:
        print(f"[TTS] Error: {e}")

def save_wav(filename, pcm_data, rate=24000, channels=1, sample_width=2, target_rate=None):
    """Save raw PCM bytes to WAV file; resample if target_rate specified."""
    with wave.open(filename, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(sample_width)
        wf.setframerate(rate)
        wf.writeframes(pcm_data)
    print(f"🔊 Audio tersimpan: {filename} ({rate}Hz, mono)")
    
    if target_rate and target_rate != rate:
        audio, sr = sf.read(filename)
        target_len = int(len(audio) * target_rate / sr)
        resampled = signal.resample(audio, target_len)
        sf.write(filename, resampled, target_rate)
        print(f"🔄 Resampled to {target_rate}Hz: {filename}")

def gemini_text_to_speech(text, output_file="output.wav", voice_name="Kore", target_rate=16000):
    """Generate TTS audio using Gemini."""
    if not client:
        return False
    
    config = types.GenerateContentConfig(
        response_modalities=["AUDIO"],
        speech_config=types.SpeechConfig(
            voice_config=types.VoiceConfig(
                prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name=voice_name)
            )
        )
    )
    
    max_retries = 3
    for attempt in range(max_retries):
        try:
            response = client.models.generate_content(
                model=model_tts_name,
                contents=text,
                config=config
            )
            
            if (response.candidates and 
                response.candidates[0].content.parts and 
                hasattr(response.candidates[0].content.parts[0].inline_data, 'data')):
                audio_bytes = response.candidates[0].content.parts[0].inline_data.data
                save_wav(output_file, audio_bytes, target_rate=target_rate)
                return True
            else:
                raise ValueError("No audio in response.")
                
        except Exception as e:
            print(f"[ERROR] TTS attempt {attempt + 1}: {e}")
            if attempt == max_retries - 1:
                raise
            time.sleep(1)
    
    return False

# Initialize TTS
initialize_gemini_tts()

# ================= MQTT CONFIG =================
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MQTT_STATUS_TOPIC = "package/status"
MQTT_COMMAND_TOPIC = "package/command"

mqtt_client_obj = None

def on_mqtt_connect(client, userdata, flags, rc):
    if rc == 0:
        print("[MQTT] Connected to broker")
        client.subscribe(MQTT_STATUS_TOPIC)
    else:
        print(f"[MQTT] Failed to connect: {rc}")

def on_mqtt_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode('utf-8')
    print(f"[MQTT] {topic}: {payload}")
    
    if payload == "boot_ready" and topic == MQTT_STATUS_TOPIC:
        print("[MQTT] ESP32 ready! Generating 'system active' audio...")
        text = "Sistem paket pintar ESP32 aktif dan siap digunakan!"
        try:
            success = gemini_text_to_speech(text, output_file="active.wav")
            if success:
                print("✅ TTS generated: active.wav")
                client.publish(MQTT_COMMAND_TOPIC, json.dumps({
                    "cmd": "play_audio",
                    "file": "active.wav"
                }))
                print("[MQTT] Command sent: Play active.wav on ESP32")
        except Exception as e:
            print(f"[ERROR] Auto-TTS failed: {e}")

def setup_mqtt():
    global mqtt_client_obj
    client = mqtt_client.Client()
    client.on_connect = on_mqtt_connect
    client.on_message = on_mqtt_message
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    
    def mqtt_loop():
        client.loop_forever()
    
    mqtt_thread = threading.Thread(target=mqtt_loop)
    mqtt_thread.daemon = True
    mqtt_thread.start()
    mqtt_client_obj = client
    print("[MQTT] Client started")

# Setup MQTT
setup_mqtt()

# Database functions (asumsikan dari db_config.py - replace with your actual implementation)
def init_database():
    # Placeholder - implement your DB init
    pass

def get_user_by_email(email):
    # Placeholder
    return None

def create_user(name, email, hashed_password):
    # Placeholder
    return True

def get_user_schedules(user_id):
    # Placeholder
    return []

def get_today_schedules(user_id, day):
    # Placeholder
    return []

def add_schedule(user_id, hari, waktu, kode_matkul, nama_matkul, sks, kelas, dosen):
    # Placeholder
    return True

def delete_schedule(jadwal_id):
    # Placeholder
    return 1

def save_chat_history(user_id, message, response):
    # Placeholder
    pass

def test_connection():
    # Placeholder
    return True

# Init DB
init_database()

# Existing Routes
@app.route('/')
def index():
    """Main endpoint for testing server."""
    return jsonify({
        'message': 'API Flask Auth and Schedule Service with Gemini AI and GA Running',
        'version': '2.2',
        'status': 'active',
        'ai_model': model_name if model_name else 'unavailable',
        'timestamp': datetime.now().isoformat()
    })

@app.route('/health', methods=['GET'])
def health_check():
    """Health check endpoint."""
    db_status = test_connection()
    ai_status = ensure_model_available()
   
    return jsonify({
        'status': 'healthy' if db_status and ai_status else 'degraded',
        'database': 'connected' if db_status else 'disconnected',
        'ai_service': f'available ({model_name})' if ai_status else 'unavailable',
        'timestamp': datetime.now().isoformat()
    })

@app.route('/signup', methods=['POST'])
@app.route('/daftar', methods=['POST'])
def signup():
    """Endpoint for user registration."""
    try:
        data = request.get_json()
        if not data:
            return jsonify(create_response('error', 'Data tidak valid')), 400
        required_fields = ['name', 'email', 'password']
        is_valid, message = validate_required_fields(data, required_fields)
        if not is_valid:
            return jsonify(create_response('error', message)), 400
        if not validate_email(data['email']):
            return jsonify(create_response('error', 'Format email tidak valid')), 400
        if len(data['password']) < 6:
            return jsonify(create_response('error', 'Password minimal 6 karakter')), 400
        hashed_password = generate_password_hash(data['password'])
        existing_user = get_user_by_email(data['email'])
        if existing_user:
            return jsonify(create_response('error', 'Email sudah terdaftar')), 400
        result = create_user(data['name'], data['email'], hashed_password)
        if result:
            return jsonify(create_response('success', 'Pendaftaran berhasil')), 201
        else:
            return jsonify(create_response('error', 'Gagal mendaftarkan user')), 500
    except Exception as e:
        print(f"[ERROR] Signup: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan server')), 500

@app.route('/signin', methods=['POST'])
@app.route('/masuk', methods=['POST'])
def signin():
    """Endpoint for user login."""
    try:
        data = request.get_json()
        if not data:
            return jsonify(create_response('error', 'Data tidak valid')), 400
        required_fields = ['email', 'password']
        is_valid, message = validate_required_fields(data, required_fields)
        if not is_valid:
            return jsonify(create_response('error', message)), 400
        user = get_user_by_email(data['email'])
        if not user:
            return jsonify(create_response('error', 'Email tidak terdaftar')), 401
        if check_password_hash(user['password'], data['password']):
            return jsonify(create_response('success', 'Login berhasil', {
                'user_id': user['id'],
                'name': user['name'],
                'email': user['email']
            })), 200
        else:
            return jsonify(create_response('error', 'Password salah')), 401
    except Exception as e:
        print(f"[ERROR] Signin: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan server')), 500

@app.route('/jadwal', methods=['POST'])
def tambah_jadwal():
    """Endpoint to add a new schedule."""
    try:
        data = request.get_json()
        if not data:
            return jsonify(create_response('error', 'Data tidak valid')), 400
        required_fields = ['user_id', 'hari', 'waktu', 'kode_matkul', 'nama_matkul', 'sks', 'kelas', 'dosen']
        is_valid, message = validate_required_fields(data, required_fields)
        if not is_valid:
            return jsonify(create_response('error', message)), 400
        valid_days_indonesia = ['Senin', 'Selasa', 'Rabu', 'Kamis', 'Jumat', 'Sabtu', 'Minggu']
        valid_days_english = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']
        valid_days = valid_days_english + valid_days_indonesia
        if data['hari'] not in valid_days:
            return jsonify(create_response('error', 'Hari tidak valid. Gunakan format Senin-Sunday atau Monday-Sunday')), 400
        hari = data['hari']
        if hari in valid_days_indonesia:
            days_map = {
                'Senin': 'Monday', 'Selasa': 'Tuesday', 'Rabu': 'Wednesday',
                'Kamis': 'Thursday', 'Jumat': 'Friday', 'Sabtu': 'Saturday', 'Minggu': 'Sunday'
            }
            hari = days_map.get(hari, hari)
            data['hari'] = hari
        try:
            sks = int(data['sks'])
            if sks < 1 or sks > 6:
                return jsonify(create_response('error', 'SKS harus antara 1-6')), 400
        except ValueError:
            return jsonify(create_response('error', 'SKS harus berupa angka')), 400
        result = add_schedule(
            data['user_id'], data['hari'], data['waktu'],
            data['kode_matkul'], data['nama_matkul'],
            sks, data['kelas'], data['dosen']
        )
        if result:
            return jsonify(create_response('success', 'Jadwal berhasil ditambahkan')), 201
        else:
            return jsonify(create_response('error', 'Gagal menambahkan jadwal')), 500
    except Exception as e:
        print(f"[ERROR] Tambah jadwal: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan server')), 500
   
@app.route('/jadwal/<int:user_id>', methods=['GET'])
def dapatkan_jadwal(user_id):
    """Endpoint to get all user schedules."""
    try:
        schedules = get_user_schedules(user_id)
        if schedules is not None:
            return jsonify(create_response('success', 'Jadwal ditemukan', schedules)), 200
        else:
            return jsonify(create_response('error', 'Gagal mengambil jadwal')), 500
    except Exception as e:
        print(f"[ERROR] Dapatkan jadwal: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan server')), 500

@app.route('/jadwal/hari-ini/<int:user_id>', methods=['GET'])
def jadwal_hari_ini(user_id):
    """Endpoint to get today's schedules."""
    try:
        hari_ini = datetime.now().strftime("%A")
        schedules = get_today_schedules(user_id, hari_ini)
       
        if schedules is not None:
            hari_indo = get_indonesian_day(hari_ini)
            for schedule in schedules:
                schedule['hari_indonesia'] = hari_indo
               
            return jsonify(create_response('success', f'Jadwal hari {hari_indo} ditemukan', schedules)), 200
        else:
            return jsonify(create_response('error', 'Gagal mengambil jadwal hari ini')), 500
    except Exception as e:
        print(f"[ERROR] Jadwal hari ini: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan server')), 500

@app.route('/jadwal/<int:jadwal_id>', methods=['DELETE'])
def hapus_jadwal(jadwal_id):
    """Endpoint to delete a schedule."""
    try:
        result = delete_schedule(jadwal_id)
        if result and result > 0:
            return jsonify(create_response('success', 'Jadwal berhasil dihapus')), 200
        elif result == 0:
            return jsonify(create_response('error', 'Jadwal tidak ditemukan')), 404
        else:
            return jsonify(create_response('error', 'Gagal menghapus jadwal')), 500
    except Exception as e:
        print(f"[ERROR] Hapus jadwal: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan server')), 500

@app.route('/jadwal-batch', methods=['POST'])
def tambah_jadwal_batch():
    """Endpoint to add multiple schedules at once."""
    try:
        data = request.get_json()
        if not data:
            return jsonify(create_response('error', 'Data tidak valid')), 400
        required_fields = ['user_id', 'schedule']
        is_valid, message = validate_required_fields(data, required_fields)
        if not is_valid:
            return jsonify(create_response('error', message)), 400
        added_count = 0
        errors = []
        for day_entry in data['schedule']:
            if not day_entry.get('day') or not day_entry.get('courses'):
                continue
               
            for course in day_entry['courses']:
                try:
                    required_course_fields = ['time', 'code', 'name', 'credits', 'class', 'lecturer']
                    is_valid_course, course_msg = validate_required_fields(course, required_course_fields)
                    if not is_valid_course:
                        errors.append(f"Course {course.get('name', 'Unknown')}: {course_msg}")
                        continue
                   
                    result = add_schedule(
                        data['user_id'],
                        day_entry['day'],
                        course['time'],
                        course['code'],
                        course['name'],
                        int(course['credits']),
                        course['class'],
                        course['lecturer']
                    )
                   
                    if result:
                        added_count += 1
                    else:
                        errors.append(f"Gagal menambah {course['name']}")
                       
                except Exception as e:
                    errors.append(f"Error pada {course.get('name', 'Unknown')}: {str(e)}")
        if added_count > 0:
            message = f"Berhasil menambahkan {added_count} jadwal"
            if errors:
                message += f". Errors: {'; '.join(errors[:3])}"
            return jsonify(create_response('success', message, {'added': added_count, 'errors': len(errors)})), 201
        else:
            return jsonify(create_response('error', f'Gagal menambahkan jadwal. Errors: {"; ".join(errors[:5])}')), 400
    except Exception as e:
        print(f"[ERROR] Tambah jadwal batch: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan server')), 500

def format_schedule_for_ai(schedules):
    """Format schedule data for AI context."""
    if not schedules:
        return "Pengguna belum memiliki jadwal kuliah yang tersimpan."
   
    schedule_text = "Jadwal Kuliah Pengguna:\n"
    current_day = ""
   
    for schedule in schedules:
        day_indo = schedule.get('hari_indonesia', get_indonesian_day(schedule['hari']))
        if day_indo != current_day:
            current_day = day_indo
            schedule_text += f"\n{current_day}:\n"
       
        schedule_text += f"- {schedule['waktu']}: {schedule['nama_matkul']} ({schedule['kode_matkul']}) - {schedule['dosen']} - Kelas {schedule['kelas']} ({schedule['sks']} SKS)\n"
   
    return schedule_text

@app.route('/api/chat', methods=['POST'])
def chat_assistant():
    """Endpoint for chatting with AI assistant."""
    try:
        data = request.get_json()
        if not data:
            return jsonify(create_response('error', 'Data request tidak valid')), 400
        user_id = data.get('user_id')
        message = data.get('message', '').strip()
        if not user_id:
            return jsonify(create_response('error', 'User ID diperlukan')), 400
        if not message:
            return jsonify(create_response('error', 'Pesan tidak boleh kosong')), 400
        if not ensure_model_available():
            return jsonify(create_response('error', 'AI service tidak tersedia saat ini')), 503
        schedules = get_user_schedules(user_id) or []
        schedule_context = format_schedule_for_ai(schedules)
       
        current_day_eng = datetime.now().strftime('%A')
        current_day_indo = get_indonesian_day(current_day_eng)
        current_time = datetime.now().strftime('%H:%M')
        current_date = datetime.now().strftime('%Y-%m-%d')
        system_prompt = f"""
Kamu adalah asisten jadwal kuliah yang membantu mahasiswa mengelola jadwal mereka.
Informasi saat ini:
- Hari: {current_day_indo} ({current_date})
- Waktu: {current_time}
{schedule_context}
Tugas kamu:
1. Jawab pertanyaan tentang jadwal kuliah dengan ramah dan informatif
2. Berikan informasi jadwal berdasarkan hari yang diminta
3. Berikan reminder atau saran yang berguna
4. Gunakan bahasa Indonesia yang natural dan friendly
5. Jika tidak ada jadwal, berikan respon yang membantu
6. Jika ditanya jadwal "hari ini", berikan jadwal untuk hari {current_day_indo}
7. Berikan informasi lengkap seperti waktu, mata kuliah, dosen, dan kelas
8. Jika pengguna meminta optimasi jadwal, sarankan untuk menggunakan fitur optimasi otomatis
Pertanyaan pengguna: {message}
Berikan jawaban yang singkat, jelas, dan membantu. Jika ada jadwal yang relevan, tampilkan dalam format yang mudah dibaca.
"""
        max_retries = 3
        for attempt in range(max_retries):
            try:
                response = model.generate_content(
                    system_prompt,
                    generation_config=genai.types.GenerationConfig(
                        temperature=0.7,
                        max_output_tokens=1000
                    )
                )
                ai_response = response.text
               
                try:
                    save_chat_history(user_id, message, ai_response)
                except Exception as save_error:
                    print(f"[WARNING] Failed to save chat history: {save_error}")
               
                return jsonify(create_response('success', ai_response, {
                    'type': 'ai_response',
                    'current_day': current_day_indo,
                    'current_time': current_time,
                    'model_used': model_name,
                    'schedules': schedules if 'jadwal' in message.lower() or 'schedule' in message.lower() else None
                })), 200
               
            except Exception as gemini_error:
                print(f"[ERROR] Gemini AI attempt {attempt + 1}: {gemini_error}")
               
                if attempt == max_retries - 1:
                    model, model_name = initialize_gemini()
                    fallback_response = generate_fallback_response(message, schedules, current_day_indo)
                   
                    return jsonify(create_response('success', fallback_response, {
                        'type': 'fallback',
                        'current_day': current_day_indo,
                        'current_time': current_time,
                        'schedules': schedules if 'jadwal' in message.lower() else None
                    })), 200
               
                time.sleep(1)
    except Exception as e:
        print(f"[ERROR] Chat endpoint: {e}")
        return jsonify(create_response('error', 'Maaf, terjadi kesalahan sistem')), 500

def generate_fallback_response(message, schedules, current_day):
    """Generate fallback response when AI is unavailable."""
    message_lower = message.lower()
   
    if any(keyword in message_lower for keyword in ['jadwal', 'schedule', 'kuliah', 'kelas', 'hari ini', 'today']):
        if 'hari ini' in message_lower or 'today' in message_lower:
            today_schedules = [s for s in schedules if get_indonesian_day(s.get('hari', '')) == current_day]
            if today_schedules:
                response = f"Jadwal kuliah hari {current_day}:\n\n"
                for schedule in today_schedules:
                    response += f"🕒 {schedule['waktu']}: {schedule['nama_matkul']} ({schedule['kode_matkul']})\n"
                    response += f" Dosen: {schedule['dosen']} | Kelas: {schedule['kelas']} | SKS: {schedule['sks']}\n\n"
                return response
            else:
                return f"Tidak ada jadwal kuliah untuk hari {current_day}. Anda bisa santai hari ini! 😊"
       
        elif schedules:
            return f"Anda memiliki {len(schedules)} mata kuliah terjadwal. Gunakan menu jadwal untuk melihat detail lengkap atau tanyakan jadwal untuk hari tertentu."
        else:
            return "Anda belum memiliki jadwal kuliah yang tersimpan. Silakan tambahkan jadwal terlebih dahulu melalui menu jadwal."
   
    return "Maaf, asisten AI sedang tidak tersedia saat ini. Silakan gunakan menu jadwal untuk melihat informasi jadwal Anda, atau coba lagi nanti."

@app.route('/api/schedule-query', methods=['POST'])
def schedule_query():
    """Fallback endpoint for schedule queries without AI."""
    try:
        data = request.get_json()
        if not data:
            return jsonify(create_response('error', 'Data request tidak valid')), 400
        user_id = data.get('user_id')
        query_type = data.get('query_type', 'today')
        if not user_id:
            return jsonify(create_response('error', 'User ID diperlukan')), 400
        if query_type == 'today':
            eng_day = datetime.now().strftime('%A')
            schedules = get_today_schedules(user_id, eng_day)
            indo_day = get_indonesian_day(eng_day)
           
            if schedules:
                for schedule in schedules:
                    schedule['hari_indonesia'] = indo_day
                return jsonify(create_response('success',
                    f'Jadwal hari {indo_day}',
                    {'type': 'schedule', 'day': indo_day, 'schedules': schedules})), 200
            else:
                return jsonify(create_response('success',
                    f'Tidak ada jadwal kuliah hari {indo_day}',
                    {'type': 'empty', 'day': indo_day})), 200
                   
        elif query_type == 'all':
            schedules = get_user_schedules(user_id)
            if schedules:
                return jsonify(create_response('success',
                    'Semua jadwal kuliah',
                    {'type': 'schedule', 'schedules': schedules})), 200
            else:
                return jsonify(create_response('success',
                    'Anda belum memiliki jadwal kuliah',
                    {'type': 'empty'})), 200
        else:
            return jsonify(create_response('error', 'Query type tidak valid')), 400
    except Exception as e:
        print(f"[ERROR] Schedule query: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan sistem')), 500

@app.route('/api/list-models', methods=['GET'])
def list_available_models():
    """Endpoint to list available AI models."""
    try:
        if not os.getenv('GEMINI_API_KEY'):
            return jsonify(create_response('error', 'API key tidak tersedia')), 500
           
        genai.configure(api_key=os.getenv('GEMINI_API_KEY'))
        models = []
       
        for m in genai.list_models():
            models.append({
                'name': m.name,
                'display_name': getattr(m, 'display_name', 'N/A'),
                'description': getattr(m, 'description', 'N/A')
            })
       
        return jsonify(create_response('success', 'Available models retrieved', {
            'models': models,
            'current_model': model_name,
            'total_count': len(models)
        })), 200
       
    except Exception as e:
        print(f"[ERROR] List models: {e}")
        return jsonify(create_response('error', f'Gagal mengambil daftar model: {str(e)}')), 500

@app.route('/jadwal/update/<int:jadwal_id>', methods=['PUT'])
def update_jadwal(jadwal_id):
    """Endpoint to update an existing schedule."""
    try:
        data = request.get_json()
        if not data:
            return jsonify(create_response('error', 'Data tidak valid')), 400
        allowed_fields = ['hari', 'waktu', 'kode_matkul', 'nama_matkul', 'sks', 'kelas', 'dosen']
        update_data = {field: data[field] for field in allowed_fields if field in data}
        if not update_data:
            return jsonify(create_response('error', 'Tidak ada data untuk diupdate')), 400
        if 'hari' in update_data:
            valid_days = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']
            if update_data['hari'] not in valid_days:
                return jsonify(create_response('error', 'Hari tidak valid')), 400
        if 'sks' in update_data:
            try:
                sks = int(update_data['sks'])
                if sks < 1 or sks > 6:
                    return jsonify(create_response('error', 'SKS harus antara 1-6')), 400
                update_data['sks'] = sks
            except ValueError:
                return jsonify(create_response('error', 'SKS harus berupa angka')), 400
        # Note: update_schedule is not implemented yet
        return jsonify(create_response('success', 'Jadwal berhasil diupdate')), 200
    except Exception as e:
        print(f"[ERROR] Update jadwal: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan server')), 500

@app.route('/api/stats/<int:user_id>', methods=['GET'])
def get_user_stats(user_id):
    """Endpoint to get user schedule statistics."""
    try:
        schedules = get_user_schedules(user_id)
        if schedules is None:
            return jsonify(create_response('error', 'Gagal mengambil data')), 500
        total_courses = len(schedules)
        total_sks = sum(schedule['sks'] for schedule in schedules)
       
        daily_stats = {}
        for schedule in schedules:
            day_indo = get_indonesian_day(schedule['hari'])
            if day_indo not in daily_stats:
                daily_stats[day_indo] = {'courses': 0, 'sks': 0}
            daily_stats[day_indo]['courses'] += 1
            daily_stats[day_indo]['sks'] += schedule['sks']
        busiest_day = max(daily_stats.items(), key=lambda x: x[1]['courses']) if daily_stats else None
        stats = {
            'total_courses': total_courses,
            'total_sks': total_sks,
            'daily_stats': daily_stats,
            'busiest_day': {
                'day': busiest_day[0],
                'courses': busiest_day[1]['courses'],
                'sks': busiest_day[1]['sks']
            } if busiest_day else None
        }
        return jsonify(create_response('success', 'Statistik berhasil diambil', stats)), 200
    except Exception as e:
        print(f"[ERROR] Get stats: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan server')), 500

# New Endpoint for Schedule Optimization with GA
@app.route('/api/optimize-schedule', methods=['POST'])
def optimize_schedule():
    """Endpoint to optimize user schedule using Genetic Algorithm and Gemini AI."""
    try:
        data = request.get_json()
        if not data:
            return jsonify(create_response('error', 'Data request tidak valid')), 400
        user_id = data.get('user_id')
        if not user_id:
            return jsonify(create_response('error', 'User ID diperlukan')), 400
        # Get current schedules
        schedules = get_user_schedules(user_id) or []
        if not schedules:
            return jsonify(create_response('error', 'Tidak ada jadwal untuk dioptimasi')), 400
        # Run Genetic Algorithm
        optimized_schedules = genetic_algorithm(schedules)
        optimized_context = format_schedule_for_ai(optimized_schedules)
        # Prepare Gemini AI prompt with optimized schedule
        current_day_eng = datetime.now().strftime('%A')
        current_day_indo = get_indonesian_day(current_day_eng)
        current_time = datetime.now().strftime('%H:%M')
        current_date = datetime.now().strftime('%Y-%m-%d')
        original_context = format_schedule_for_ai(schedules)
        system_prompt = f"""
Kamu adalah asisten jadwal kuliah yang membantu mahasiswa mengelola jadwal mereka.
Informasi saat ini:
- Hari: {current_day_indo} ({current_date})
- Waktu: {current_time}
Jadwal Asli Pengguna:
{original_context}
Jadwal yang Dioptimasi (menggunakan algoritma genetika):
{optimized_context}
Tugas kamu:
1. Berikan penjelasan singkat mengapa jadwal yang dioptimasi lebih baik (misalnya, tidak ada konflik waktu, lebih seimbang)
2. Tampilkan jadwal yang dioptimasi dalam format yang mudah dibaca
3. Berikan saran tambahan untuk pengguna berdasarkan jadwal yang dioptimasi
4. Gunakan bahasa Indonesia yang natural dan friendly
5. Jangan ubah logika pengolahan jadwal di database
Berikan jawaban yang singkat, jelas, dan membantu.
"""
        if not ensure_model_available():
            return jsonify(create_response('error', 'AI service tidak tersedia saat ini')), 503
        max_retries = 3
        for attempt in range(max_retries):
            try:
                response = model.generate_content(
                    system_prompt,
                    generation_config=genai.types.GenerationConfig(
                        temperature=0.7,
                        max_output_tokens=1000
                    )
                )
                ai_response = response.text
               
                try:
                    save_chat_history(user_id, "Optimasi jadwal", ai_response)
                except Exception as save_error:
                    print(f"[WARNING] Failed to save chat history: {save_error}")
               
                return jsonify(create_response('success', ai_response, {
                    'type': 'optimized_schedule',
                    'current_day': current_day_indo,
                    'current_time': current_time,
                    'model_used': model_name,
                    'optimized_schedules': optimized_schedules
                })), 200
               
            except Exception as gemini_error:
                print(f"[ERROR] Gemini AI attempt {attempt + 1}: {gemini_error}")
               
                if attempt == max_retries - 1:
                    model, model_name = initialize_gemini()
                    fallback_response = f"""
Jadwal telah dioptimasi menggunakan algoritma genetika. Berikut adalah jadwal yang dioptimasi:
{optimized_context}
Silakan periksa jadwal ini untuk memastikan tidak ada konflik waktu dan beban SKS seimbang.
"""
                    return jsonify(create_response('success', fallback_response, {
                        'type': 'fallback_optimized',
                        'current_day': current_day_indo,
                        'current_time': current_time,
                        'optimized_schedules': optimized_schedules
                    })), 200
               
                time.sleep(1)
    except Exception as e:
        print(f"[ERROR] Optimize schedule: {e}")
        return jsonify(create_response('error', 'Terjadi kesalahan sistem')), 500

# Audio Serve Route
@app.route('/audio/<filename>')
def serve_audio(filename):
    if os.path.exists(filename):
        return send_file(filename, mimetype='audio/wav')
    return jsonify(create_response('error', 'File tidak ditemukan')), 404

# Error Handlers
@app.errorhandler(404)
def not_found(error):
    return jsonify(create_response('error', 'Endpoint tidak ditemukan')), 404

@app.errorhandler(405)
def method_not_allowed(error):
    return jsonify(create_response('error', 'Method tidak diizinkan')), 405

@app.errorhandler(500)
def internal_error(error):
    return jsonify(create_response('error', 'Terjadi kesalahan server internal')), 500

# Main Application
if __name__ == '__main__':
    try:
        init_database()
        print("[DB] Database berhasil diinisialisasi")
    except Exception as e:
        print(f"[DB] Error inisialisasi database: {e}")
   
    if model and model_name:
        print(f"[DEBUG] Gemini AI ready with model: {model_name}")
    else:
        print("[AI] Warning: Gemini AI tidak tersedia - aplikasi akan berjalan tanpa fitur AI")
   
    debug_mode = os.getenv('FLASK_ENV', 'False').lower() == 'true'
    port = int(os.getenv('PORT', 5000))
   
    print(f"[DEBUG] Menjalankan server pada port {port}")
    print(f"[DEBUG] Debug mode: {debug_mode}")
   
    app.run(debug=debug_mode, host='0.0.0.0', port=port)
