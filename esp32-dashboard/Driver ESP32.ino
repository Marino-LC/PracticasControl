// ================= LIBRERIAS =================
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

// ================= CONFIGURACION WIFI =================
const char* ssid = "Galaxy A71";
const char* password = "12345678";

/* ================= PINES =================
const int PIN_DIR1 = 4;
const int PIN_DIR2 = 16;
const int PIN_PWM  = 17;
const int PIN_ENCA = 18;
const int PIN_ENCB = 19;*/

/* ================= PINES (Adaptados para ESP32-C3 Supermini) =================
const int PIN_ENCA = 0;  // Seguro y rápido para interrupciones
const int PIN_ENCB = 1;  // Seguro para lectura digital
const int PIN_PWM  = 3;  // Seguro para salida PWM
const int PIN_DIR1 = 4;  // Seguro para salida digital
const int PIN_DIR2 = 5;  // Seguro para salida digital*/

// ================= PINES (Adaptados para ESP32-C3 Supermini) =================
const int PIN_ENCA = 0;  // Seguro y rápido para interrupciones
const int PIN_ENCB = 1;  // Seguro para lectura digital
const int PIN_PWM  = 21;  // Seguro para salida PWM
const int PIN_DIR1 = 20;  // Seguro para salida digital
const int PIN_DIR2 = 7;  // Seguro para salida digital

// ================= GANANCIAS DE CONTROL =================
float Kp_vel = 10.13;
float Ki_vel = 100;
float Kp_pos = 1.5;

// ================= CONSTANTES =================
const float PPR = 493.9;
const float Ts = 0.05;
const float TF_SEGUNDOS = 5.0;
const float RPM_MAX = 120.0;

// ================= PERFIL VELOCIDAD =================
bool perfil_vel_activo = false;
float T_total_vel = 5.0;
float V_max_vel = 120.0;
float const_A_vel = 0;
float const_B_vel = 0;
unsigned long tiempo_inicio_perfil_vel = 0;

// ================= VARIABLES PRINCIPALES =================
volatile long pulsos = 0;
long pulsos_previos = 0;

unsigned long tiempo_previo = 0;
unsigned long tiempo_inicio_perfil = 0;

float theta_real_rad = 0;
float theta_real_deg = 0;
float omega_real_rads = 0;
float omega_real_rpm = 0;
float error_integral_vel = 0;

bool movimiento_activo = false;

float theta_inicial_rad = 0;
float theta_inicial_deg = 0;
float delta_theta_rad = 0;
float delta_theta_deg = 0;
float theta_ref_deg = 0;

float offset_cero_rad = 0.0;

// Variables para modo manual
float setpoint_vel = 0;      // en rad/s
float setpoint_rpm = 0;      // en RPM
String modo_control = "manual";

// ================= SERVER =================
WebServer server(80);

// ================= FUNCION PARA CALCULAR ERROR CORTO =================
float calcularErrorCorto(float objetivo, float actual) {
  float error = objetivo - actual;
  if (error > 180) {
    error = error - 360;
  } else if (error < -180) {
    error = error + 360;
  }
  return error;
}

// ================= ENCODER =================
void IRAM_ATTR leerEncoder() {
  if (digitalRead(PIN_ENCA) == digitalRead(PIN_ENCB)) {
    pulsos--;
  } else {
    pulsos++;
  }
}

// ================= CORS =================
void enableCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ================= FUNCION PERFIL POSICION =================
float calcularPerfilPosicion(float t, float T, float delta_theta) {
  if (t <= 0) return 0;
  if (t >= T) return delta_theta;
  
  float tau = t / T;
  float tau2 = tau * tau;
  float tau3 = tau2 * tau;
  float tau4 = tau3 * tau;
  float tau5 = tau4 * tau;
  
  return delta_theta * (10*tau3 - 15*tau4 + 6*tau5);
}

// ================= ENDPOINTS =================
void handlePing() {
  enableCORS();
  server.send(200, "text/plain", "pong");
}

void handleSetRPM() {
  enableCORS();
  if (server.hasArg("value")) {
    setpoint_rpm = server.arg("value").toFloat();
    if (setpoint_rpm > RPM_MAX) setpoint_rpm = RPM_MAX;
    if (setpoint_rpm < 0) setpoint_rpm = 0;
    setpoint_vel = setpoint_rpm * (2.0 * PI / 60.0);
    modo_control = "manual";
    perfil_vel_activo = false;
    movimiento_activo = false;
    error_integral_vel = 0;
    Serial.printf("RPM manual: %.1f\n", setpoint_rpm);
    server.send(200, "text/plain", "OK");
  }
}

void handleSetDirection() {
  enableCORS();
  if (server.hasArg("dir")) {
    String dir = server.arg("dir");
    if (dir == "CW") {
      digitalWrite(PIN_DIR1, LOW);
      digitalWrite(PIN_DIR2, HIGH);
    } else {
      digitalWrite(PIN_DIR1, HIGH);
      digitalWrite(PIN_DIR2, LOW);
    }
    server.send(200, "text/plain", dir);
  }
}

void handleProfileConfig() {
  enableCORS();
  if (server.hasArg("vmax") && server.hasArg("t")) {
    V_max_vel = server.arg("vmax").toFloat();
    T_total_vel = server.arg("t").toFloat();
    
    if (V_max_vel > RPM_MAX) V_max_vel = RPM_MAX;
    if (V_max_vel < 0) V_max_vel = 0;
    if (T_total_vel < 0.5) T_total_vel = 0.5;
    
    const_A_vel = -(4.0 * V_max_vel) / (T_total_vel * T_total_vel);
    const_B_vel = (4.0 * V_max_vel) / T_total_vel;
    
    Serial.printf("Perfil configurado: Vmax=%.1f RPM, T=%.1f s\n", V_max_vel, T_total_vel);
    server.send(200, "text/plain", "Perfil OK");
  }
}

void handleStartProfile() {
  enableCORS();
  perfil_vel_activo = true;
  modo_control = "perfil_vel";
  movimiento_activo = false;
  tiempo_inicio_perfil_vel = millis();
  error_integral_vel = 0;
  Serial.println("Perfil velocidad INICIADO");
  server.send(200, "text/plain", "Start");
}

void handleStopProfile() {
  enableCORS();
  perfil_vel_activo = false;
  modo_control = "manual";
  setpoint_vel = 0;
  setpoint_rpm = 0;
  Serial.println("Perfil velocidad DETENIDO");
  server.send(200, "text/plain", "Stop");
}

void handleSetPosition() {
  enableCORS();
  if (server.hasArg("grados")) {
    float target_grados = server.arg("grados").toFloat();
    if (target_grados < 0) target_grados = 0;
    if (target_grados > 360) target_grados = 360;
    
    // Guardar posicion inicial
    theta_inicial_deg = theta_real_deg;
    delta_theta_deg = target_grados - theta_inicial_deg;
    
    // Usar el camino mas corto
    if (delta_theta_deg > 180) {
      delta_theta_deg = delta_theta_deg - 360;
    } else if (delta_theta_deg < -180) {
      delta_theta_deg = delta_theta_deg + 360;
    }
    
    theta_inicial_rad = theta_inicial_deg * (PI / 180.0);
    delta_theta_rad = delta_theta_deg * (PI / 180.0);
    
    error_integral_vel = 0;
    tiempo_inicio_perfil = millis();
    movimiento_activo = true;
    modo_control = "posicion";
    perfil_vel_activo = false;
    setpoint_vel = 0;
    setpoint_rpm = 0;
    
    Serial.printf("Posicion: %.1f° -> %.1f° (delta: %.1f°)\n", 
                  theta_inicial_deg, target_grados, delta_theta_deg);
    server.send(200, "text/plain", "Movimiento iniciado");
  }
}

void handleStopPosition() {
  enableCORS();
  movimiento_activo = false;
  modo_control = "manual";
  setpoint_vel = 0;
  setpoint_rpm = 0;
  Serial.println("Posicion DETENIDA");
  server.send(200, "text/plain", "Stop");
}

void handleCalibrateZero() {
  enableCORS();
  noInterrupts();
  long pulsos_actuales = pulsos;
  interrupts();
  
  offset_cero_rad = (pulsos_actuales / PPR) * (2.0 * PI);
  error_integral_vel = 0;
  movimiento_activo = false;
  theta_real_rad = 0;
  theta_real_deg = 0;
  setpoint_vel = 0;
  setpoint_rpm = 0;
  
  Serial.println("CERO CALIBRADO");
  server.send(200, "text/plain", "Zero OK");
}

void handleStatus() {
  enableCORS();
  
  StaticJsonDocument<384> doc;
  
  float rpm_set = 0;
  if (modo_control == "perfil_vel" && perfil_vel_activo) {
    float t_actual = (millis() - tiempo_inicio_perfil_vel) / 1000.0;
    if (t_actual <= T_total_vel) {
      rpm_set = (const_A_vel * t_actual * t_actual) + (const_B_vel * t_actual);
      if (rpm_set < 0) rpm_set = 0;
      if (rpm_set > RPM_MAX) rpm_set = RPM_MAX;
    }
  } else if (modo_control == "manual") {
    rpm_set = setpoint_rpm;
  }
  
  doc["setpoint"] = roundf(rpm_set * 10) / 10.0;
  doc["rpm"] = roundf(omega_real_rpm * 10) / 10.0;
  doc["pwm"] = 0;
  doc["kp"] = Kp_vel;
  doc["ki"] = Ki_vel;
  doc["kd"] = 0;
  doc["mode"] = modo_control;
  
  doc["position"] = roundf(theta_real_deg * 10) / 10.0;
  
  // Setpoint de posicion para la grafica
  float setpoint_pos_grados = theta_real_deg;
  if (modo_control == "posicion" && movimiento_activo) {
    float t = (millis() - tiempo_inicio_perfil) / 1000.0;
    if (t <= TF_SEGUNDOS) {
      setpoint_pos_grados = theta_inicial_deg + calcularPerfilPosicion(t, TF_SEGUNDOS, delta_theta_deg);
    } else {
      setpoint_pos_grados = theta_inicial_deg + delta_theta_deg;
    }
    while (setpoint_pos_grados < 0) setpoint_pos_grados += 360;
    while (setpoint_pos_grados >= 360) setpoint_pos_grados -= 360;
  }
  doc["positionSetpoint"] = roundf(setpoint_pos_grados * 10) / 10.0;
  doc["positionActive"] = movimiento_activo;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handlePID() {
  enableCORS();
  if (server.hasArg("kp") && server.hasArg("ki")) {
    Kp_vel = server.arg("kp").toFloat();
    Ki_vel = server.arg("ki").toFloat();
    Serial.printf("PID actualizado: Kp=%.2f, Ki=%.2f\n", Kp_vel, Ki_vel);
    server.send(200, "text/plain", "PID OK");
  }
}

void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {
    enableCORS();
    server.send(200);
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.println("");
  Serial.println("==========================================");
  Serial.println("SISTEMA LISTO - ESPRUL");
  Serial.println("==========================================");

  pinMode(PIN_ENCA, INPUT_PULLUP);
  pinMode(PIN_ENCB, INPUT_PULLUP);
  pinMode(PIN_DIR1, OUTPUT);
  pinMode(PIN_DIR2, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_ENCA), leerEncoder, RISING);

  ledcAttach(PIN_PWM, 1000, 8);
  ledcWrite(PIN_PWM, 0);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Conectando a WiFi");
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 40) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("WiFi conectado! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("Error de conexion WiFi");
  }
  
  if (MDNS.begin("ESPRUL")) {
    Serial.println("mDNS: ESPRUL.local");
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/ping", handlePing);
  server.on("/rpm", handleSetRPM);
  server.on("/direction", handleSetDirection);
  server.on("/profile/config", handleProfileConfig);
  server.on("/profile/start", handleStartProfile);
  server.on("/profile/stop", handleStopProfile);
  server.on("/position", handleSetPosition);
  server.on("/position/stop", handleStopPosition);
  server.on("/set_zero", handleCalibrateZero);
  server.on("/status", handleStatus);
  server.on("/pid", handlePID);
  server.onNotFound(handleNotFound);

  server.begin();
  
  tiempo_previo = millis();
  
  Serial.println("==========================================");
  Serial.println("ESP32 LISTO - ESPRUL");
  Serial.println("Modos: manual, perfil_vel, posicion");
  Serial.println("==========================================");
}

// ================= LOOP PRINCIPAL =================
void loop() {
  server.handleClient();
  
  unsigned long tiempo_actual = millis();

  // ================= PERFIL VELOCIDAD =================
  if (modo_control == "perfil_vel" && perfil_vel_activo) {
    float t_actual = (tiempo_actual - tiempo_inicio_perfil_vel) / 1000.0;
    
    if (t_actual <= T_total_vel) {
      float rpm = (const_A_vel * t_actual * t_actual) + (const_B_vel * t_actual);
      if (rpm < 0) rpm = 0;
      if (rpm > RPM_MAX) rpm = RPM_MAX;
      setpoint_rpm = rpm;
      setpoint_vel = rpm * (2.0 * PI / 60.0);
    } else {
      perfil_vel_activo = false;
      modo_control = "manual";
      setpoint_vel = 0;
      setpoint_rpm = 0;
      Serial.println("Perfil velocidad COMPLETADO");
    }
  }

  // ================= CONTROL PRINCIPAL =================
  if (tiempo_actual - tiempo_previo >= 50) {

    noInterrupts();
    long pulsos_actuales = pulsos;
    interrupts();

    long delta_pulsos = pulsos_actuales - pulsos_previos;
    pulsos_previos = pulsos_actuales;

    // POSICION CON OFFSET (en radianes)
    theta_real_rad = ((pulsos_actuales / PPR) * (2.0 * PI)) - offset_cero_rad;
    
    // Convertir a grados y normalizar a 0-360
    theta_real_deg = theta_real_rad * (180.0 / PI);
    while (theta_real_deg < 0) theta_real_deg += 360;
    while (theta_real_deg >= 360) theta_real_deg -= 360;
    theta_real_rad = theta_real_deg * (PI / 180.0);

    // VELOCIDAD
    omega_real_rads = (delta_pulsos / PPR) * (2.0 * PI / Ts);
    omega_real_rpm = omega_real_rads * (60.0 / (2.0 * PI));
    if (abs(omega_real_rpm) > 150) omega_real_rpm = 0;
    if (abs(omega_real_rads) > 15) omega_real_rads = 0;

    float theta_ref_rad = theta_real_rad;
    float omega_ref = 0;
    float theta_ref_deg = theta_real_deg;

    // ================= PERFIL DE POSICION =================
    if (modo_control == "posicion" && movimiento_activo) {

      float t = (tiempo_actual - tiempo_inicio_perfil) / 1000.0;

      if (t <= TF_SEGUNDOS) {
        // Generar referencia suave en grados
        theta_ref_deg = theta_inicial_deg + calcularPerfilPosicion(t, TF_SEGUNDOS, delta_theta_deg);
        // Normalizar
        while (theta_ref_deg < 0) theta_ref_deg += 360;
        while (theta_ref_deg >= 360) theta_ref_deg -= 360;
        theta_ref_rad = theta_ref_deg * (PI / 180.0);
        
        float tau = t / TF_SEGUNDOS;
        omega_ref = (delta_theta_rad / TF_SEGUNDOS) * (30*tau*tau - 60*tau*tau*tau + 30*tau*tau*tau*tau);
      } else {
        movimiento_activo = false;
        theta_ref_deg = theta_inicial_deg + delta_theta_deg;
        while (theta_ref_deg < 0) theta_ref_deg += 360;
        while (theta_ref_deg >= 360) theta_ref_deg -= 360;
        theta_ref_rad = theta_ref_deg * (PI / 180.0);
        omega_ref = 0;
        Serial.println("Posicion COMPLETADA");
      }
    }

    // ================= CALCULAR ERROR Y CONTROL =================
    float salida_control = 0;
    
    if (modo_control == "posicion" && movimiento_activo) {
      // Control de posicion con error corto
      float error_posicion_deg = calcularErrorCorto(theta_ref_deg, theta_real_deg);
      float error_posicion_rad = error_posicion_deg * (PI / 180.0);
      float omega_target = omega_ref + (Kp_pos * error_posicion_rad);
      
      float error_velocidad = omega_target - omega_real_rads;
      error_integral_vel += error_velocidad * Ts;
      error_integral_vel = constrain(error_integral_vel, -50, 50);
      
      salida_control = (Kp_vel * error_velocidad) + (Ki_vel * error_integral_vel);
    } 
    else if (modo_control == "manual" || modo_control == "perfil_vel") {
      // Control de velocidad manual
      float error_velocidad = setpoint_vel - omega_real_rads;
      error_integral_vel += error_velocidad * Ts;
      error_integral_vel = constrain(error_integral_vel, -50, 50);
      salida_control = (Kp_vel * error_velocidad) + (Ki_vel * error_integral_vel);
    }

    // ================= FRICCION =================
    float pwm_minimo = 30.0;
    float pwm_compensado = 0;
    
    float error_actual = 0;
    if (modo_control == "posicion" && movimiento_activo) {
      error_actual = calcularErrorCorto(theta_ref_deg, theta_real_deg);
    } else {
      error_actual = setpoint_rpm - omega_real_rpm;
    }

    if ((movimiento_activo && abs(error_actual) > 1.0) || 
        ((modo_control == "manual" || modo_control == "perfil_vel") && abs(setpoint_rpm) > 5)) {
      if (salida_control > 0.1) {
        pwm_compensado = salida_control + pwm_minimo;
      } else if (salida_control < -0.1) {
        pwm_compensado = salida_control - pwm_minimo;
      } else {
        pwm_compensado = salida_control;
      }
    } else {
      pwm_compensado = salida_control;
    }

    pwm_compensado = constrain(pwm_compensado, -255, 255);
    int pwm_final = abs(pwm_compensado);

    // ================= DIRECCION =================
    if (pwm_compensado >= 0) {
      digitalWrite(PIN_DIR1, LOW);
      digitalWrite(PIN_DIR2, HIGH);
    } else {
      digitalWrite(PIN_DIR1, HIGH);
      digitalWrite(PIN_DIR2, LOW);
    }

    // ================= PARADA =================
    bool detener = false;
    if (modo_control == "posicion" && !movimiento_activo && abs(calcularErrorCorto(theta_ref_deg, theta_real_deg)) < 1.0) {
      detener = true;
    } else if ((modo_control == "manual" || modo_control == "perfil_vel") && abs(setpoint_rpm) < 1 && abs(omega_real_rpm) < 5) {
      detener = true;
    }
    
    if (detener) {
      pwm_final = 0;
      error_integral_vel = 0;
    }

    ledcWrite(PIN_PWM, pwm_final);

    tiempo_previo = tiempo_actual;
  }
  
  // Debug cada 3 segundos
  static unsigned long last_debug = 0;
  if (tiempo_actual - last_debug >= 3000) {
    last_debug = tiempo_actual;
    Serial.printf("Pos: %.1f° | RPM: %.1f | Modo: %s | Setpoint RPM: %.1f\n", 
                  theta_real_deg, 
                  omega_real_rpm, 
                  modo_control.c_str(),
                  setpoint_rpm);
  }
}