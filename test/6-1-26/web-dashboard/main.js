// ================= MQTT Configuration =================
const MQTT_BROKER_URL = "wss://test.mosquitto.org:8081"
const MQTT_TOPIC_STATUS = "package/status"
const MQTT_TOPIC_COMMAND = "package/command"
const MQTT_TOPIC_RESPONSE = "package/response"

let client = null
let isConnected = false

// Declare Paho variable before using it
const Paho = window.Paho

// ================= Initialize MQTT Connection =================
function initMQTT() {
  const clientId = "paket-pintar-" + Math.random().toString(16).substr(2, 8)

  client = new Paho.MQTT.Client(MQTT_BROKER_URL, clientId)

  client.onConnectionLost = onConnectionLost
  client.onMessageArrived = onMessageArrived

  client.connect({
    onSuccess: onConnect,
    useSSL: true,
    cleanSession: true,
    reconnect: true,
  })
}

function onConnect() {
  isConnected = true
  updateMQTTStatus(true)
  addLog("MQTT berhasil terkoneksi", "success")

  // Subscribe ke topics
  client.subscribe(MQTT_TOPIC_STATUS)
  client.subscribe(MQTT_TOPIC_RESPONSE)
}

function onConnectionLost(responseObject) {
  isConnected = false
  updateMQTTStatus(false)

  if (responseObject.errorCode !== 0) {
    addLog("Koneksi MQTT terputus: " + responseObject.errorMessage, "error")
  }
}

function onMessageArrived(message) {
  const topic = message.destinationName
  const payload = message.payloadString

  console.log(`[MQTT] Topic: ${topic}, Payload: ${payload}`)

  if (topic === MQTT_TOPIC_STATUS) {
    handleStatusUpdate(payload)
  } else if (topic === MQTT_TOPIC_RESPONSE) {
    handleResponseUpdate(payload)
  }
}

function updateMQTTStatus(connected) {
  const indicator = document.querySelector(".status-indicator")
  const text = document.getElementById("mqtt-text")

  if (connected) {
    indicator.classList.add("connected")
    indicator.classList.remove("disconnected")
    text.textContent = "Terhubung"
    addLog("Status MQTT: Terhubung", "success")
  } else {
    indicator.classList.remove("connected")
    indicator.classList.add("disconnected")
    text.textContent = "Terputus"
    addLog("Status MQTT: Terputus", "error")
  }
}

// ================= Status Management =================
let currentStatus = null

function handleStatusUpdate(status) {
  currentStatus = status
  updateStatusDisplay(status)
  addLog(`Status AI: ${status}`, "info")

  // Play notification sound (optional)
  playNotification()
}

function updateStatusDisplay(status) {
  // Remove active class from all
  document.querySelectorAll(".status-item").forEach((item) => {
    item.classList.remove("active")
  })

  // Add active class to current status
  const statusMap = {
    listening: "status-listening",
    thinking: "status-thinking",
    speaking: "status-speaking",
    sleep: "status-sleep",
  }

  if (statusMap[status]) {
    document.getElementById(statusMap[status])?.classList.add("active")
  }
}

function handleResponseUpdate(response) {
  try {
    const data = JSON.parse(response)

    // Update package info
    if (data.name) {
      document.getElementById("recipient-name").textContent = data.name
      addLog(`📦 Paket untuk: ${data.name}`, "success")
    }

    if (data.tts_text) {
      document.getElementById("ai-response").textContent = data.tts_text
      addLog(`🤖 AI: ${data.tts_text}`, "info")
    }

    if (data.status) {
      document.getElementById("package-status").textContent = data.status
    }
  } catch (e) {
    console.error("Error parsing response:", e)
    document.getElementById("ai-response").textContent = response
  }
}

// ================= Activity Log =================
function addLog(message, type = "info") {
  const logContainer = document.getElementById("activity-logs")
  const timestamp = new Date().toLocaleTimeString("id-ID", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  })

  const logEntry = document.createElement("div")
  logEntry.className = `log-entry ${type}`
  logEntry.innerHTML = `
        <span class="timestamp">${timestamp}</span>
        <span class="message">${message}</span>
    `

  logContainer.appendChild(logEntry)
  logContainer.scrollTop = logContainer.scrollHeight

  // Keep max 50 logs
  const entries = logContainer.querySelectorAll(".log-entry")
  if (entries.length > 50) {
    entries[0].remove()
  }
}

function clearLogs() {
  const logContainer = document.getElementById("activity-logs")
  logContainer.innerHTML = ""
  addLog("Log telah dihapus", "info")
}

// ================= UI Interactions =================
document.getElementById("clear-logs")?.addEventListener("click", clearLogs)

// Status item click handler
document.querySelectorAll(".status-item").forEach((item) => {
  item.addEventListener("click", function () {
    const status = this.id.replace("status-", "")
    addLog(`Status ${status} diklik`, "info")
  })
})

// ================= Notifications =================
function playNotification() {
  // Create a simple beep sound using Web Audio API
  const audioContext = new (window.AudioContext || window.webkitAudioContext)()
  const oscillator = audioContext.createOscillator()
  const gain = audioContext.createGain()

  oscillator.connect(gain)
  gain.connect(audioContext.destination)

  oscillator.frequency.value = 800
  oscillator.type = "sine"

  gain.gain.setValueAtTime(0.3, audioContext.currentTime)
  gain.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + 0.1)

  oscillator.start(audioContext.currentTime)
  oscillator.stop(audioContext.currentTime + 0.1)
}

// ================= Simulation Mode (for testing without MQTT) =================
function simulateStatus() {
  const statuses = ["listening", "thinking", "speaking", "sleep"]
  const randomStatus = statuses[Math.floor(Math.random() * statuses.length)]
  handleStatusUpdate(randomStatus)
}

function simulateResponse() {
  const responses = [
    { name: "Budi Santoso", tts_text: "Permisi, paket untuk Anda", status: "Paket diterima" },
    { name: "Siti Nurhaliza", tts_text: "Ada paket dari Tokopedia", status: "Menunggu penerima" },
    { name: "Ahmad Wijaya", tts_text: "Silakan ambil paket Anda", status: "Paket sudah dibuka" },
  ]

  const randomResponse = responses[Math.floor(Math.random() * responses.length)]
  handleResponseUpdate(JSON.stringify(randomResponse))
}

// ================= Initialize =================
document.addEventListener("DOMContentLoaded", () => {
  // Try to initialize MQTT
  try {
    // Check if Paho MQTT is available
    if (typeof Paho !== "undefined") {
      initMQTT()
    } else {
      console.warn("Paho MQTT library not loaded. Enable simulation mode.")
      updateMQTTStatus(false)
      addLog("MQTT library tidak ditemukan. Mode simulasi aktif.", "warning")
    }
  } catch (e) {
    console.error("MQTT initialization error:", e)
    updateMQTTStatus(false)
    addLog("Gagal menginisialisasi MQTT", "error")
  }

  // Initialize with default status
  updateStatusDisplay("sleep")
  addLog("Dashboard dimulai. Menunggu perangkat...", "info")
})

// Optional: Keyboard shortcuts for testing
document.addEventListener("keydown", (e) => {
  // Ctrl+S for simulate status
  if (e.ctrlKey && e.key === "s") {
    simulateStatus()
  }

  // Ctrl+R for simulate response
  if (e.ctrlKey && e.key === "r") {
    simulateResponse()
  }
})
