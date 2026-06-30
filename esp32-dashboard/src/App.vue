<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from "vue";

const ESP32_PORT = 80;
const ESP32_HOSTNAME = "ESPrul  .local";

// Estado de simulación
const simulationMode = ref(false);
const simulationInterval = ref(null);

const kp = ref(10.13);
const ki = ref(100);
const kd = ref(0);

const profileVmax = ref(120);
const profileTime = ref(5);
const profilePositionTime = ref(5);

const CHART_HEIGHT = 320;
const CHART_PADDING = 40;
const MAX_POINTS = 400;

const activeProfile = ref('rpm');
const rpm = ref(0);
const inputRpm = ref("0");
const connected = ref(false);
const connecting = ref(false);
const discovering = ref(false);
const networkState = ref({
  type: "web",
  isConnected: navigator.onLine,
});
const ipAddress = ref(window.location.hostname || "localhost");
const esp32Ip = ref("");
const esp32Name = ref("");
const esp32List = ref([]);
const selectedEsp32 = ref(null);
const showConfig = ref(false);
const telemetry = ref({
  setpoint: 0,
  rpm: 0,
  pwm: 0,
  kp: 0,
  ki: 0,
  kd: 0,
  mode: "manual"
});
const connectionMessage = ref({ text: "", type: "", visible: false });
const scrollEnabled = ref(true);
let messageTimeout = null;
let reconnectAttempts = 0;

const chartDataVel = ref({
  deseado: [],
  obtenido: [],
  timestamps: [],
});

const chartDataPos = ref({
  setpoint: [],
  obtenido: [],
  timestamps: [],
});

const zoom = ref(1);
const startTime = ref(Date.now());
const pollInterval = ref(null);
const followEnabledVel = ref(true);
const followEnabledPos = ref(true);

const chartScrollContainerVel = ref(null);
const chartScrollContainerPos = ref(null);

const sliderWidth = 450;
const sliderDragging = ref(false);

const chartWidth = computed(() => {
  if (typeof window !== "undefined") {
    return Math.min(window.innerWidth - 100, 1200);
  }
  return 1000;
});

const chartInnerWidth = computed(() => {
  return chartWidth.value * zoom.value;
});

function showMessage(text, type = "success") {
  connectionMessage.value = { text, type, visible: true };
  if (messageTimeout) clearTimeout(messageTimeout);
  messageTimeout = setTimeout(() => {
    connectionMessage.value.visible = false;
  }, 4000);
}

function hideMessage() {
  connectionMessage.value.visible = false;
  if (messageTimeout) clearTimeout(messageTimeout);
}

async function fetchWithTimeout(url, options = {}, timeout = 5000) {
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), timeout);
  try {
    const response = await fetch(url, {
      ...options,
      signal: controller.signal,
    });
    return response;
  } finally {
    clearTimeout(timeoutId);
  }
}

async function discoverESP32s() {
  discovering.value = true;
  esp32List.value = [];
  showMessage("Escaneando red...", "info");
  
  const foundDevices = [];
  
  try {
    const response = await fetchWithTimeout(`http://${ESP32_HOSTNAME}:${ESP32_PORT}/ping`, {}, 2000);
    if (response.ok) {
      foundDevices.push({
        name: "ESPRUL",
        address: ESP32_HOSTNAME,
        ip: ESP32_HOSTNAME,
        type: "mDNS"
      });
      showMessage("ESPRUL encontrado", "success");
    }
  } catch (e) {}
  
  const localIp = await getLocalIP();
  if (localIp) {
    const network = localIp.split('.');
    const baseIp = `${network[0]}.${network[1]}.${network[2]}.`;
    
    for (let i = 1; i <= 254; i++) {
      const testIp = baseIp + i;
      try {
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 300);
        const response = await fetch(`http://${testIp}:${ESP32_PORT}/ping`, {
          signal: controller.signal
        });
        clearTimeout(timeoutId);
        
        if (response.ok) {
          foundDevices.push({
            name: "ESPRUL",
            address: testIp,
            ip: testIp,
            type: "IP"
          });
          showMessage(`ESPRUL encontrado en ${testIp}`, "success");
        }
      } catch (e) {}
    }
  }
  
  esp32List.value = foundDevices;
  
  if (foundDevices.length === 0) {
    showMessage("No se encontraron ESP32", "error");
  } else {
    showMessage(`Encontrados ${foundDevices.length} ESP32`, "success");
  }
  
  discovering.value = false;
}

function getLocalIP() {
  return new Promise((resolve) => {
    try {
      window.RTCPeerConnection = window.RTCPeerConnection || window.mozRTCPeerConnection || window.webkitRTCPeerConnection;
      const pc = new RTCPeerConnection({ iceServers: [] });
      
      pc.createDataChannel('');
      pc.createOffer().then(offer => pc.setLocalDescription(offer));
      pc.onicecandidate = (ice) => {
        if (!ice || !ice.candidate || !ice.candidate.candidate) return;
        const ipRegex = /([0-9]{1,3}\.){3}[0-9]{1,3}/;
        const match = ipRegex.exec(ice.candidate.candidate);
        if (match && match[0] !== '0.0.0.0') {
          resolve(match[0]);
          pc.close();
        }
      };
      setTimeout(() => resolve(null), 3000);
    } catch (e) {
      resolve(null);
    }
  });
}

async function connectToESP32(device) {
  if (simulationMode.value) {
    // Modo simulación
    connected.value = true;
    selectedEsp32.value = { name: "SIMULADOR", address: "simulador" };
    esp32Name.value = "SIMULADOR";
    esp32Ip.value = "simulación";
    startTime.value = Date.now();
    resetCharts();
    reconnectAttempts = 0;
    showMessage("Modo simulación activado", "success");
    setTimeout(() => fetchESP32Data(), 100);
    return;
  }

  if (!networkState.value.isConnected) {
    showMessage("No hay conexion de red", "error");
    return;
  }

  connecting.value = true;
  selectedEsp32.value = device;
  esp32Name.value = device.name;
  esp32Ip.value = device.address;

  try {
    const response = await fetchWithTimeout(
      `http://${device.address}:${ESP32_PORT}/ping`,
      {},
      5000
    );

    if (response.ok) {
      connected.value = true;
      startTime.value = Date.now();
      resetCharts();
      reconnectAttempts = 0;
      
      const savedDevices = localStorage.getItem('esp32_devices') || '[]';
      const devices = JSON.parse(savedDevices);
      if (!devices.find(d => d.address === device.address)) {
        devices.push(device);
        localStorage.setItem('esp32_devices', JSON.stringify(devices));
      }
      localStorage.setItem('esp32_last_connected', device.address);
      
      showMessage(`Conectado a ${device.name}`, "success");
      setTimeout(() => fetchESP32Data(), 100);
    } else {
      showMessage(`${device.name} no responde`, "error");
    }
  } catch (error) {
    showMessage(`No se pudo conectar a ${device.name}`, "error");
  } finally {
    connecting.value = false;
  }
}

function resetCharts() {
  chartDataVel.value = {
    deseado: [],
    obtenido: [],
    timestamps: [],
  };
  chartDataPos.value = {
    setpoint: [],
    obtenido: [],
    timestamps: [],
  };
}

function getX(index, timestamps, width, zoomLevel) {
  const time = timestamps[index];
  const minTime = Math.min(...timestamps);
  const maxTime = Math.max(...timestamps);
  const timeRange = maxTime - minTime || 1;
  return (
    CHART_PADDING +
    ((time - minTime) / timeRange) * (width - 2 * CHART_PADDING) * zoomLevel
  );
}

function getY(value, maxVal, minVal = 0) {
  const range = maxVal - minVal || 1;
  return (
    CHART_HEIGHT -
    CHART_PADDING -
    ((value - minVal) / range) * (CHART_HEIGHT - 2 * CHART_PADDING)
  );
}

function zoomIn(container, followEnabled) {
  zoom.value = Math.min(zoom.value + 0.5, 5);
  if (followEnabled.value && container.value) {
    nextTick(() => {
      container.value.scrollLeft = container.value.scrollWidth;
    });
  }
}

function zoomOut(container, followEnabled) {
  zoom.value = Math.max(zoom.value - 0.5, 1);
  if (followEnabled.value && container.value) {
    nextTick(() => {
      container.value.scrollLeft = container.value.scrollWidth;
    });
  }
}

function resetZoom(container) {
  zoom.value = 1;
  if (container.value) {
    container.value.scrollLeft = 0;
  }
}

function scrollToEnd(containerRef, followEnabled) {
  if (containerRef.value) {
    containerRef.value.scrollLeft = containerRef.value.scrollWidth;
    if (followEnabled) followEnabled.value = true;
  }
}

function scrollToStart(containerRef, followEnabled) {
  if (containerRef.value) {
    containerRef.value.scrollLeft = 0;
    if (followEnabled) followEnabled.value = false;
  }
}

function handleScroll(containerRef, followEnabled) {
  if (!containerRef.value) return;
  const container = containerRef.value;
  const maxScroll = container.scrollWidth - container.clientWidth;
  const isAtEnd = container.scrollLeft >= maxScroll - 10;
  followEnabled.value = isAtEnd;
}

function updateScrollPosition(containerRef, followEnabled) {
  if (followEnabled.value && containerRef.value) {
    containerRef.value.scrollLeft = containerRef.value.scrollWidth;
  }
}

let profileStartTime = 0;
let maxSpeedReachedTime = 0;
let profileEndTime = 0;
let maxSpeedValue = 0;
let positionStartTime = 0;
let positionArrivalTime = 0;
let errorSum = 0;
let errorCount = 0;

const profileMetrics = ref({
  maxSpeed: 0,
  timeToMaxSpeed: 0,
  totalTime: 0,
  error: 0,
  followPercent: 0
});

const positionMetrics = ref({
  target: 0,
  current: 0,
  speed: 0,
  time: 0,
  error: 0
});

// Variables para simulación
let simulatedRpm = 0;
let simulatedPosition = 0;
let simulatedSetpoint = 0;
let profileActiveSim = false;
let profileStartTimeSim = 0;
let profileTargetSpeed = 0;
let profileDuration = 0;

// Función de simulación
function simulateESP32Data() {
  if (!simulationMode.value || !connected.value) return;

  const timeElapsed = (Date.now() - startTime.value) / 1000;

  // Simular respuesta del motor con inercia
  const inertia = 0.85;
  const noise = (Math.random() - 0.5) * 3; // Ruido realista
  
  // Suavizar la respuesta del RPM
  let targetRpm = telemetry.value.setpoint;
  if (perfilActivo.value && profileActiveSim) {
    // Simular perfil trapezoidal
    const elapsed = timeElapsed - profileStartTimeSim;
    if (elapsed <= 0) {
      targetRpm = 0;
    } else if (elapsed <= profileDuration * 0.3) {
      // Aceleración
      targetRpm = (elapsed / (profileDuration * 0.3)) * profileTargetSpeed;
    } else if (elapsed <= profileDuration * 0.7) {
      // Velocidad constante
      targetRpm = profileTargetSpeed;
    } else if (elapsed <= profileDuration) {
      // Desaceleración
      targetRpm = profileTargetSpeed * (1 - (elapsed - profileDuration * 0.7) / (profileDuration * 0.3));
    } else {
      targetRpm = 0;
      perfilActivo.value = false;
      profileActiveSim = false;
    }
    telemetry.value.setpoint = Math.min(120, Math.max(0, Math.round(targetRpm)));
  } else if (!perfilActivo.value) {
    telemetry.value.setpoint = simulatedSetpoint;
  }
  
  // Aplicar inercia
  simulatedRpm = simulatedRpm * inertia + telemetry.value.setpoint * (1 - inertia);
  simulatedRpm = Math.min(120, Math.max(0, simulatedRpm + noise));
  
  // Simular movimiento de posición
  let targetPosition = gradosObjetivo.value;
  if (controlPosicionActivo.value) {
    const positionError = targetPosition - simulatedPosition;
    const positionStep = Math.min(15, Math.abs(positionError) * 0.25) * Math.sign(positionError);
    simulatedPosition += positionStep;
    
    if (Math.abs(simulatedPosition - targetPosition) < 1) {
      simulatedPosition = targetPosition;
      if (positionStartTime > 0 && positionArrivalTime === 0) {
        positionArrivalTime = (Date.now() - positionStartTime) / 1000;
        positionMetrics.value.time = Math.round(positionArrivalTime * 100) / 100;
        controlPosicionActivo.value = false;
        showMessage(`Posicion alcanzada en ${positionMetrics.value.time} s`, "success");
      }
    }
  } else {
    // Mantener posición actual
    simulatedPosition = simulatedPosition;
  }
  
  simulatedPosition = Math.min(360, Math.max(0, simulatedPosition));
  posicionActualGrados.value = simulatedPosition;

  telemetry.value = {
    setpoint: telemetry.value.setpoint,
    rpm: Math.round(simulatedRpm),
    pwm: Math.round(simulatedRpm * 2.55),
    kp: kp.value,
    ki: ki.value,
    kd: kd.value,
    mode: "manual"
  };

  // Actualizar posición
  let currentPosition = simulatedPosition;
  let positionSetpoint = gradosObjetivo.value;
  
  posicionActualGrados.value = currentPosition;
  
  positionMetrics.value.current = Math.round(currentPosition);
  positionMetrics.value.speed = telemetry.value.rpm;
  const targetPos = gradosObjetivo.value % 360;
  let positionError = Math.abs(targetPos - currentPosition);
  if (positionError > 180) positionError = 360 - positionError;
  positionMetrics.value.error = Math.round(positionError * 10) / 10;

  // Actualizar métricas de perfil
  if (perfilActivo.value && telemetry.value.rpm > maxSpeedValue) {
    maxSpeedValue = telemetry.value.rpm;
    profileMetrics.value.maxSpeed = Math.round(maxSpeedValue * 10) / 10;
    if (maxSpeedReachedTime === 0 && maxSpeedValue >= profileVmax.value * 0.95) {
      maxSpeedReachedTime = timeElapsed;
      profileMetrics.value.timeToMaxSpeed = Math.round(maxSpeedReachedTime * 100) / 100;
    }
  }

  const currentError = Math.abs(telemetry.value.setpoint - telemetry.value.rpm);
  
  if (perfilActivo.value || controlPosicionActivo.value) {
    errorSum += currentError;
    errorCount++;
    profileMetrics.value.error = Math.round((errorSum / errorCount) * 10) / 10;
    
    const followPercent = telemetry.value.setpoint > 0 ? (telemetry.value.rpm / telemetry.value.setpoint) * 100 : 0;
    const currentFollowPercent = profileMetrics.value.followPercent;
    const newFollowPercent = (currentFollowPercent * (errorCount - 1) + followPercent) / errorCount;
    profileMetrics.value.followPercent = Math.round(Math.min(100, Math.max(0, newFollowPercent)) * 10) / 10;
  }

  // Actualizar gráficas
  chartDataVel.value = {
    deseado: [...chartDataVel.value.deseado, telemetry.value.setpoint].slice(-MAX_POINTS),
    obtenido: [...chartDataVel.value.obtenido, telemetry.value.rpm].slice(-MAX_POINTS),
    timestamps: [...chartDataVel.value.timestamps, timeElapsed].slice(-MAX_POINTS),
  };
  
  chartDataPos.value = {
    setpoint: [...chartDataPos.value.setpoint, positionSetpoint].slice(-MAX_POINTS),
    obtenido: [...chartDataPos.value.obtenido, Math.round(currentPosition)].slice(-MAX_POINTS),
    timestamps: [...chartDataPos.value.timestamps, timeElapsed].slice(-MAX_POINTS),
  };

  updateScrollPosition(chartScrollContainerVel, followEnabledVel);
  updateScrollPosition(chartScrollContainerPos, followEnabledPos);
}

async function fetchESP32Data() {
  if (simulationMode.value && connected.value) {
    simulateESP32Data();
    return;
  }
  
  if (!connected.value || !esp32Ip.value) return;

  try {
    const response = await fetchWithTimeout(
      `http://${esp32Ip.value}:${ESP32_PORT}/status`
    );

    const data = await response.json();

    telemetry.value = {
      setpoint: data.setpoint || 0,
      rpm: data.rpm || 0,
      pwm: data.pwm || 0,
      kp: data.kp || 0,
      ki: data.ki || 0,
      kd: data.kd || 0,
      mode: data.mode || "manual"
    };

    let currentPosition = 0;
    let positionSetpoint = 0;
    
    if (data.position !== undefined) {
      currentPosition = data.position;
      posicionActualGrados.value = currentPosition;
      
      positionMetrics.value.current = Math.round(currentPosition);
      positionMetrics.value.speed = telemetry.value.rpm;
      const targetPos = gradosObjetivo.value % 360;
      let positionError = Math.abs(targetPos - currentPosition);
      if (positionError > 180) positionError = 360 - positionError;
      positionMetrics.value.error = Math.round(positionError * 10) / 10;
      
      if (controlPosicionActivo.value && Math.abs(currentPosition - targetPos) < 2 && positionStartTime > 0 && positionArrivalTime === 0) {
        positionArrivalTime = (Date.now() - positionStartTime) / 1000;
        positionMetrics.value.time = Math.round(positionArrivalTime * 100) / 100;
        controlPosicionActivo.value = false;
        showMessage(`Posicion alcanzada en ${positionMetrics.value.time} s`, "success");
      }
    }
    
    if (data.positionSetpoint !== undefined) {
      positionSetpoint = data.positionSetpoint;
    }
    
    if (data.positionActive !== undefined) {
      controlPosicionActivo.value = data.positionActive;
      if (data.positionActive && positionStartTime === 0) {
        positionStartTime = Date.now();
        positionArrivalTime = 0;
        positionMetrics.value.target = gradosObjetivo.value;
      } else if (!data.positionActive && positionStartTime > 0 && positionArrivalTime === 0) {
        positionArrivalTime = (Date.now() - positionStartTime) / 1000;
        positionMetrics.value.time = Math.round(positionArrivalTime * 100) / 100;
        positionStartTime = 0;
      }
    }

    const timeElapsed = (Date.now() - startTime.value) / 1000;

    chartDataVel.value = {
      deseado: [...chartDataVel.value.deseado, Math.round(data.setpoint || 0)].slice(-MAX_POINTS),
      obtenido: [...chartDataVel.value.obtenido, Math.round(data.rpm || 0)].slice(-MAX_POINTS),
      timestamps: [...chartDataVel.value.timestamps, timeElapsed].slice(-MAX_POINTS),
    };
    
    const currentError = Math.abs(telemetry.value.setpoint - telemetry.value.rpm);
    
    if (perfilActivo.value || controlPosicionActivo.value) {
      errorSum += currentError;
      errorCount++;
      profileMetrics.value.error = Math.round((errorSum / errorCount) * 10) / 10;
      
      const followPercent = telemetry.value.setpoint > 0 ? (telemetry.value.rpm / telemetry.value.setpoint) * 100 : 0;
      const currentFollowPercent = profileMetrics.value.followPercent;
      const newFollowPercent = (currentFollowPercent * (errorCount - 1) + followPercent) / errorCount;
      profileMetrics.value.followPercent = Math.round(Math.min(100, Math.max(0, newFollowPercent)) * 10) / 10;
    }
    
    if (perfilActivo.value && telemetry.value.rpm > maxSpeedValue) {
      maxSpeedValue = telemetry.value.rpm;
      profileMetrics.value.maxSpeed = Math.round(maxSpeedValue * 10) / 10;
      if (maxSpeedReachedTime === 0 && maxSpeedValue >= profileVmax.value * 0.95) {
        maxSpeedReachedTime = timeElapsed;
        profileMetrics.value.timeToMaxSpeed = Math.round(maxSpeedReachedTime * 100) / 100;
      }
    }
    
    // Grafica de posicion con setpoint (curva S)
    chartDataPos.value = {
      setpoint: [...chartDataPos.value.setpoint, Math.round(positionSetpoint)].slice(-MAX_POINTS),
      obtenido: [...chartDataPos.value.obtenido, Math.round(currentPosition)].slice(-MAX_POINTS),
      timestamps: [...chartDataPos.value.timestamps, timeElapsed].slice(-MAX_POINTS),
    };

    updateScrollPosition(chartScrollContainerVel, followEnabledVel);
    updateScrollPosition(chartScrollContainerPos, followEnabledPos);
    
    reconnectAttempts = 0;

  } catch (error) {
    reconnectAttempts++;
    if (reconnectAttempts >= 5) {
      connected.value = false;
      stopPolling();
      showMessage("Conexion perdida - Reconectando...", "warning");
      setTimeout(() => {
        if (esp32Ip.value) {
          connectToESP32({name: esp32Name.value, address: esp32Ip.value});
        }
      }, 3000);
    }
  }
}

function startPolling() {
  stopPolling();
  fetchESP32Data();
  pollInterval.value = setInterval(fetchESP32Data, 500);
}

function stopPolling() {
  if (pollInterval.value) {
    clearInterval(pollInterval.value);
    pollInterval.value = null;
  }
}

async function checkNetworkStatus() {
  try {
    networkState.value = {
      type: "web",
      isConnected: navigator.onLine,
    };
    ipAddress.value = window.location.hostname || "localhost";
  } catch (error) {
    console.error("Error:", error);
  }
}

function disconnect() {
  connected.value = false;
  selectedEsp32.value = null;
  stopPolling();
  errorSum = 0;
  errorCount = 0;
  showMessage("Desconectado", "info");
}

async function sendRPM(value) {
  if (!connected.value) {
    showMessage("No hay conexion", "error");
    return;
  }

  const numeric = Number(value);
  const rpmValue = Math.min(120, Math.max(0, isNaN(numeric) ? 0 : numeric));

  rpm.value = rpmValue;
  inputRpm.value = Math.round(rpmValue).toString();
  simulatedSetpoint = rpmValue;
  
  if (simulationMode.value) {
    showMessage(`RPM enviado (simulación): ${Math.round(rpmValue)}`, "success");
    return;
  }

  try {
    await fetchWithTimeout(
      `http://${esp32Ip.value}:${ESP32_PORT}/rpm?value=${Math.round(rpmValue)}`,
      {},
      10000
    );

    showMessage(`RPM enviado: ${Math.round(rpmValue)}`, "success");
  } catch (error) {
    showMessage("Error al enviar RPM", "error");
  }
}

function handleInputChange(event) {
  let value = event.target.value.replace(/[^\d]/g, "").slice(0, 3);
  if (parseInt(value) > 120) value = "120";
  inputRpm.value = value;
}

function handleInputSubmit() {
  const value = parseInt(inputRpm.value, 10);
  if (!isNaN(value)) {
    sendRPM(value);
  }
}

async function emergencyStop() {
  await sendRPM(0);
  showMessage("PARO DE EMERGENCIA", "warning");
}

async function sendPID(){
  if(!connected.value){
    showMessage("No conectado","error");
    return;
  }

  if(simulationMode.value){
    showMessage("PID actualizado (simulación)","success");
    return;
  }

  try{
    await fetch(
      `http://${esp32Ip.value}:${ESP32_PORT}/pid?kp=${kp.value}&ki=${ki.value}`
    );
    showMessage("PID actualizado","success");
  }catch(e){
    showMessage("Error enviando PID","error");
  }
}

async function configProfile(){
  if(!connected.value){
    showMessage("No conectado","error");
    return;
  }

  if(simulationMode.value){
    showMessage("Perfil configurado (simulación)","success");
    return;
  }

  try{
    await fetch(
      `http://${esp32Ip.value}:${ESP32_PORT}/profile/config?vmax=${profileVmax.value}&t=${profileTime.value}`
    );
    showMessage("Perfil configurado","success");
  }catch(e){
    showMessage("Error configurando perfil","error");
  }
}

async function configProfilePosition(){
  if(!connected.value){
    showMessage("No conectado","error");
    return;
  }

  if(simulationMode.value){
    showMessage("Tiempo de posicion actualizado (simulación)","success");
    return;
  }

  try{
    await fetch(
      `http://${esp32Ip.value}:${ESP32_PORT}/profile/position/config?tiempo=${profilePositionTime.value}`
    );
    showMessage("Tiempo de posicion actualizado","success");
  }catch(e){
    showMessage("Error configurando tiempo","error");
  }
}

async function startProfile(){
  errorSum = 0;
  errorCount = 0;
  profileMetrics.value.error = 0;
  profileMetrics.value.followPercent = 0;
  perfilActivo.value = true;
  maxSpeedReachedTime = 0;
  maxSpeedValue = 0;
  profileStartTime = Date.now() / 1000;
  profileMetrics.value.timeToMaxSpeed = 0;
  profileMetrics.value.maxSpeed = 0;
  
  if (simulationMode.value) {
    // Simular perfil
    profileActiveSim = true;
    profileStartTimeSim = Date.now() / 1000;
    profileTargetSpeed = profileVmax.value;
    profileDuration = profileTime.value;
    showMessage("Perfil iniciado (simulación)", "success");
    
    setTimeout(() => {
      if (perfilActivo.value) {
        profileEndTime = (Date.now() / 1000) - profileStartTime;
        profileMetrics.value.totalTime = Math.round(profileEndTime * 100) / 100;
        perfilActivo.value = false;
        profileActiveSim = false;
      }
    }, profileTime.value * 1000);
    return;
  }
  
  await fetch(`http://${esp32Ip.value}:${ESP32_PORT}/profile/start`);
  showMessage("Perfil iniciado", "success");
  
  setTimeout(() => {
    if (perfilActivo.value) {
      profileEndTime = (Date.now() / 1000) - profileStartTime;
      profileMetrics.value.totalTime = Math.round(profileEndTime * 100) / 100;
      perfilActivo.value = false;
    }
  }, profileTime.value * 1000);
}

async function stopProfile() {
  if(!connected.value){
    showMessage("No conectado","error");
    return;
  }
  
  if(simulationMode.value){
    perfilActivo.value = false;
    profileActiveSim = false;
    profileEndTime = (Date.now() / 1000) - profileStartTime;
    profileMetrics.value.totalTime = Math.round(profileEndTime * 100) / 100;
    showMessage("Perfil detenido (simulación)", "info");
    return;
  }
  
  try {
    await fetch(`http://${esp32Ip.value}:${ESP32_PORT}/profile/stop`);
    perfilActivo.value = false;
    profileEndTime = (Date.now() / 1000) - profileStartTime;
    profileMetrics.value.totalTime = Math.round(profileEndTime * 100) / 100;
    showMessage("Perfil detenido", "info");
  } catch(e) {
    showMessage("Error deteniendo perfil","error");
  }
}

const gradosObjetivo = ref(180);
const controlPosicionActivo = ref(false);
const posicionActualGrados = ref(0);
const perfilActivo = ref(false);

const progresoPosicion = computed(() => {
  if (gradosObjetivo.value > 0) {
    return Math.min(posicionActualGrados.value / gradosObjetivo.value, 1);
  }
  return 0;
});

function getAnguloRadians(grados) {
  return (grados - 90) * Math.PI / 180;
}

async function iniciarMovimientoPosicion() {
  if (!connected.value) {
    showMessage("No conectado", "error");
    return;
  }
  
  errorSum = 0;
  errorCount = 0;
  profileMetrics.value.error = 0;
  profileMetrics.value.followPercent = 0;
  
  if(simulationMode.value){
    controlPosicionActivo.value = true;
    positionStartTime = Date.now();
    positionArrivalTime = 0;
    positionMetrics.value.target = gradosObjetivo.value;
    positionMetrics.value.time = 0;
    showMessage(`Moviendo a ${gradosObjetivo.value}° (simulación)`, "success");
    return;
  }
  
  try {
    const response = await fetchWithTimeout(
      `http://${esp32Ip.value}:${ESP32_PORT}/position?grados=${gradosObjetivo.value}`,
      {},
      5000
    );
    
    if (response.ok) {
      controlPosicionActivo.value = true;
      positionStartTime = Date.now();
      positionArrivalTime = 0;
      positionMetrics.value.target = gradosObjetivo.value;
      positionMetrics.value.time = 0;
      showMessage(`Moviendo a ${gradosObjetivo.value}°`, "success");
    } else {
      showMessage("Error en el comando", "error");
    }
  } catch (e) {
    showMessage("Error conectando", "error");
  }
}

async function regresarACero() {
  if (!connected.value) {
    showMessage("No conectado", "error");
    return;
  }
  
  gradosObjetivo.value = 0;
  
  if(simulationMode.value){
    controlPosicionActivo.value = true;
    positionStartTime = Date.now();
    positionArrivalTime = 0;
    positionMetrics.value.target = 0;
    positionMetrics.value.time = 0;
    showMessage("Regresando a 0° (simulación)", "success");
    return;
  }
  
  try {
    const response = await fetchWithTimeout(
      `http://${esp32Ip.value}:${ESP32_PORT}/position?grados=0`,
      {},
      5000
    );
    
    if (response.ok) {
      controlPosicionActivo.value = true;
      positionStartTime = Date.now();
      positionArrivalTime = 0;
      positionMetrics.value.target = 0;
      positionMetrics.value.time = 0;
      showMessage("Regresando a 0°", "success");
    } else {
      showMessage("Error en el comando", "error");
    }
  } catch (e) {
    showMessage("Error conectando", "error");
  }
}

async function detenerMovimientoPosicion() {
  if(simulationMode.value){
    controlPosicionActivo.value = false;
    positionStartTime = 0;
    showMessage("Movimiento detenido (simulación)", "info");
    return;
  }
  
  try {
    await fetchWithTimeout(
      `http://${esp32Ip.value}:${ESP32_PORT}/position/stop`,
      {},
      5000
    );
    controlPosicionActivo.value = false;
    positionStartTime = 0;
    showMessage("Movimiento detenido", "info");
  } catch (e) {
    showMessage("Error deteniendo movimiento", "error");
  }
}

async function establecerCeroAbsoluto() {
  if (!connected.value) {
    showMessage("No conectado", "error");
    return;
  }
  
  if(simulationMode.value){
    showMessage("Cero calibrado (simulación)", "success");
    gradosObjetivo.value = 0;
    posicionActualGrados.value = 0;
    simulatedPosition = 0;
    return;
  }
  
  try {
    await fetch(`http://${esp32Ip.value}:${ESP32_PORT}/set_zero`);
    showMessage("Cero calibrado", "success");
    gradosObjetivo.value = 0;
    posicionActualGrados.value = 0;
  } catch (e) {
    showMessage("Error calibrando", "error");
  }
}

// Activar/desactivar simulación
function toggleSimulation() {
  if (connected.value) {
    disconnect();
    setTimeout(() => {
      simulationMode.value = !simulationMode.value;
      if (simulationMode.value) {
        // Inicializar variables de simulación
        simulatedRpm = 0;
        simulatedPosition = 0;
        simulatedSetpoint = 0;
        connectToESP32({ name: "SIMULADOR", address: "simulador" });
        showMessage("Modo simulación activado - Puedes probar sin hardware", "success");
      } else {
        showMessage("Modo simulación desactivado", "info");
      }
    }, 100);
  } else {
    simulationMode.value = !simulationMode.value;
    if (simulationMode.value) {
      simulatedRpm = 0;
      simulatedPosition = 0;
      simulatedSetpoint = 0;
      connectToESP32({ name: "SIMULADOR", address: "simulador" });
      showMessage("Modo simulación activado - Puedes probar sin hardware", "success");
    } else {
      showMessage("Modo simulación desactivado - Conecta un ESP32 real", "info");
    }
  }
}

function updateSliderByClientX(clientX, element) {
  if (!element) return;

  const rect = element.getBoundingClientRect();
  const x = clientX - rect.left;
  const newRpm = Math.min(120, Math.max(0, (x / sliderWidth) * 120));

  rpm.value = newRpm;
  inputRpm.value = Math.round(newRpm).toString();
}

function onSliderMouseDown(event) {
  if (!connected.value) return;

  sliderDragging.value = true;
  scrollEnabled.value = false;
  updateSliderByClientX(event.clientX, event.currentTarget);

  const move = (e) => {
    updateSliderByClientX(
      e.clientX,
      document.querySelector(".slider-touch-area")
    );
  };

  const up = async () => {
    window.removeEventListener("mousemove", move);
    window.removeEventListener("mouseup", up);
    sliderDragging.value = false;
    scrollEnabled.value = true;
    await sendRPM(rpm.value);
  };

  window.addEventListener("mousemove", move);
  window.addEventListener("mouseup", up);
}

function onSliderTouchStart(event) {
  if (!connected.value) return;

  sliderDragging.value = true;
  scrollEnabled.value = false;

  const touch = event.touches[0];
  updateSliderByClientX(touch.clientX, event.currentTarget);

  const move = (e) => {
    if (e.cancelable) e.preventDefault();
    const currentTouch = e.touches[0];
    updateSliderByClientX(
      currentTouch.clientX,
      document.querySelector(".slider-touch-area")
    );
  };

  const end = async () => {
    window.removeEventListener("touchmove", move);
    window.removeEventListener("touchend", end);
    window.removeEventListener("touchcancel", cancel);
    sliderDragging.value = false;
    scrollEnabled.value = true;
    await sendRPM(rpm.value);
  };

  const cancel = () => {
    window.removeEventListener("touchmove", move);
    window.removeEventListener("touchend", end);
    window.removeEventListener("touchcancel", cancel);
    sliderDragging.value = false;
    scrollEnabled.value = true;
  };

  window.addEventListener("touchmove", move, { passive: false });
  window.addEventListener("touchend", end);
  window.addEventListener("touchcancel", cancel);
}

const chartReadyVel = computed(() => chartDataVel.value.deseado.length >= 2);
const chartReadyPos = computed(() => chartDataPos.value.setpoint.length >= 2);

const maxRPM = computed(() =>
  Math.max(120, ...chartDataVel.value.deseado, ...chartDataVel.value.obtenido, 0)
);

const maxGrados = 360;

const deseadoPoints = computed(() =>
  chartDataVel.value.deseado.map((value, index) => ({
    x: getX(index, chartDataVel.value.timestamps, chartWidth.value, zoom.value),
    y: getY(value, maxRPM.value),
  }))
);

const obtenidoPointsVel = computed(() =>
  chartDataVel.value.obtenido.map((value, index) => ({
    x: getX(index, chartDataVel.value.timestamps, chartWidth.value, zoom.value),
    y: getY(value, maxRPM.value),
  }))
);

const setpointPointsPos = computed(() =>
  chartDataPos.value.setpoint.map((value, index) => ({
    x: getX(index, chartDataPos.value.timestamps, chartWidth.value, zoom.value),
    y: getY(value, maxGrados),
  }))
);

const obtenidoPointsPos = computed(() =>
  chartDataPos.value.obtenido.map((value, index) => ({
    x: getX(index, chartDataPos.value.timestamps, chartWidth.value, zoom.value),
    y: getY(value, maxGrados),
  }))
);

function handleOnline() { checkNetworkStatus(); }
function handleOffline() { checkNetworkStatus(); }

watch(() => connected.value, async (value) => {
  await nextTick();
  if (value) { startPolling(); } 
  else { stopPolling(); }
});

onMounted(() => {
  checkNetworkStatus();
  window.addEventListener("online", handleOnline);
  window.addEventListener("offline", handleOffline);
  
  const lastConnected = localStorage.getItem('esp32_last_connected');
  if (lastConnected) {
    esp32Ip.value = lastConnected;
  }
});

onBeforeUnmount(() => {
  stopPolling();
  window.removeEventListener("online", handleOnline);
  window.removeEventListener("offline", handleOffline);
  if (messageTimeout) clearTimeout(messageTimeout);
});
</script>

<template>
  <div class="app" :class="{ 'no-scroll': !scrollEnabled, 'simulation-mode': simulationMode }">
    <transition name="message">
      <div v-if="connectionMessage.visible" class="floating-message" :class="'message-' + connectionMessage.type" @click="hideMessage">
        {{ connectionMessage.text }}
      </div>
    </transition>

    <div v-if="connected" class="floating-nav">
      <button class="floating-nav-btn" :class="{ active: activeProfile === 'rpm' }" @click="activeProfile = 'rpm'">
        <span class="nav-icon">RPM</span>
      </button>
      <button class="floating-nav-btn" :class="{ active: activeProfile === 'profile' }" @click="activeProfile = 'profile'">
        <span class="nav-text">Perfil</span>
      </button>
      <button class="floating-nav-btn" :class="{ active: activeProfile === 'position' }" @click="activeProfile = 'position'">
        <span class="nav-text">Posicion</span>
      </button>
    </div>

    <div v-if="connected" class="fixed-button-bar">
      <button class="fixed-emergency-button" @click="emergencyStop">
        <span>EMERGENCIA</span>
      </button>
      <button class="fixed-calibration-button" @click="establecerCeroAbsoluto">
        <span>CALIBRAR CERO</span>
      </button>
    </div>

    <main class="scroll-view" :class="{ 'with-fixed-bar': connected }">
      <div class="top-section">
        <section class="header">
          <div class="title-wrapper">
            <h1 class="title">Control Motor DC</h1>
            <span v-if="simulationMode" class="simulation-badge">🎮 SIMULACIÓN</span>
          </div>
          <div class="header-buttons">
            <button class="simulation-button" :class="{ active: simulationMode }" @click="toggleSimulation">
              <span class="simulation-icon">🎮</span>
              <span>{{ simulationMode ? 'Desactivar Simulación' : 'Activar Simulación' }}</span>
            </button>
            <button class="settings-button" @click="showConfig = !showConfig">
              <span class="settings-icon">⚙️</span>
              <span>Ajustes</span>
            </button>
          </div>
        </section>

        <section class="status-bar">
          <div class="status-item" :class="{ 'status-online': networkState.isConnected, 'status-offline': !networkState.isConnected }">
            <span class="status-dot"></span>
            <span class="status-text">Red: {{ networkState.isConnected ? 'Conectado' : 'Desconectado' }}</span>
          </div>
          <div v-if="connected" class="status-item" :class="{ 'status-simulation': simulationMode }">
            <span class="status-dot" :class="{ 'status-dot-green': !simulationMode, 'status-dot-simulation': simulationMode }"></span>
            <span class="status-text">Conectado a:</span>
            <span class="status-ip">{{ esp32Name }} ({{ esp32Ip }})</span>
          </div>
        </section>

        <transition name="slide">
          <section v-if="showConfig" class="config-panel">
            <div class="config-card">
              <div class="config-header">
                
                <h3>Configuración de conexión</h3>
              </div>
              <div class="config-body">
                <div v-if="!simulationMode && esp32List.length > 0" class="devices-list">
                  <label>ESP32 disponibles:</label>
                  <div class="devices-grid">
                    <div v-for="device in esp32List" :key="device.address" class="device-card" :class="{ 'selected': selectedEsp32 === device }" @click="connectToESP32(device)">
                      <div class="device-icon">ESP</div>
                      <div class="device-info">
                        <div class="device-name">{{ device.name }}</div>
                        <div class="device-ip">{{ device.address }}</div>
                      </div>
                      <div class="device-status">●</div>
                    </div>
                  </div>
                </div>
                
                <div v-if="!simulationMode" class="action-buttons-config">
                  <button class="scan-btn" @click="discoverESP32s" :disabled="discovering">
                    <span v-if="discovering" class="spinner"></span>
                   
                    {{ discovering ? ' Escaneando...' : ' Buscar ESP32' }}
                  </button>
                </div>
                
                <div class="input-group" v-if="!connected && !simulationMode">
                  <label>O conectar manualmente por IP</label>
                  <input v-model="esp32Ip" class="input" type="text" placeholder="Ej: 192.168.1.100">
                  <button class="connect-btn" @click="connectToESP32({name: 'Manual', address: esp32Ip})" :disabled="connecting">
                    Conectar
                  </button>
                </div>

                <div v-if="!connected && simulationMode" class="simulation-info">
                  <div class="info-card-sim">
                    <span class="sim-icon">🎮</span>
                    <div>
                      <strong>Modo simulación activo</strong>
                      <p>Puedes probar todas las funciones sin necesidad de hardware real. Los datos se generan automáticamente.</p>
                    </div>
                  </div>
                  <button class="connect-btn" @click="connectToESP32({name: 'SIMULADOR', address: 'simulador'})">
                    Iniciar simulación
                  </button>
                </div>
              </div>
            </div>
          </section>
        </transition>
      </div>

      <template v-if="connected">
        <transition name="fade-slide" mode="out-in">
          <div v-if="activeProfile === 'rpm'" key="rpm" class="profile-container">
            <div class="profile-header-single">
              <h2>Control de RPM</h2>
              <div class="profile-badge">0 - 120 RPM</div>
            </div>
            
            <div class="profile-content-single">
              <div class="slider-section">
                <div class="slider-touch-area" @mousedown="onSliderMouseDown" @touchstart="onSliderTouchStart">
                  <div class="slider-track">
                    <div class="slider-fill" :style="{ width: (rpm / 120) * sliderWidth + 'px' }"></div>
                    <div class="slider-thumb" :style="{ left: (rpm / 120) * sliderWidth - 13 + 'px' }"></div>
                  </div>
                </div>
                <div class="rpm-input-group">
                  <span>RPM:</span>
                  <input class="rpm-input" :value="inputRpm" @input="handleInputChange" @keyup.enter="handleInputSubmit">
                  <button class="send-btn" @click="handleInputSubmit">Enviar</button>
                </div>
              </div>
              
              <div class="quick-buttons">
                <button v-for="val in [0,20,40,60,80,100,120]" :key="val" class="quick-btn" :class="{ 'stop-btn': val === 0 }" @click="sendRPM(val)">{{ val }}</button>
              </div>
              
              <div class="dashboard">
                <h3>Desempeño del Sistema</h3>
                <div class="dashboard-grid">
                  <div class="dashboard-item">
                    <span class="dashboard-label">RPM Objetivo</span>
                    <span class="dashboard-value">{{ Math.round(telemetry.setpoint) }} RPM</span>
                  </div>
                  <div class="dashboard-item">
                    <span class="dashboard-label">RPM Real</span>
                    <span class="dashboard-value">{{ Math.round(telemetry.rpm) }} RPM</span>
                  </div>
                  <div class="dashboard-item">
                    <span class="dashboard-label">Error</span>
                    <span class="dashboard-value">{{ Math.round(Math.abs(telemetry.setpoint - telemetry.rpm)) }}</span>
                  </div>
                  <div class="dashboard-item">
                    <span class="dashboard-label">Seguimiento</span>
                    <span class="dashboard-value">{{ Math.round((telemetry.rpm / Math.max(telemetry.setpoint, 1)) * 100) }}%</span>
                  </div>
                </div>
              </div>

              <div class="pid-section">
                <h3>Control PID Velocidad</h3>
                <div class="pid-inputs">
                  <div><label>Kp</label><input v-model="kp" class="input-small" type="number" step="0.1"></div>
                  <div><label>Ki</label><input v-model="ki" class="input-small" type="number" step="0.1"></div>
                  <div><label>Kd</label><input v-model="kd" class="input-small" type="number" step="0.1"></div>
                </div>
                <button class="action-btn update-btn" @click="sendPID">Actualizar PID</button>
              </div>
              
              <div class="chart-section">
                <div class="chart-header">
                  <span>Respuesta de velocidad (RPM vs Tiempo)</span>
                  <div class="chart-controls">
                    <button class="chart-btn" @click="zoomOut(chartScrollContainerVel, followEnabledVel)">Zoom -</button>
                    <button class="chart-btn" @click="zoomIn(chartScrollContainerVel, followEnabledVel)">Zoom +</button>
                    <button class="chart-btn" @click="resetZoom(chartScrollContainerVel)">Reset</button>
                  </div>
                </div>
                <div class="chart-scroll-container" ref="chartScrollContainerVel" @scroll="handleScroll(chartScrollContainerVel, followEnabledVel)">
                  <div class="chart-inner" :style="{ width: chartInnerWidth + 'px' }">
                    <div v-if="!chartReadyVel" class="chart-placeholder">Esperando datos...</div>
                    <div v-else class="chart-container">
                      <svg :width="chartWidth * zoom" :height="CHART_HEIGHT">
                        <line :x1="CHART_PADDING" :y1="CHART_PADDING" :x2="CHART_PADDING" :y2="CHART_HEIGHT - CHART_PADDING" stroke="#94a3b8" stroke-width="1" />
                        <line :x1="CHART_PADDING" :y1="CHART_HEIGHT - CHART_PADDING" :x2="chartWidth * zoom - CHART_PADDING" :y2="CHART_HEIGHT - CHART_PADDING" stroke="#94a3b8" stroke-width="1" />
                        
                        <template v-for="(point, index) in deseadoPoints" :key="'d'+index">
                          <line v-if="index>0" :x1="deseadoPoints[index-1].x" :y1="deseadoPoints[index-1].y" :x2="point.x" :y2="point.y" stroke="#3b82f6" stroke-width="2" />
                        </template>
                        <template v-for="(point, index) in obtenidoPointsVel" :key="'o'+index">
                          <line v-if="index>0" :x1="obtenidoPointsVel[index-1].x" :y1="obtenidoPointsVel[index-1].y" :x2="point.x" :y2="point.y" stroke="#f97316" stroke-width="2" />
                        </template>
                        
                        <circle v-for="(point, index) in deseadoPoints" :key="'dc'+index" :cx="point.x" :cy="point.y" r="2" fill="#3b82f6" />
                        <circle v-for="(point, index) in obtenidoPointsVel" :key="'oc'+index" :cx="point.x" :cy="point.y" r="2" fill="#f97316" />
                      </svg>
                      <div class="legend">
                        <span class="legend-line blue"></span><span>Deseado</span>
                        <span class="legend-line orange"></span><span>Real</span>
                      </div>
                    </div>
                  </div>
                </div>
                <div class="scroll-buttons">
                  <button class="scroll-btn" @click="scrollToStart(chartScrollContainerVel, followEnabledVel)">Inicio</button>
                  <button class="scroll-btn" @click="scrollToEnd(chartScrollContainerVel, followEnabledVel)">Fin</button>
                </div>
              </div>
            </div>
          </div>
        </transition>

        <transition name="fade-slide" mode="out-in">
          <div v-if="activeProfile === 'profile'" key="profile" class="profile-container">
            <div class="profile-header-single">
              <h2>Perfil de Velocidad</h2>
              <div class="profile-badge">Perfil trapezoidal</div>
            </div>
            
            <div class="profile-content-single">
              <div class="config-section">
                <div class="config-row">
                  <div class="config-field">
                    <label>Velocidad maxima (RPM)</label>
                    <input v-model="profileVmax" class="input" type="number" min="0" max="120">
                  </div>
                  <div class="config-field">
                    <label>Tiempo total (segundos)</label>
                    <input v-model="profileTime" class="input" type="number" min="0.1" step="0.5">
                  </div>
                </div>
                <div class="action-buttons">
                  <button class="action-btn config-btn" @click="configProfile">Actualizar Perfil</button>
                  <button class="action-btn start-btn" @click="startProfile">Iniciar Perfil</button>
                  <button v-if="perfilActivo" class="action-btn stop-btn" @click="stopProfile">Detener Perfil</button>
                </div>
              </div>
              
              <div class="dashboard">
                <h3>Desempeño del Perfil</h3>
                <div class="dashboard-grid">
                  <div class="dashboard-item">
                    <span class="dashboard-label">Velocidad maxima</span>
                    <span class="dashboard-value">{{ profileMetrics.maxSpeed }} RPM</span>
                  </div>
                  <div class="dashboard-item">
                    <span class="dashboard-label">Tiempo a Vmax</span>
                    <span class="dashboard-value">{{ profileMetrics.timeToMaxSpeed }} s</span>
                  </div>
                  <div class="dashboard-item">
                    <span class="dashboard-label">Tiempo total</span>
                    <span class="dashboard-value">{{ profileMetrics.totalTime }} s</span>
                  </div>
                  <div class="dashboard-item">
                    <span class="dashboard-label">Error promedio</span>
                    <span class="dashboard-value">{{ profileMetrics.error }} RPM</span>
                  </div>
                  <div class="dashboard-item">
                    <span class="dashboard-label">Seguimiento promedio</span>
                    <span class="dashboard-value">{{ profileMetrics.followPercent }}%</span>
                  </div>
                </div>
              </div>
              
              <div class="chart-section">
                <div class="chart-header">
                  <span>Respuesta de velocidad (RPM vs Tiempo)</span>
                  <div class="chart-controls">
                    <button class="chart-btn" @click="zoomOut(chartScrollContainerVel, followEnabledVel)">Zoom -</button>
                    <button class="chart-btn" @click="zoomIn(chartScrollContainerVel, followEnabledVel)">Zoom +</button>
                    <button class="chart-btn" @click="resetZoom(chartScrollContainerVel)">Reset</button>
                  </div>
                </div>
                <div class="chart-scroll-container" ref="chartScrollContainerVel" @scroll="handleScroll(chartScrollContainerVel, followEnabledVel)">
                  <div class="chart-inner" :style="{ width: chartInnerWidth + 'px' }">
                    <div v-if="!chartReadyVel" class="chart-placeholder">Esperando datos...</div>
                    <div v-else class="chart-container">
                      <svg :width="chartWidth * zoom" :height="CHART_HEIGHT">
                        <line :x1="CHART_PADDING" :y1="CHART_PADDING" :x2="CHART_PADDING" :y2="CHART_HEIGHT - CHART_PADDING" stroke="#94a3b8" stroke-width="1" />
                        <line :x1="CHART_PADDING" :y1="CHART_HEIGHT - CHART_PADDING" :x2="chartWidth * zoom - CHART_PADDING" :y2="CHART_HEIGHT - CHART_PADDING" stroke="#94a3b8" stroke-width="1" />
                        
                        <template v-for="(point, index) in deseadoPoints" :key="'d'+index">
                          <line v-if="index>0" :x1="deseadoPoints[index-1].x" :y1="deseadoPoints[index-1].y" :x2="point.x" :y2="point.y" stroke="#3b82f6" stroke-width="2" />
                        </template>
                        <template v-for="(point, index) in obtenidoPointsVel" :key="'o'+index">
                          <line v-if="index>0" :x1="obtenidoPointsVel[index-1].x" :y1="obtenidoPointsVel[index-1].y" :x2="point.x" :y2="point.y" stroke="#f97316" stroke-width="2" />
                        </template>
                        
                        <circle v-for="(point, index) in deseadoPoints" :key="'dc'+index" :cx="point.x" :cy="point.y" r="2" fill="#3b82f6" />
                        <circle v-for="(point, index) in obtenidoPointsVel" :key="'oc'+index" :cx="point.x" :cy="point.y" r="2" fill="#f97316" />
                      </svg>
                      <div class="legend">
                        <span class="legend-line blue"></span><span>Deseado</span>
                        <span class="legend-line orange"></span><span>Real</span>
                      </div>
                    </div>
                  </div>
                </div>
                <div class="scroll-buttons">
                  <button class="scroll-btn" @click="scrollToStart(chartScrollContainerVel, followEnabledVel)">Inicio</button>
                  <button class="scroll-btn" @click="scrollToEnd(chartScrollContainerVel, followEnabledVel)">Fin</button>
                </div>
              </div>
            </div>
          </div>
        </transition>

        <transition name="fade-slide" mode="out-in">
          <div v-if="activeProfile === 'position'" key="position" class="profile-container">
            <div class="profile-header-single">
              <h2>Control de Posición</h2>
              <div class="profile-badge">0° - 360°</div>
            </div>
            
            <div class="profile-content-single">
              <div class="angle-section">
                <label>Grados objetivo</label>
                <div class="angle-control-group">
                  <div class="angle-input-wrapper">
                    <input v-model.number="gradosObjetivo" type="number" min="0" max="360" step="15" class="angle-input">
                    <span class="angle-unit">°</span>
                  </div>
                  <div class="angle-buttons">
                    <button class="angle-btn" @click="gradosObjetivo = 0">0°</button>
                    <button class="angle-btn" @click="gradosObjetivo = 90">90°</button>
                    <button class="angle-btn" @click="gradosObjetivo = 180">180°</button>
                    <button class="angle-btn" @click="gradosObjetivo = 270">270°</button>
                    <button class="angle-btn" @click="gradosObjetivo = 360">360°</button>
                  </div>
                </div>
              </div>
              
           
              
             <!-- Speedometer Circular con 0° a la izquierda (donde estaba 180°) -->
<div class="speedometer-container">
  <div class="speedometer">
    <svg viewBox="0 0 220 220" class="gauge">
      <!-- Circulo de fondo -->
      <circle cx="110" cy="110" r="95" fill="#f1f5f9" stroke="#e2e8f0" stroke-width="12"/>
      
      <!-- Arco de progreso - 0° empieza a la izquierda -->
      <circle cx="110" cy="110" r="95" fill="none" stroke="#3b82f6" stroke-width="12" 
        stroke-dasharray="596.9" 
        :stroke-dashoffset="596.9 * (1 - posicionActualGrados / 360)"
        stroke-linecap="round"
        transform="rotate(-180 110 110)"/>
      
      <!-- Marcas cada 30 grados - 0° a la izquierda -->
      <line v-for="angle in [0,30,60,90,120,150,180,210,240,270,300,330]" 
        :key="angle"
        :x1="110 + 85 * Math.cos((angle - 180) * Math.PI / 180)"
        :y1="110 + 85 * Math.sin((angle - 180) * Math.PI / 180)"
        :x2="110 + 75 * Math.cos((angle - 180) * Math.PI / 180)"
        :y2="110 + 75 * Math.sin((angle - 180) * Math.PI / 180)"
        stroke="#94a3b8" stroke-width="3"/>
      
      <!-- Numeros - 0° a la izquierda -->
      <text v-for="angle in [0,30,60,90,120,150,180,210,240,270,300,330]" 
        :key="'t'+angle"
        :x="110 + 65 * Math.cos((angle - 180) * Math.PI / 180)"
        :y="110 + 65 * Math.sin((angle - 180) * Math.PI / 180) + 5"
        text-anchor="middle" font-size="10" fill="#475569">
        {{ angle }}°
      </text>
      
      <!-- Flecha indicadora - 0° a la izquierda -->
      <line :x1="110" :y1="110" 
        :x2="110 + 70 * Math.cos((posicionActualGrados - 180) * Math.PI / 180)" 
        :y2="110 + 70 * Math.sin((posicionActualGrados - 180) * Math.PI / 180)" 
        stroke="#ef4444" stroke-width="4" stroke-linecap="round"/>
      
      <!-- Centro -->
      <circle cx="110" cy="110" r="10" fill="#1e293b"/>
      <circle cx="110" cy="110" r="5" fill="#3b82f6"/>
      
      <!-- Valor actual -->
      <text x="110" y="210" text-anchor="middle" font-size="20" font-weight="800" fill="#0f172a">
        {{ Math.round(posicionActualGrados) }}°
      </text>
      <text x="110" y="220" text-anchor="middle" font-size="10" fill="#64748b">posición actual</text>
    </svg>
  </div>
</div>
              <div class="dashboard">
                <h3>Desempeño de Posición</h3>
                <div class="dashboard-grid">
                  <div class="dashboard-item">
                    <span class="dashboard-label">Grado objetivo</span>
                    <span class="dashboard-value">{{ gradosObjetivo }}°</span>
                  </div>
                  <div class="dashboard-item">
                    <span class="dashboard-label">Grados reales</span>
                    <span class="dashboard-value">{{ Math.round(posicionActualGrados) }}°</span>
                  </div>
                
                  <div class="dashboard-item">
                    <span class="dashboard-label">Tiempo</span>
                    <span class="dashboard-value">{{ positionMetrics.time }} s</span>
                  </div>
                  <div class="dashboard-item">
                    <span class="dashboard-label">Error</span>
                    <span class="dashboard-value">{{ positionMetrics.error }}°</span>
                  </div>
                </div>
              </div>
              
              <div v-if="controlPosicionActivo" class="progress-section">
                <div class="progress-bar">
                  <div class="progress-fill" :style="{ width: (posicionActualGrados / Math.max(gradosObjetivo, 1)) * 100 + '%' }"></div>
                </div>
                <div class="progress-stats">
                  <span>{{ Math.round(posicionActualGrados) }}°</span>
                  <span>{{ gradosObjetivo }}°</span>
                </div>
              </div>
              
              <div class="action-buttons">
                <button class="action-btn start-move-btn" @click="iniciarMovimientoPosicion">Iniciar movimiento</button>
                <button class="action-btn zero-btn" @click="regresarACero">Regresar a 0°</button>
                
              </div>
              
              <div class="chart-section">
                <div class="chart-header">
                  <span>Seguimiento de posición (Grados vs Tiempo)</span>
                  <div class="chart-controls">
                    <button class="chart-btn" @click="zoomOut(chartScrollContainerPos, followEnabledPos)">Zoom -</button>
                    <button class="chart-btn" @click="zoomIn(chartScrollContainerPos, followEnabledPos)">Zoom +</button>
                    <button class="chart-btn" @click="resetZoom(chartScrollContainerPos)">Reset</button>
                  </div>
                </div>
                <div class="chart-scroll-container" ref="chartScrollContainerPos" @scroll="handleScroll(chartScrollContainerPos, followEnabledPos)">
                  <div class="chart-inner" :style="{ width: chartInnerWidth + 'px' }">
                    <div v-if="!chartReadyPos" class="chart-placeholder">Esperando datos...</div>
                    <div v-else class="chart-container">
                      <svg :width="chartWidth * zoom" :height="CHART_HEIGHT">
                        <line :x1="CHART_PADDING" :y1="CHART_PADDING" :x2="CHART_PADDING" :y2="CHART_HEIGHT - CHART_PADDING" stroke="#94a3b8" stroke-width="1" />
                        <line :x1="CHART_PADDING" :y1="CHART_HEIGHT - CHART_PADDING" :x2="chartWidth * zoom - CHART_PADDING" :y2="CHART_HEIGHT - CHART_PADDING" stroke="#94a3b8" stroke-width="1" />
                        
                        <template v-for="(point, index) in setpointPointsPos" :key="'s'+index">
                          <line v-if="index>0" :x1="setpointPointsPos[index-1].x" :y1="setpointPointsPos[index-1].y" :x2="point.x" :y2="point.y" stroke="#3b82f6" stroke-width="2" stroke-dasharray="5,5" />
                        </template>
                        <template v-for="(point, index) in obtenidoPointsPos" :key="'p'+index">
                          <line v-if="index>0" :x1="obtenidoPointsPos[index-1].x" :y1="obtenidoPointsPos[index-1].y" :x2="point.x" :y2="point.y" stroke="#10b981" stroke-width="2" />
                        </template>
                        
                        <circle v-for="(point, index) in obtenidoPointsPos" :key="'pc'+index" :cx="point.x" :cy="point.y" r="2" fill="#10b981" />
                      </svg>
                      <div class="legend">
                        <span class="legend-line blue-dashed"></span><span>Setpoint</span>
                        <span class="legend-line green"></span><span>Posicion real</span>
                      </div>
                    </div>
                  </div>
                </div>
                <div class="scroll-buttons">
                  <button class="scroll-btn" @click="scrollToStart(chartScrollContainerPos, followEnabledPos)">Inicio</button>
                  <button class="scroll-btn" @click="scrollToEnd(chartScrollContainerPos, followEnabledPos)">Fin</button>
                </div>
              </div>
            </div>
          </div>
        </transition>

        <button class="disconnect-btn" @click="disconnect">Desconectar</button>
      </template>

      <div v-else class="hero-section">
        <div class="hero-container">
          <div class="hero-content">
            <div class="hero-icon">
              <span class="pulse-ring"></span>
              <span class="main-icon">🔌</span>
            </div>
            <h2>Control de Motor DC</h2>
            <p class="hero-description">
              Conectate a tu ESP32 para controlar el motor
            </p>
            
            <div class="connection-info">
              <div class="info-card">
                <div class="info-icon">🌐</div>
                <div class="info-text">
                  <strong>Estado de red</strong>
                  <span :class="networkState.isConnected ? 'text-success' : 'text-danger'">
                    {{ networkState.isConnected ? 'Conectado' : 'Sin conexion' }}
                  </span>
                </div>
              </div>
              <div class="info-card">
                <div class="info-icon">🔌</div>
                <div class="info-text">
                  <strong>Dispositivo</strong>
                  <span>ESPRUL</span>
                </div>
              </div>
            </div>
            
            <div class="help-text">
              <span class="help-icon">i</span>
              <span>Asegurate de que el ESP32 este encendido y en la misma red</span>
            </div>

            <button class="simulation-hero-btn" @click="toggleSimulation">
              <span>🎮</span>
              Probar en modo simulación
            </button>
          </div>
        </div>
      </div>
    </main>
  </div>
</template>

<style scoped>
* {
  box-sizing: border-box;
}

.app {
  background: linear-gradient(135deg, #f8fafc 0%, #f1f5f9 100%);
  min-height: 100vh;
}

.app.simulation-mode {
  background: linear-gradient(135deg, #f0fdf4 0%, #dcfce7 100%);
}

.no-scroll {
  touch-action: none;
}

.scroll-view {
  min-height: 100vh;
  padding: 24px 32px 100px;
}

.with-fixed-bar {
  padding-bottom: 100px;
}

.fade-slide-enter-active,
.fade-slide-leave-active {
  transition: all 0.4s cubic-bezier(0.4, 0, 0.2, 1);
}

.fade-slide-enter-from {
  opacity: 0;
  transform: translateX(30px);
}

.fade-slide-leave-to {
  opacity: 0;
  transform: translateX(-30px);
}

.message-enter-active,
.message-leave-active {
  transition: all 0.3s ease;
}

.message-enter-from {
  transform: translateX(100%);
  opacity: 0;
}

.message-leave-to {
  transform: translateX(100%);
  opacity: 0;
}

.slide-enter-active,
.slide-leave-active {
  transition: all 0.3s ease;
}

.slide-enter-from {
  opacity: 0;
  transform: translateY(-20px);
}

.slide-leave-to {
  opacity: 0;
  transform: translateY(-20px);
}

.floating-message {
  position: fixed;
  top: 24px;
  right: 24px;
  z-index: 1000;
  padding: 14px 24px;
  border-radius: 12px;
  font-weight: 500;
  color: white;
  cursor: pointer;
  box-shadow: 0 8px 24px rgba(0,0,0,0.15);
  max-width: 380px;
  backdrop-filter: blur(8px);
}

.message-success { background: linear-gradient(135deg, #22c55e, #16a34a); }
.message-error { background: linear-gradient(135deg, #ef4444, #dc2626); }
.message-warning { background: linear-gradient(135deg, #f97316, #ea580c); }
.message-info { background: linear-gradient(135deg, #3b82f6, #2563eb); }

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 32px;
  flex-wrap: wrap;
  gap: 16px;
}

.title-wrapper {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.simulation-badge {
  background: linear-gradient(135deg, #22c55e, #16a34a);
  color: white;
  padding: 6px 14px;
  border-radius: 30px;
  font-size: 0.8rem;
  font-weight: 600;
  letter-spacing: 0.5px;
  animation: pulse-sim 2s ease-in-out infinite;
}

@keyframes pulse-sim {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.7;
  }
}

.plug-icon {
  font-size: 2rem;
  display: inline-block;
  animation: float 3s ease-in-out infinite;
}

.title {
  font-size: 2rem;
  font-weight: 700;
  background: linear-gradient(135deg, #1e293b, #0f172a);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
  margin: 0;
}

@keyframes float {
  0%, 100% { transform: translateY(0px); }
  50% { transform: translateY(-5px); }
}

.header-buttons {
  display: flex;
  gap: 12px;
}

.simulation-button {
  background: linear-gradient(135deg, #22c55e, #16a34a);
  border: none;
  padding: 10px 24px;
  border-radius: 40px;
  font-size: 0.9rem;
  font-weight: 600;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 8px;
  transition: all 0.2s;
  color: white;
  box-shadow: 0 1px 3px rgba(0,0,0,0.05);
}

.simulation-button:hover {
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(34,197,94,0.3);
}

.simulation-button.active {
  background: linear-gradient(135deg, #ef4444, #dc2626);
}

.simulation-button.active:hover {
  box-shadow: 0 4px 12px rgba(239,68,68,0.3);
}

.settings-button {
  background: white;
  border: 1px solid #e2e8f0;
  padding: 10px 24px;
  border-radius: 40px;
  font-size: 0.9rem;
  font-weight: 500;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 8px;
  transition: all 0.2s;
  box-shadow: 0 1px 3px rgba(0,0,0,0.05);
}

.settings-button:hover {
  background: #f8fafc;
  border-color: #cbd5e1;
  transform: translateY(-1px);
}

.top-section {
  display: grid;
  grid-template-columns: 1fr auto;
  grid-template-rows: auto auto;
  gap: 16px;
  margin-bottom: 32px;
  align-items: start;
}

.header {
  grid-column: 1 / -1;
  margin-bottom: 0;
}

.status-bar {
  grid-column: 1;
  display: flex;
  gap: 16px;
  justify-content: flex-start;
  margin-bottom: 0;
  align-self: center;
}

.config-panel {
  grid-column: 2;
  grid-row: 2;
  margin-bottom: 0;
  align-self: start;
}

.config-card {
  background: white;
  border-radius: 24px;
  padding: 0;
  max-width: 500px;
  margin: 0 auto;
  box-shadow: 0 8px 32px rgba(0,0,0,0.08);
  overflow: hidden;
}

.config-header {
  background: linear-gradient(135deg, #1e293b, #0f172a);
  padding: 20px 24px;
  display: flex;
  align-items: center;
  gap: 12px;
}

.config-icon {
  font-size: 1.5rem;
}

.config-header h3 {
  margin: 0;
  color: white;
  font-size: 1.1rem;
  font-weight: 600;
}

.config-body {
  padding: 24px;
}

.input-group {
  margin-bottom: 24px;
}

.input-group label {
  display: block;
  margin-bottom: 8px;
  font-weight: 500;
  color: #334155;
}

.input {
  width: 100%;
  padding: 12px 16px;
  border: 2px solid #e2e8f0;
  border-radius: 16px;
  font-size: 1rem;
  transition: all 0.2s;
}

.input:focus {
  border-color: #3b82f6;
  outline: none;
  box-shadow: 0 0 0 3px rgba(59,130,246,0.1);
}

.input-hint {
  font-size: 0.75rem;
  color: #64748b;
  margin-top: 6px;
}

.connect-btn {
  width: 100%;
  padding: 14px;
  background: linear-gradient(135deg, #3b82f6, #2563eb);
  border: none;
  border-radius: 40px;
  color: white;
  font-weight: 600;
  font-size: 1rem;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  transition: all 0.2s;
}

.connect-btn:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(59,130,246,0.3);
}

.connect-btn:disabled {
  opacity: 0.7;
  cursor: not-allowed;
}

.spinner {
  width: 20px;
  height: 20px;
  border: 2px solid rgba(255,255,255,0.3);
  border-top-color: white;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
  display: inline-block;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.status-bar {
  display: flex;
  gap: 16px;
  margin-bottom: 32px;
  justify-content: center;
  flex-wrap: wrap;
}

.status-item {
  background: white;
  padding: 8px 20px;
  border-radius: 40px;
  font-size: 0.85rem;
  display: flex;
  align-items: center;
  gap: 10px;
  border: 1px solid #e2e8f0;
  box-shadow: 0 1px 3px rgba(0,0,0,0.05);
}

.status-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #94a3b8;
}

.status-online .status-dot { background: #22c55e; box-shadow: 0 0 0 2px rgba(34,197,94,0.2); }
.status-offline .status-dot { background: #ef4444; box-shadow: 0 0 0 2px rgba(239,68,68,0.2); }
.status-simulation .status-dot-simulation { background: #22c55e; box-shadow: 0 0 0 2px rgba(34,197,94,0.2); animation: pulse 2s ease-in-out infinite; }

@keyframes pulse {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.5;
  }
}

.status-ip {
  font-family: monospace;
  background: #f1f5f9;
  padding: 2px 8px;
  border-radius: 20px;
  font-size: 0.75rem;
  color: #475569;
}

.floating-nav {
  position: fixed;
  bottom: 20px;
  left: 20px;
  z-index: 200;
  display: flex;
  flex-direction: column;
  gap: 12px;
  background: rgba(255,255,255,0.95);
  backdrop-filter: blur(10px);
  padding: 12px;
  border-radius: 60px;
  box-shadow: 0 8px 24px rgba(0,0,0,0.15);
  border: 1px solid rgba(226,232,240,0.8);
}

.floating-nav-btn {
  width: 56px;
  height: 56px;
  border-radius: 50%;
  background: white;
  border: 2px solid #e2e8f0;
  cursor: pointer;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  box-shadow: 0 2px 8px rgba(0,0,0,0.05);
}

.floating-nav-btn:hover {
  transform: scale(1.05);
  border-color: #3b82f6;
  box-shadow: 0 4px 12px rgba(59,130,246,0.2);
}

.floating-nav-btn.active {
  background: linear-gradient(135deg, #3b82f6, #2563eb);
  border-color: #3b82f6;
  box-shadow: 0 4px 12px rgba(59,130,246,0.4);
  color: white;
}

.nav-icon {
  font-size: 0.9rem;
  font-weight: 600;
  color: #475569;
}

.nav-text {
  font-size: 0.6rem;
  font-weight: 600;
  color: #64748b;
}

.floating-nav-btn.active .nav-icon,
.floating-nav-btn.active .nav-text {
  color: white;
}

.fixed-button-bar {
  position: fixed;
  bottom: 20px;
  right: 20px;
  left: auto;
  padding: 0;
  background: transparent;
  backdrop-filter: none;
  border-top: none;
  z-index: 100;
  display: flex;
  gap: 12px;
  justify-content: flex-end;
  box-shadow: none;
}

.fixed-emergency-button,
.fixed-calibration-button {
  flex: none;
  width: auto;
  min-width: 140px;
  max-width: 180px;
  padding: 12px 20px;
  border: none;
  border-radius: 50px;
  font-weight: 700;
  font-size: 0.85rem;
  cursor: pointer;
  transition: all 0.2s ease;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  letter-spacing: 0.5px;
  box-shadow: 0 4px 12px rgba(0,0,0,0.15);
}

.fixed-emergency-button {
  background: linear-gradient(135deg, #dc2626, #b91c1c);
  color: white;
}

.fixed-emergency-button:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(220, 38, 38, 0.4);
}

.fixed-calibration-button {
  background: linear-gradient(135deg, #16a34a, #15803d);
  color: white;
}

.fixed-calibration-button:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(22, 163, 74, 0.4);
}

.profile-container {
  background: white;
  border-radius: 32px;
  overflow: hidden;
  box-shadow: 0 8px 32px rgba(0,0,0,0.08);
  border: 1px solid rgba(226,232,240,0.6);
  margin-bottom: 32px;
}

.profile-header-single {
  background: linear-gradient(135deg, #1e293b, #0f172a);
  padding: 24px 32px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.profile-header-single h2 {
  margin: 0;
  font-size: 1.5rem;
  font-weight: 600;
  color: white;
}

.profile-badge {
  background: rgba(255,255,255,0.2);
  padding: 6px 14px;
  border-radius: 24px;
  font-size: 0.75rem;
  font-weight: 500;
  color: white;
  backdrop-filter: blur(4px);
}

.profile-content-single {
  padding: 32px;
}

.slider-section {
  margin-bottom: 32px;
  display: flex;
  flex-direction: column;
  align-items: center;
}

.slider-touch-area {
  width: 100%;
  max-width: 450px;
  height: 44px;
  display: flex;
  align-items: center;
  touch-action: none;
  cursor: pointer;
}

.slider-track {
  width: 100%;
  height: 6px;
  background: #e2e8f0;
  border-radius: 3px;
  position: relative;
}

.slider-fill {
  height: 6px;
  background: linear-gradient(90deg, #3b82f6, #2563eb);
  border-radius: 3px;
  position: absolute;
  left: 0;
  top: 0;
}

.slider-thumb {
  position: absolute;
  top: -11px;
  width: 28px;
  height: 28px;
  border-radius: 28px;
  background: white;
  border: 2px solid #3b82f6;
  box-shadow: 0 2px 8px rgba(0,0,0,0.15);
  transition: transform 0.1s;
}

.rpm-input-group {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  margin-top: 20px;
}

.rpm-input {
  width: 100px;
  text-align: center;
  padding: 10px;
  border: 2px solid #e2e8f0;
  border-radius: 30px;
  font-size: 1rem;
  font-weight: 600;
}

.send-btn {
  background: #3b82f6;
  border: none;
  padding: 10px 24px;
  border-radius: 30px;
  color: white;
  cursor: pointer;
  font-weight: 500;
  transition: all 0.2s;
}

.quick-buttons {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  gap: 10px;
  margin-bottom: 32px;
}

.quick-btn {
  background: white;
  border: 1px solid #e2e8f0;
  padding: 8px 18px;
  border-radius: 30px;
  font-size: 0.85rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
}

.quick-btn:hover {
  background: #f1f5f9;
  border-color: #3b82f6;
  transform: translateY(-1px);
}

.stop-btn {
  border-color: #ef4444;
  color: #ef4444;
}

.stop-btn:hover {
  background: #ef4444;
  color: white;
}

.dashboard {
  background: linear-gradient(135deg, #f8fafc, #f1f5f9);
  border-radius: 20px;
  padding: 24px;
  margin-bottom: 32px;
  border: 1px solid #e2e8f0;
}

.dashboard h3 {
  margin: 0 0 20px 0;
  font-size: 1rem;
  color: #475569;
  text-align: center;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.dashboard-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 20px;
}

.dashboard-item {
  text-align: center;
  padding: 16px;
  background: white;
  border-radius: 16px;
  transition: all 0.2s;
}

.dashboard-item:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0,0,0,0.05);
}

.dashboard-label {
  display: block;
  font-size: 0.7rem;
  color: #64748b;
  margin-bottom: 8px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.dashboard-value {
  font-size: 1.4rem;
  font-weight: 700;
  color: #0f172a;
}

.pid-section {
  margin-bottom: 32px;
  padding: 24px;
  background: linear-gradient(135deg, #f8fafc, #f1f5f9);
  border-radius: 20px;
}

.pid-section h3 {
  margin: 0 0 20px 0;
  font-size: 1rem;
  text-align: center;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: #475569;
}

.pid-inputs {
  display: flex;
  gap: 20px;
  margin-bottom: 20px;
  flex-wrap: wrap;
  justify-content: center;
}

.pid-inputs div {
  flex: 1;
  min-width: 120px;
  max-width: 160px;
}

.pid-inputs label {
  display: block;
  font-size: 0.7rem;
  margin-bottom: 8px;
  color: #64748b;
  text-align: center;
  font-weight: 600;
}

.input-small {
  width: 100%;
  padding: 10px;
  border: 2px solid #e2e8f0;
  border-radius: 14px;
  text-align: center;
  font-weight: 500;
  transition: all 0.2s;
}

.input-small:focus {
  border-color: #3b82f6;
  outline: none;
}

.chart-section {
  margin-top: 24px;
}

.chart-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
  font-size: 0.9rem;
  font-weight: 500;
  flex-wrap: wrap;
  gap: 12px;
}

.chart-controls {
  display: flex;
  gap: 10px;
}

.chart-btn {
  background: #f1f5f9;
  border: 1px solid #e2e8f0;
  padding: 6px 14px;
  border-radius: 24px;
  cursor: pointer;
  font-size: 0.75rem;
  font-weight: 500;
  transition: all 0.2s;
}

.chart-btn:hover {
  background: #e2e8f0;
  border-color: #cbd5e1;
}

.chart-scroll-container {
  width: 100%;
  overflow-x: auto;
  border-radius: 20px;
  background: #f8fafc;
  border: 1px solid #e2e8f0;
}

.chart-inner {
  min-width: 100%;
}

.chart-container {
  padding: 20px;
}

.chart-placeholder {
  height: 320px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #94a3b8;
  font-size: 0.9rem;
}

.legend {
  display: flex;
  justify-content: center;
  gap: 32px;
  margin-top: 20px;
  font-size: 0.75rem;
}

.legend-line {
  display: inline-block;
  width: 24px;
  height: 2px;
  margin-right: 8px;
  vertical-align: middle;
}

.blue { background: #3b82f6; }
.orange { background: #f97316; }
.green { background: #10b981; }
.blue-dashed { background: repeating-linear-gradient(90deg, #3b82f6, #3b82f6 6px, transparent 6px, transparent 12px); }

.scroll-buttons {
  display: flex;
  justify-content: center;
  gap: 16px;
  margin-top: 20px;
}

.scroll-btn {
  background: #f1f5f9;
  border: 1px solid #e2e8f0;
  padding: 8px 24px;
  border-radius: 30px;
  cursor: pointer;
  font-size: 0.8rem;
  font-weight: 500;
  transition: all 0.2s;
}

.scroll-btn:hover {
  background: #e2e8f0;
  border-color: #cbd5e1;
}

.angle-section {
  margin-bottom: 32px;
}

.angle-section label {
  display: block;
  margin-bottom: 12px;
  font-weight: 500;
  font-size: 0.9rem;
  text-align: center;
  color: #475569;
}

.angle-control-group {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 24px;
  flex-wrap: wrap;
}

.angle-input-wrapper {
  position: relative;
  display: inline-flex;
  align-items: center;
}

.angle-input {
  width: 140px;
  padding: 12px 16px;
  border: 2px solid #e2e8f0;
  border-radius: 20px;
  font-size: 1.2rem;
  font-weight: 600;
  text-align: center;
  padding-right: 40px;
}

.angle-unit {
  position: absolute;
  right: 16px;
  font-size: 1.1rem;
  font-weight: 600;
  color: #64748b;
}

.angle-buttons {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
  justify-content: center;
}

.angle-btn {
  background: #f1f5f9;
  border: 1px solid #e2e8f0;
  padding: 10px 20px;
  border-radius: 30px;
  cursor: pointer;
  font-size: 0.85rem;
  font-weight: 500;
  transition: all 0.2s;
}

.angle-btn:hover {
  background: #e2e8f0;
  border-color: #3b82f6;
  transform: translateY(-1px);
}

.config-row-single {
  display: flex;
  justify-content: center;
  margin-bottom: 24px;
}

.config-row-single .config-field {
  max-width: 300px;
  width: 100%;
}

.speedometer-container {
  display: flex;
  justify-content: center;
  margin: 24px 0;
}

.speedometer {
  position: relative;
  width: 220px;
  height: 220px;
}

.gauge {
  width: 100%;
  height: 100%;
}

.progress-section {
  margin: 24px 0;
}

.progress-bar {
  width: 100%;
  height: 10px;
  background: #e2e8f0;
  border-radius: 5px;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #3b82f6, #2563eb);
  transition: width 0.2s ease;
}

.progress-stats {
  display: flex;
  justify-content: space-between;
  margin-top: 10px;
  font-size: 0.8rem;
  color: #64748b;
}

.action-buttons {
  display: flex;
  gap: 20px;
  margin-bottom: 32px;
  flex-wrap: wrap;
  justify-content: center;
}

.action-btn {
  flex: 1;
  min-width: 180px;
  max-width: 220px;
  padding: 12px;
  border: none;
  border-radius: 40px;
  font-weight: 600;
  cursor: pointer;
  font-size: 0.9rem;
  transition: all 0.2s;
}

.update-btn { background: linear-gradient(135deg, #3b82f6, #2563eb); color: white; }
.config-btn { background: linear-gradient(135deg, #64748b, #475569); color: white; }
.start-btn { background: linear-gradient(135deg, #22c55e, #16a34a); color: white; }
.stop-btn { background: linear-gradient(135deg, #ef4444, #dc2626); color: white; }
.start-move-btn { background: linear-gradient(135deg, #3b82f6, #2563eb); color: white; }
.zero-btn { background: linear-gradient(135deg, #f59e0b, #d97706); color: white; }
.stop-move-btn { background: linear-gradient(135deg, #ef4444, #dc2626); color: white; }
.stop-move-btn:disabled { opacity: 0.5; cursor: not-allowed; }

.action-btn:hover:not(:disabled) {
  transform: translateY(-2px);
  filter: brightness(1.05);
  box-shadow: 0 4px 12px rgba(0,0,0,0.15);
}

.config-row {
  display: flex;
  gap: 24px;
  margin-bottom: 24px;
  flex-wrap: wrap;
  justify-content: center;
}

.config-field {
  flex: 1;
  min-width: 220px;
  max-width: 300px;
}

.config-field label {
  display: block;
  margin-bottom: 8px;
  font-size: 0.85rem;
  font-weight: 500;
  text-align: center;
  color: #475569;
}

.disconnect-btn {
  width: 100%;
  max-width: 400px;
  margin: 16px auto 0;
  display: block;
  padding: 16px;
  background: white;
  border: 2px solid #ef4444;
  border-radius: 50px;
  color: #ef4444;
  font-weight: 600;
  font-size: 1rem;
  cursor: pointer;
  transition: all 0.2s;
}

.disconnect-btn:hover {
  background: #ef4444;
  color: white;
  transform: translateY(-2px);
}

.devices-list {
  margin-bottom: 24px;
}

.devices-list label {
  display: block;
  margin-bottom: 12px;
  font-weight: 600;
  color: #334155;
  font-size: 0.9rem;
}

.devices-grid {
  display: flex;
  flex-direction: column;
  gap: 12px;
  max-height: 300px;
  overflow-y: auto;
  margin-bottom: 20px;
}

.device-card {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  background: #f8fafc;
  border: 2px solid #e2e8f0;
  border-radius: 16px;
  cursor: pointer;
  transition: all 0.2s;
}

.device-card:hover {
  background: #f1f5f9;
  border-color: #3b82f6;
  transform: translateX(4px);
}

.device-card.selected {
  background: #eff6ff;
  border-color: #3b82f6;
  box-shadow: 0 2px 8px rgba(59,130,246,0.2);
}

.device-icon {
  font-size: 1.2rem;
  font-weight: 600;
}

.device-info {
  flex: 1;
}

.device-name {
  font-weight: 600;
  color: #0f172a;
  font-size: 1rem;
}

.device-ip {
  font-size: 0.75rem;
  color: #64748b;
  font-family: monospace;
}

.device-status {
  color: #22c55e;
  font-size: 1.2rem;
}

.action-buttons-config {
  margin-bottom: 24px;
}

.scan-btn {
  width: 100%;
  padding: 12px;
  background: linear-gradient(135deg, #3b82f6, #2563eb);
  border: none;
  border-radius: 40px;
  color: white;
  font-weight: 600;
  font-size: 0.9rem;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  transition: all 0.2s;
}

.scan-btn:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 8px 20px rgba(59,130,246,0.3);
}

.scan-btn:disabled {
  opacity: 0.7;
  cursor: not-allowed;
}

.simulation-info {
  text-align: center;
}

.info-card-sim {
  background: linear-gradient(135deg, #f0fdf4, #dcfce7);
  border: 1px solid #bbf7d0;
  border-radius: 16px;
  padding: 16px;
  margin-bottom: 20px;
  display: flex;
  gap: 12px;
  align-items: flex-start;
  text-align: left;
}

.sim-icon {
  font-size: 2rem;
}

.info-card-sim strong {
  display: block;
  margin-bottom: 8px;
  color: #166534;
}

.info-card-sim p {
  margin: 0;
  font-size: 0.85rem;
  color: #14532d;
}

.hero-section {
  min-height: calc(100vh - 200px);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 40px 20px;
}

.hero-container {
  max-width: 900px;
  width: 100%;
  margin: 0 auto;
}

.hero-content {
  text-align: center;
}

.hero-icon {
  position: relative;
  display: inline-block;
  margin-bottom: 32px;
}

.pulse-ring {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  width: 100px;
  height: 100px;
  border-radius: 50%;
  background: rgba(59,130,246,0.2);
  animation: pulse-ring 2s ease-out infinite;
}

@keyframes pulse-ring {
  0% {
    transform: translate(-50%, -50%) scale(0.8);
    opacity: 0.8;
  }
  100% {
    transform: translate(-50%, -50%) scale(1.5);
    opacity: 0;
  }
}

.main-icon {
  position: relative;
  display: inline-block;
  font-size: 4rem;
  animation: float 3s ease-in-out infinite;
}

.hero-content h2 {
  font-size: 2.5rem;
  font-weight: 700;
  color: #0f172a;
  margin-bottom: 16px;
}

.hero-description {
  font-size: 1.1rem;
  color: #475569;
  margin-bottom: 40px;
  line-height: 1.6;
}

.connection-info {
  display: flex;
  justify-content: center;
  gap: 24px;
  margin-bottom: 48px;
  flex-wrap: wrap;
}

.info-card {
  background: white;
  padding: 20px 32px;
  border-radius: 24px;
  display: flex;
  align-items: center;
  gap: 16px;
  box-shadow: 0 4px 12px rgba(0,0,0,0.05);
  border: 1px solid #e2e8f0;
  transition: all 0.2s;
}

.info-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(0,0,0,0.1);
}

.info-icon {
  font-size: 2rem;
}

.info-text {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
}

.info-text strong {
  font-size: 0.75rem;
  color: #64748b;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: 4px;
}

.info-text span {
  font-size: 1rem;
  font-weight: 600;
  color: #0f172a;
}

.text-success { color: #22c55e !important; }
.text-danger { color: #ef4444 !important; }

.help-text {
  display: inline-flex;
  align-items: center;
  gap: 12px;
  background: #f1f5f9;
  padding: 12px 24px;
  border-radius: 60px;
  font-size: 0.85rem;
  color: #475569;
  margin-top: 20px;
}

.help-icon {
  font-size: 1.1rem;
  font-weight: bold;
  background: #cbd5e1;
  width: 20px;
  height: 20px;
  border-radius: 50%;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  color: #475569;
}

.simulation-hero-btn {
  margin-top: 32px;
  padding: 16px 32px;
  background: linear-gradient(135deg, #22c55e, #16a34a);
  border: none;
  border-radius: 60px;
  color: white;
  font-size: 1.1rem;
  font-weight: 600;
  cursor: pointer;
  display: inline-flex;
  align-items: center;
  gap: 12px;
  transition: all 0.3s ease;
  box-shadow: 0 4px 12px rgba(34,197,94,0.3);
}

.simulation-hero-btn:hover {
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(34,197,94,0.4);
}

.simulation-hero-btn span {
  font-size: 1.5rem;
}

@media (max-width: 768px) {
  .scroll-view {
    padding: 16px 20px 100px;
  }
  
  .floating-nav {
    bottom: 100px;
    left: 10px;
    padding: 8px;
    gap: 8px;
  }
  
  .floating-nav-btn {
    width: 48px;
    height: 48px;
  }
  
  .fixed-button-bar {
    bottom: 20px;
    right: 10px;
    gap: 8px;
  }
  
  .fixed-emergency-button,
  .fixed-calibration-button {
    min-width: 100px;
    padding: 10px 16px;
    font-size: 0.75rem;
  }
  
  .title {
    font-size: 1.5rem;
  }
  
  .profile-content-single {
    padding: 20px;
  }
  
  .profile-header-single {
    padding: 16px 20px;
  }
  
  .profile-header-single h2 {
    font-size: 1.2rem;
  }
  
  .dashboard-grid {
    grid-template-columns: repeat(2, 1fr);
  }
  
  .hero-content h2 {
    font-size: 1.8rem;
  }
  
  .hero-description {
    font-size: 1rem;
    padding: 0 20px;
  }
  
  .connection-info {
    flex-direction: column;
    align-items: center;
  }
  
  .info-card {
    width: 100%;
    max-width: 300px;
  }
  
  .pid-inputs {
    flex-direction: column;
    align-items: center;
  }
  
  .pid-inputs div {
    max-width: 100%;
    width: 100%;
  }
  
  .config-row {
    flex-direction: column;
    align-items: center;
  }
  
  .config-field {
    max-width: 100%;
    width: 100%;
  }
  
  .action-buttons {
    flex-direction: column;
    align-items: center;
  }
  
  .action-btn {
    width: 100%;
    max-width: 100%;
  }
  
  .angle-control-group {
    flex-direction: column;
    align-items: center;
  }
  
  .chart-header {
    flex-direction: column;
    align-items: center;
  }
  
  .devices-grid {
    max-height: 200px;
  }
  
  .speedometer-container {
    transform: scale(0.8);
  }

  .simulation-button {
    padding: 8px 16px;
    font-size: 0.8rem;
  }

  .simulation-button span:last-child {
    display: none;
  }

  .simulation-button .simulation-icon {
    margin: 0;
  }

  .header-buttons {
    gap: 8px;
  }

  .title-wrapper {
    flex-direction: column;
    align-items: flex-start;
  }
}
</style>