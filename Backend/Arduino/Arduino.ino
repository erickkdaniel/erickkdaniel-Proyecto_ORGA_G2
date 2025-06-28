#include <Servo.h> // Incluimos la librería para controlar servomotores
#include <LiquidCrystal_I2C.h>

//------ INICIALIZAR EL OBJETO DE LA PANTALLA
LiquidCrystal_I2C lcd(0x27, 16, 2);

//------ PINES Y CONFIGURACIÓN PARA SENSORES ULTRASÓNICOS Y SERVOMOTORES
const int LDR_PARQUEO_ENTRADA_PIN = A4;   // Pin para detectar entrada

const int servoPin1 = 6;  // Pin de señal del Servomotor 1

//const int LDR_PARQUEO_ENTRADA_PIN = A4;   // Pin para detectar entrada

const int LDR_PARQUEO_SALIDA_PIN = A5;   // Pin para detectar salida
const int servoPin2 = 3;  // Pin de señal del Servomotor 2
const int PBuzzer = 11;   // Pin para alarma en modo pánico

// Objetos Servo
Servo miServo1;
Servo miServo2;

// Variables para control de servos
unsigned long ultimoMovimientoServo1 = 0;
unsigned long ultimoMovimientoServo2 = 0;
const long TIEMPO_MINIMO_ENTRE_ACTIVACIONES = 1000; // 1 segundo entre activaciones
const long TIEMPO_ABIERTO = 5000; // Tiempo que el servo permanece abierto (ms)

//------ PINES Y CONFIGURACIÓN PARA FOTORRESISTENCIAS (PARQUEOS)
const int LDR_PARQUEO_1_PIN = A0;
const int LDR_PARQUEO_2_PIN = A1;
const int LDR_PARQUEO_3_PIN = A2;
const int LDR_PARQUEO_4_PIN = A3;

// Umbrales de luz para detectar "ocupado" o "libre"
const int UMBRAL_LUZ_SERVO_ENTRADA = 784; // Ajustado según recomendación previa
const int UMBRAL_LUZ_SERVO_SALIDA = 787;  // Ajustado según recomendación previa

// const int UMBRAL_LUZ_SERVO_ENTRADA = 1016; // Ajustado según recomendación previa
// const int UMBRAL_LUZ_SERVO_SALIDA = 1016;  // Ajustado según recomendación previa

const int UMBRAL_LUZ_PARQUEO1 = 4;
const int UMBRAL_LUZ_PARQUEO2 = 8;
const int UMBRAL_LUZ_PARQUEO3 = 15;
const int UMBRAL_LUZ_PARQUEO4 = 1015;


// Variables para almacenar los valores de las fotorresistencias
int valorLDR_P1, valorLDR_P2, valorLDR_P3, valorLDR_P4;

// Estados de los parqueos para enviar al backend (true = ocupado, false = libre)
bool estadoParqueo1 = false;
bool estadoParqueo2 = false;
bool estadoParqueo3 = false;
bool estadoParqueo4 = false;

//------ CONTADOR
const int Oc0 = 10; // Pines para ocupados
const int Oc1 = 2;
const int Oc2 = 9;

const int Lib0 = 4; // Pines para libres
const int Lib1 = 7;
const int Lib2 = 8;
int NumeroOcupados = 0;
int NumeroLibres = 0;
int VO0, VO1, VO2 = 0;
int VL0, VL1, VL2 = 0;

const int PContadorEntrada = 12; // Pin para pulsos de entrada (reloj)
const int PContadorSalida = 13;  // Pin para pulsos de salida (reset)

volatile int contadorP = 0;

//------ COMUNICACIÓN SERIAL Y MODOS
const int bufferSize = 64; // Aumentado para mayor robustez
char receivedBuffer[bufferSize];
byte bufferIndex = 0;
String currentMode = "normal"; // Modo actual: normal, panic, nocturnal, maintenance, evacuation
String lastMode = ""; // Para detectar cambios de modo
unsigned long ultimoEnvioParqueos = 0;
unsigned long ultimoEnvioServos = 0;
unsigned long ultimoCambioBuzzer = 0; // Para el parpadeo del buzzer en evacuación
const long INTERVALO_ENVIO_PARQUEOS = 10000; // 10 segundos
const long INTERVALO_ENVIO_SERVOS = 500;    // 0.5 segundos
unsigned long ultimoEnvioServo1 = 0; // Temporizador para servo 1
unsigned long ultimoEnvioServo2 = 0; // Temporizador para servo 2
const long INTERVALO_BUZZER = 500;          // 500ms para el parpadeo del buzzer
const int NUM_LECTURAS_PROMEDIO = 5;        // Número de lecturas para promedio

bool serv1 = false;
bool serv2 = false;

void setup() {
  // Inicializar la pantalla LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sistema iniciado");
  delay(2000);
  lcd.clear();

  // Configurar pines
  pinMode(Oc0, INPUT);
  pinMode(Oc1, INPUT);
  pinMode(Oc2, INPUT);
  pinMode(Lib0, INPUT);
  pinMode(Lib1, INPUT);
  pinMode(Lib2, INPUT);
  pinMode(PBuzzer, OUTPUT);
  pinMode(PContadorEntrada, OUTPUT);
  pinMode(PContadorSalida, OUTPUT);

  // Inicializar pines del contador
  digitalWrite(PContadorEntrada, LOW);
  digitalWrite(PContadorSalida, HIGH); // Asumimos HIGH como estado inactivo para reset

  // Adjuntar servomotores
  miServo1.attach(servoPin1);
  miServo2.attach(servoPin2);
  
  miServo1.write(90); // Servo 1 en 0° (cerrado)
  miServo2.write(90); // Servo 2 en 0° (cerrado)

  digitalWrite(PBuzzer, LOW); // Buzzer apagado

  // Iniciar comunicación serial
  Serial.begin(9600);
  Serial.println("{\"status\":\"Sistema de control de acceso y parqueo iniciado.\"}");

  // Prueba de servos (para depuración)
  // miServo1.write(90);
  // delay(1000);
  // miServo1.write(0);
  // miServo2.write(90);
  // delay(1000);
  // miServo2.write(0);
}

int leerLDRPromedio(int ldrPin) {
  long suma = 0;
  for (int i = 0; i < NUM_LECTURAS_PROMEDIO; i++) {
    suma += analogRead(ldrPin);
    delay(10); // Pequeña pausa entre lecturas
  }
  int promedio = suma / NUM_LECTURAS_PROMEDIO;
  Serial.print("LDR Servo Pin "); Serial.print(ldrPin); Serial.print(": "); Serial.println(promedio);
  return promedio;
}

bool estaParqueoOcupado(int ldrPin, int umbral) {
  int valorLDR = analogRead(ldrPin);
  Serial.print("LDR Pin "); Serial.print(ldrPin); Serial.print(": "); Serial.println(valorLDR);
  return (valorLDR < umbral);
}

bool estaServoOcupado(int ldrPin, int umbral) {
  int valorLDR = leerLDRPromedio(ldrPin);
  bool ocupado = (valorLDR <= umbral);
  Serial.print("Servo "); Serial.print(ldrPin == LDR_PARQUEO_ENTRADA_PIN ? "1" : "2");
  Serial.print(" ocupado: "); Serial.println(ocupado ? "true" : "false");
  return ocupado;
}

int valorRecibido(int pin) {
  return digitalRead(pin);
}

int valorReal(int v0, int v1, int v2) {
  return (v0 * 1) + (v1 * 2) + (v2 * 4);
}

void enviarPulsos(int cant, int pinContador, int pinReset) {
  if (cant > 0) {
    Serial.print("Enviando "); Serial.print(cant); Serial.println(" pulsos de entrada");
    for (int i = 0; i < cant; i++) {
      Serial.println(i);
      digitalWrite(pinContador, HIGH);
      delay(50); // Pulso de 50ms
      digitalWrite(pinContador, LOW);
      delay(50);
    }
  }
}

// void enviarResetContador(int pinReset) {
//   Serial.println("Enviando pulso de reset al contador");
//   digitalWrite(pinReset, HIGH);
//   delay(50);
//   digitalWrite(pinReset, LOW);
//   delay(50);
//   digitalWrite(pinReset, HIGH); // Restaurar estado inactivo
// }

void configurarModo(String mode) {
  Serial.print("Configurando modo: "); Serial.println(mode);
  if (mode == "panic") {
    miServo1.write(90);
    miServo2.write(90);
    digitalWrite(PBuzzer, HIGH);
  } else if (mode == "nocturnal") {
    miServo1.write(90);
    miServo2.write(90);
    digitalWrite(PBuzzer, LOW);
  } else if (mode == "evacuation") {
    miServo1.write(0);
    miServo2.write(0);
    digitalWrite(PBuzzer, LOW); // Buzzer manejado en loop para parpadeo
  } else if (mode == "maintenance") {
    miServo1.write(90);
    digitalWrite(PBuzzer, LOW);
  } else if (mode == "normal") {
    miServo1.write(90);
    miServo2.write(90);
    digitalWrite(PBuzzer, LOW);
  }
  updateLCD(mode);
}

void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (bufferIndex < bufferSize - 1) {
      receivedBuffer[bufferIndex++] = inChar;
    }
    if (inChar == '\n') {
      receivedBuffer[bufferIndex] = '\0';
      String command = String(receivedBuffer);
      command.trim();
      Serial.print("Comando recibido: ["); Serial.print(command); Serial.println("]");
      if (command == "PANIC-ON") {
        currentMode = "panic";
        Serial.println("{\"mode\":\"panic\"}");
        configurarModo("panic");
      } else if (command == "NOCTURNAL-ON") {
        currentMode = "nocturnal";
        Serial.println("{\"mode\":\"nocturnal\"}");
        configurarModo("nocturnal");
      } else if (command == "MAINTENANCE-ON") {
        currentMode = "maintenance";
        Serial.println("{\"mode\":\"maintenance\"}");
        configurarModo("maintenance");
      } else if (command == "EVACUATION-ON") {
        currentMode = "evacuation";
        Serial.println("{\"mode\":\"evacuation\"}");
        configurarModo("evacuation");
      } else if (command == "NORMAL-ON") {
        currentMode = "normal";
        Serial.println("{\"mode\":\"normal\"}");
        configurarModo("normal");
      } else {
        Serial.println("{\"error\":\"Comando de modo inválido: " + command + "\"}");
      }
      bufferIndex = 0;
      memset(receivedBuffer, 0, bufferSize);
    }
  }
}

void updateLCD(String mode) {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (mode == "normal") {

      VO0 = valorRecibido(Oc0);
      VO1 = valorRecibido(Oc1);
      VO2 = valorRecibido(Oc2);
      NumeroOcupados = valorReal(VO0, VO1, VO2);

      VL0 = valorRecibido(Lib0);
      VL1 = valorRecibido(Lib1);
      VL2 = valorRecibido(Lib2);
      NumeroLibres = valorReal(VL0, VL1, VL2);

    lcd.print("Ocupados:"); lcd.print(NumeroOcupados);
    lcd.setCursor(0, 1);
    lcd.print("Libres:"); lcd.print(NumeroLibres);
  } else {
    lcd.print("MODO");
    lcd.setCursor(0, 1);
    if (mode == "panic") {
      lcd.print("PANICO");
    } else if (mode == "nocturnal") {
      lcd.print("NOCHE");
    } else if (mode == "maintenance") {
      lcd.print("MANTENIM.");
    } else if (mode == "evacuation") {
      lcd.print("EVACUACION");
    }
  }
}

void manejarServo(Servo &servo, unsigned long &ultimoMovimiento, int ldrPin, int umbral, int servoNum) {
  unsigned long currentTime7 = millis();
  // Verificar si se puede activar el servo
  if (ldrPin != -1 && estaServoOcupado(ldrPin, umbral) && (currentTime7 - ultimoMovimiento >= TIEMPO_MINIMO_ENTRE_ACTIVACIONES)) {
    Serial.print("FOTORESISTENCIA "); Serial.print(servoNum); Serial.println(": Objeto detectado. Abriendo Servomotor.");
    servo.write(0); // Abrir servo a 90°
    delay(TIEMPO_ABIERTO); // Mantener abierto
    Serial.print("FOTORESISTENCIA "); Serial.print(servoNum); Serial.println(": Cerrando Servomotor.");
    servo.write(90); // Cerrar servo a 0°
    ultimoMovimiento = millis(); // Actualizar tiempo del último movimiento
    
  }
}

void loop() {
  // Procesar comandos seriales
  serialEvent();

  // Lógica según el modo actual
  if (currentMode == "normal") {
    // Lógica para servos
    unsigned long currentTime4 = millis();
   if (currentTime4 - ultimoEnvioServo1 >= INTERVALO_ENVIO_SERVOS) {
    ultimoEnvioServo1 = currentTime4;
    manejarServo(miServo1, ultimoMovimientoServo1, LDR_PARQUEO_ENTRADA_PIN, UMBRAL_LUZ_SERVO_ENTRADA, 1);
  }
   unsigned long currentTime5 = millis();
 
  // Manejo del servo 2
  if (currentTime5 - ultimoEnvioServo2 >= INTERVALO_ENVIO_SERVOS) {
    ultimoEnvioServo2 = currentTime5;
    manejarServo(miServo2, ultimoMovimientoServo2, LDR_PARQUEO_SALIDA_PIN, UMBRAL_LUZ_SERVO_SALIDA, 2);
  }


    // Lógica para parqueos y contador
    unsigned long currentTime = millis();
    if (currentTime - ultimoEnvioParqueos >= INTERVALO_ENVIO_PARQUEOS) {
      ultimoEnvioParqueos = currentTime;

      // Leer estado de los parqueos
      estadoParqueo1 = estaParqueoOcupado(LDR_PARQUEO_1_PIN, UMBRAL_LUZ_PARQUEO1);
      estadoParqueo2 = estaParqueoOcupado(LDR_PARQUEO_2_PIN, UMBRAL_LUZ_PARQUEO2);
      estadoParqueo3 = estaParqueoOcupado(LDR_PARQUEO_3_PIN, UMBRAL_LUZ_PARQUEO3);
      estadoParqueo4 = estaParqueoOcupado(LDR_PARQUEO_4_PIN, UMBRAL_LUZ_PARQUEO4);

    Serial.print("p1 actual");Serial.println(estadoParqueo1);
    Serial.print("p2 actual");Serial.println(estadoParqueo2);
    Serial.print("p3 actual");Serial.println(estadoParqueo3);
    Serial.print("p4 actual");Serial.println(estadoParqueo4);
      
      // Acumular pulsos de entrada
      if (estadoParqueo1 == false ) {
        Serial.println("PULSO ENTRADA: P1 Ocupado");
        Serial.println("Si1");
        contadorP++;
      }
      if (estadoParqueo2 == false) {
        Serial.println("PULSO ENTRADA: P2 Ocupado");
        Serial.println("Si2");
        contadorP++;
      }
      if (estadoParqueo3 == false) {
        Serial.println("PULSO ENTRADA: P3 Ocupado");
        Serial.println("Si3");
        contadorP++;
      }
      if (estadoParqueo4==false) {
        Serial.println("PULSO ENTRADA: P4 Ocupado");
        Serial.println("Si4");
        contadorP++;
      }
      
      // // Actualizar estados
      // estadoParqueo1 = currentEstadoParqueo1;
      // estadoParqueo2 = currentEstadoParqueo2;
      // estadoParqueo3 = currentEstadoParqueo3;
      // estadoParqueo4 = currentEstadoParqueo4;

      // Leer pines del contador
      VO0 = valorRecibido(Oc0);
      VO1 = valorRecibido(Oc1);
      VO2 = valorRecibido(Oc2);
      NumeroOcupados = valorReal(VO0, VO1, VO2);

      VL0 = valorRecibido(Lib0);
      VL1 = valorRecibido(Lib1);
      VL2 = valorRecibido(Lib2);
      NumeroLibres = valorReal(VL0, VL1, VL2);

      Serial.print("NumeroOcupados: "); Serial.println(NumeroOcupados);
      Serial.print("NumeroLibres: "); Serial.println(NumeroLibres);
      Serial.print("ValorFoto: "); Serial.println(contadorP);
        bool aux1 = true;
        while(aux1 = true){
          
      VL0 = valorRecibido(Lib0);
      VL1 = valorRecibido(Lib1);
      VL2 = valorRecibido(Lib2);
      NumeroLibres = valorReal(VL0, VL1, VL2);

        if(NumeroLibres == 4){
          digitalWrite(PContadorSalida, HIGH);
          delay(50);
          digitalWrite(PContadorSalida, LOW);
          delay(50);
          digitalWrite(PContadorSalida, HIGH); // Restaurar estado inactivo
          aux1 = false;
          return;
         }else{
          aux1 = true;
          enviarPulsos(1, PContadorEntrada,PContadorSalida);
   
        }
        }
      // if(NumeroLibres == 4){
      // enviarPulsos(5-NumeroOcupados, PContadorEntrada,PContadorSalida);
      // digitalWrite(PContadorSalida, HIGH);
      // delay(50);
      // digitalWrite(PContadorSalida, LOW);
      // delay(50);
      // digitalWrite(PContadorSalida, HIGH); // Restaurar estado inactivo
      // digitalWrite(PContadorSalida, LOW);
      // delay(50);
      // digitalWrite(PContadorSalida, HIGH); // Restaurar estado inactivo
      //  delay(1000);
      // }else{
      //   Serial.println("No envia ticks");
      // }


    // enviarPulsos(contadorP,PContadorEntrada,PContadorSalida);
      
      // Actualizar LCD
      updateLCD("normal");
      contadorP=0;

      // Enviar JSON al servidor
      Serial.print("{\"p1\":"); Serial.print(estadoParqueo1 ? "true" : "false");
      Serial.print(",\"p2\":"); Serial.print(estadoParqueo2 ? "true" : "false");
      Serial.print(",\"p3\":"); Serial.print(estadoParqueo3 ? "true" : "false");
      Serial.print(",\"p4\":"); Serial.print(estadoParqueo4 ? "true" : "false");
      Serial.println("}");

      }
  } else if (currentMode == "panic") {
    // Configuración ya manejada en serialEvent
    // No se procesan servos ni parqueos
  } else if (currentMode == "nocturnal") {
    // Configuración ya manejada en serialEvent
    // No se procesan servos ni parqueos
  } else if (currentMode == "maintenance") {
    // Lógica para servo de salida
    unsigned long currentTime1 = millis();
    if (currentTime1 - ultimoEnvioServos >= INTERVALO_ENVIO_SERVOS) {
      ultimoEnvioServos = currentTime1;
      manejarServo(miServo2, ultimoMovimientoServo2, LDR_PARQUEO_SALIDA_PIN, UMBRAL_LUZ_SERVO_SALIDA, 2);
    }

    // Lógica para parqueos y contador
    unsigned long currentTime = millis();
    if (currentTime - ultimoEnvioParqueos >= INTERVALO_ENVIO_PARQUEOS) {
      ultimoEnvioParqueos = currentTime;

      bool currentEstadoParqueo1 = estaParqueoOcupado(LDR_PARQUEO_1_PIN, UMBRAL_LUZ_PARQUEO1);
      bool currentEstadoParqueo2 = estaParqueoOcupado(LDR_PARQUEO_2_PIN, UMBRAL_LUZ_PARQUEO2);
      bool currentEstadoParqueo3 = estaParqueoOcupado(LDR_PARQUEO_3_PIN, UMBRAL_LUZ_PARQUEO3);
      bool currentEstadoParqueo4 = estaParqueoOcupado(LDR_PARQUEO_4_PIN, UMBRAL_LUZ_PARQUEO4);

      // Acumular pulsos de entrada
      if (currentEstadoParqueo1 && !estadoParqueo1) {
        Serial.println("PULSO ENTRADA: P1 Ocupado");
        contadorP++;
      }
      if (currentEstadoParqueo2 && !estadoParqueo2) {
        Serial.println("PULSO ENTRADA: P2 Ocupado");
        contadorP++;
      }
      if (currentEstadoParqueo3 && !estadoParqueo3) {
        Serial.println("PULSO ENTRADA: P3 Ocupado");
        contadorP++;
      }
      if (currentEstadoParqueo4 && !estadoParqueo4) {
        Serial.println("PULSO ENTRADA: P4 Ocupado");
        contadorP++;
      }

      // Actualizar estados
      estadoParqueo1 = currentEstadoParqueo1;
      estadoParqueo2 = currentEstadoParqueo2;
      estadoParqueo3 = currentEstadoParqueo3;
      estadoParqueo4 = currentEstadoParqueo4;

      // Leer pines del contador
      VO0 = valorRecibido(Oc0);
      VO1 = valorRecibido(Oc1);
      VO2 = valorRecibido(Oc2);
      NumeroOcupados = valorReal(VO0, VO1, VO2);
      VL0 = valorRecibido(Lib0);
      VL1 = valorRecibido(Lib1);
      VL2 = valorRecibido(Lib2);
      NumeroLibres = valorReal(VL0, VL1, VL2);
      Serial.print("NumeroOcupados: "); Serial.println(NumeroOcupados);
      Serial.print("NumeroLibres: "); Serial.println(NumeroLibres);
      Serial.print("OcupadosFoto: "); Serial.println(contadorP);
      // Enviar JSON al servidor
      Serial.print("{\"p1\":"); Serial.print(estadoParqueo1 ? "true" : "false");
      Serial.print(",\"p2\":"); Serial.print(estadoParqueo2 ? "true" : "false");
      Serial.print(",\"p3\":"); Serial.print(estadoParqueo3 ? "true" : "false");
      Serial.print(",\"p4\":"); Serial.print(estadoParqueo4 ? "true" : "false");
      Serial.println("}");

    }
  } else if (currentMode == "evacuation") {
    // Buzzer intermitente
    unsigned long currentTime = millis();
    if (currentTime - ultimoCambioBuzzer >= INTERVALO_BUZZER) {
      ultimoCambioBuzzer = currentTime;
      digitalWrite(PBuzzer, !digitalRead(PBuzzer)); // Alternar estado del buzzer
      Serial.print("Buzzer state: "); Serial.println(digitalRead(PBuzzer) ? "ON" : "OFF");
    }

    // Lógica para parqueos (sin pulsos)
    if (currentTime - ultimoEnvioParqueos >= INTERVALO_ENVIO_PARQUEOS) {
      ultimoEnvioParqueos = currentTime;

      bool currentEstadoParqueo1 = estaParqueoOcupado(LDR_PARQUEO_1_PIN, UMBRAL_LUZ_PARQUEO1);
      bool currentEstadoParqueo2 = estaParqueoOcupado(LDR_PARQUEO_2_PIN, UMBRAL_LUZ_PARQUEO2);
      bool currentEstadoParqueo3 = estaParqueoOcupado(LDR_PARQUEO_3_PIN, UMBRAL_LUZ_PARQUEO3);
      bool currentEstadoParqueo4 = estaParqueoOcupado(LDR_PARQUEO_4_PIN, UMBRAL_LUZ_PARQUEO4);

      // Actualizar estados
      estadoParqueo1 = currentEstadoParqueo1;
      estadoParqueo2 = currentEstadoParqueo2;
      estadoParqueo3 = currentEstadoParqueo3;
      estadoParqueo4 = currentEstadoParqueo4;

      // Enviar JSON al servidor
      Serial.print("{\"p1\":"); Serial.print(estadoParqueo1 ? "true" : "false");
      Serial.print(",\"p2\":"); Serial.print(estadoParqueo2 ? "true" : "false");
      Serial.print(",\"p3\":"); Serial.print(estadoParqueo3 ? "true" : "false");
      Serial.print(",\"p4\":"); Serial.print(estadoParqueo4 ? "true" : "false");
      Serial.println("}");
    }
  }
  delay(50);
}