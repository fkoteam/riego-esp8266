/*
   RIEGO AUTOMATICO
   Cada MINUTOS_MUESTRA, miramos la humedad. Tomamos NUM_MUESTRAS y hacemos la media para mayor precision. Si es inferior a HUMEDAD_MIN, regamos durante SEGUNDOS_RIEGO. Si han pasado DIAS_MAX desde el último riego, regamos igualmente.
   Nos aseguramos que entre riego y riego pasan un mínimo de HORAS_ENTRE_RIEGO_MIN.
  Una vez encendido, podemos conectarnos por wifi para configurar el riego a la dirección 192.168.1.2
  El wifi está 20 minutos activo y luego se apaga. Para reactivarlo, hay que pulsar el boton flash.
*/
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <TimeLib.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EasyButton.h>

//Boton para reactivar el wifi. El botón 0 es el boton FLASH de la esp8266
#define BUTTON_PIN 0
EasyButton button(BUTTON_PIN);

//Configuramos el wifi en modo AP, puerto 80, dirección 192.168.1.2, contrasenya petisus, nombre RIEGO
ESP8266WebServer server(80);
IPAddress ip(192, 168, 1, 2);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
String contrasenya = "petisuis";

//pin donde va conectado el sensor de humedad. Es el A0, unica entrada analogica del esp8266
const int AnalogIn  = A0;

//PIN de control del motor. 
#define PWM1   D1
//PIN de control del motor. 
#define PWM2   D2

//Los sensores de humedad se estropean si están todo el rato encendidos. Esto es porque los electrodos hacen una especie de electrolisis. Así que tomamos muestras sólo cada X minutos. Este PIN controla el encendido/apagado del sensor.
#define HUM   D6

//builtin led. Encendido por defecto, apagado mientras riega
int salida = 2;


boolean regando = false;
boolean online = false;

/*INICIO. VARIABLES CONFIGURABLES POR LA WEB */
int MINUTOS_MUESTRA = 5; //eeprom pos 50
int HUMEDAD_MIN = 730; //eeprom pos 60
int SEGUNDOS_RIEGO = 60; //eeprom pos 70
int DIAS_MAX = 4; //eeprom pos 80
int HORAS_ENTRE_RIEGO_MIN = 24; //eeprom pos 90
int NUM_MUESTRAS = 10; //eeprom pos 100
int contador_muestras_humedad = 0; //eeprom pos 110. Esta variable va de 0-9 y sirve para saber en qué posición debemos guardar la muestra en la eeprom
int contador_riegos = 0; //eeprom pos 120. Esta variable va de 0-9 y sirve para saber en qué posición debemos guardar el registro del riego en la eeprom
int eepromInicializada = 0; //eeprom pos 130. Está a 1 si la placa está inicializada

int dia = 1; //eeprom pos 1
int mes = 1; //eeprom pos 10
int anyo = 20; //hay que sumarle 2000. eeprom pos 20
int hora = 0; //eeprom pos 30
int minuto = 0; //eeprom pos 40
/*FIN. VARIABLES CONFIGURABLES POR LA WEB */

//el wifi permanece encendido durante 20 minutos Luego se apaga y para reactivarlo hay que presionar el boton FLASH de la esp8266
int MINUTOS_ONLINE = 20;

unsigned long cuandoOnline = 0;
unsigned long ultimoRiego = 0;
unsigned long ultimaLecturaHumedad = 0;




void setup(void)
{
  gpio_init(); // Initilise GPIO pins
  pinMode(salida, OUTPUT);
  EEPROM.begin(512);
  setSyncProvider( requestSync);  //set function to call when sync required
  setTime(hora, minuto, 0, dia, mes, anyo); // alternative to above, yr is 2 or 4 digit yr (2010 or 10 sets year to 2010)
  leerEeprom(); //Lee las variables configurables de la Eeprom y si tienen valores inválidos, las inicializa

  pinMode(PWM1, OUTPUT);
  pinMode(PWM2, OUTPUT);
  pinMode(HUM, OUTPUT);

  // Initialize the button.
  button.begin();
  // Add the callback function to be called when the button is pressed. En este caso, rectivamos el wifi
  button.onPressed(onPressed);

  analogWriteRange(255);  // set PWM output range from 0 to 255
  Serial.begin(115200);
  delay(100);
  //inicilizamos el wifi. Permanecerá activo 20 minutos
  initWifi();

}


// main loop
void loop()
{
  delay(1);
  button.read();
  server.handleClient();

  //paramos riego si se está regando y ha pasado el tiempo definido
  if (regando && millis() > (ultimoRiego + (SEGUNDOS_RIEGO * 1000))) {
    pararRiego();
  }

  //paramos el wifi si la placa está online y ha pasado el tiempo definido
  if (online && millis() > (cuandoOnline + (MINUTOS_ONLINE * 60 *  1000))) {
    light_sleep();
  }

  //tomamos muestras de humedad al iniciar o segun el tiempo estipulado. Si ya se ha regado recientemente, no se toma la humedad
  if (ultimaLecturaHumedad == 0 || ((millis() > (ultimaLecturaHumedad + (MINUTOS_MUESTRA * 60 * 1000))) && (millis() > ultimoRiego + (HORAS_ENTRE_RIEGO_MIN * 60 * 60 * 1000))))
  {
    int humedad = compruebaHumedad();


    //si la humedad está por debajo del número establecido, regamos
    if ((humedad > HUMEDAD_MIN) || (millis() > (ultimoRiego + (DIAS_MAX * 24 * 60 * 60 * 1000))))
    {
      regar();

    }
  }



}


//inicializa el wifi como AP
void initWifi()
{
  Serial.println("Iniciando wifi...");

  WiFi.mode(WIFI_AP);
  while (!WiFi.softAP("riego", contrasenya))
  {
    Serial.println(".");
    delay(100);
  }
  WiFi.softAPConfig(ip, gateway, subnet);

  Serial.print("Iniciado AP ");
  Serial.println("riego");
  Serial.print("IP address:\t");
  Serial.println(WiFi.softAPIP());
  server.on("/", handleRoot);      //Which routine to handle at root location

  server.begin();                  //Start server
  cuandoOnline = millis();
  online = true;
}


//apaga wifi
void light_sleep() {
  Serial.println("Inicio sleep.");

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  online = false;
}


//inicia el riego
void regar()
{
  light_sleep();
  Serial.println("Inicio Riego.");

  digitalWrite(PWM1, LOW);//riego encendido / apagado
  digitalWrite(PWM2, HIGH);
  digitalWrite(salida, HIGH);//apagamos builtin led
  ultimoRiego = millis();
  regando = true;
  guardaEepromRiego(now()); //guardamos las fechas de los últimos 10 riegos
}

//paramos riego
void pararRiego()
{
  initWifi();
  Serial.println("Fin Riego.");

  //paramos riego
  digitalWrite(PWM1, LOW);
  digitalWrite(PWM2, LOW);    
  digitalWrite(salida, LOW);
  regando = false;
}

//Lee las variables configurables de la Eeprom y si tienen valores inválidos, las inicializa
void leerEeprom()
{
  EEPROM.get(50, MINUTOS_MUESTRA);
  if (MINUTOS_MUESTRA < 1 || MINUTOS_MUESTRA > 59)
  {
    MINUTOS_MUESTRA = 5;
    EEPROM.put(50, MINUTOS_MUESTRA);
    EEPROM.commit();
  }
  EEPROM.get(60, HUMEDAD_MIN);
  if (HUMEDAD_MIN < 1 || HUMEDAD_MIN > 1000)
  {
    HUMEDAD_MIN = 730;
    EEPROM.put(60, HUMEDAD_MIN);
    EEPROM.commit();
  }
  EEPROM.get(70, SEGUNDOS_RIEGO);
  if (SEGUNDOS_RIEGO < 1 || SEGUNDOS_RIEGO > 200)
  {
    SEGUNDOS_RIEGO = 60;
    EEPROM.put(70, SEGUNDOS_RIEGO);
    EEPROM.commit();
  }
  EEPROM.get(80, DIAS_MAX);
  if (DIAS_MAX < 1 || DIAS_MAX > 30)
  {
    DIAS_MAX = 4;
    EEPROM.put(80, DIAS_MAX);
    EEPROM.commit();
  }
  EEPROM.get(90, HORAS_ENTRE_RIEGO_MIN);
  if (HORAS_ENTRE_RIEGO_MIN < 1 || HORAS_ENTRE_RIEGO_MIN > 300)
  {
    HORAS_ENTRE_RIEGO_MIN = 24;
    EEPROM.put(90, HORAS_ENTRE_RIEGO_MIN);
    EEPROM.commit();
  }
  EEPROM.get(100, NUM_MUESTRAS);
  if (NUM_MUESTRAS < 1 || NUM_MUESTRAS > 100)
  {
    NUM_MUESTRAS = 10;
    EEPROM.put(100, NUM_MUESTRAS);
    EEPROM.commit();
  }
  EEPROM.get(110, contador_muestras_humedad);
  if (contador_muestras_humedad < 0 || contador_muestras_humedad > 9)
  {
    contador_muestras_humedad = 0;
    EEPROM.put(110, contador_muestras_humedad);
    EEPROM.commit();
  }
  EEPROM.get(120, contador_riegos);
  if (contador_riegos < 0 || contador_riegos > 9)
  {
    contador_riegos = 0;
    EEPROM.put(120, contador_riegos);
    EEPROM.commit();
  }

  EEPROM.get(130, eepromInicializada);
  if (eepromInicializada != 1)
  {
    inicializarEeprom();
    eepromInicializada = 1;
    EEPROM.put(130, eepromInicializada);
    EEPROM.commit();
  }

}


//muestra la web de configuración y recoge los parámetros de configuración cuando se envían
void handleRoot()
{
  if (server.hasArg("dia") && server.hasArg("hora")) {
    hora = getValue(server.arg("hora"), ':', 0).toInt(); // sino probar con %3A
    minuto = getValue(server.arg("hora"), ':', 1).toInt();
    dia = getValue(server.arg("dia"), '-', 2).toInt();
    mes = getValue(server.arg("dia"), '-', 1).toInt();
    anyo = getValue(server.arg("dia"), '-', 0).toInt();

    setTime(hora, minuto, 0, dia, mes, anyo); // alternative to above, yr is 2 or 4 digit yr
    digitalClockDisplay();
  }
  if (server.hasArg("MINUTOS_MUESTRA"))
  {
    MINUTOS_MUESTRA = server.arg("MINUTOS_MUESTRA").toInt();
    EEPROM.put(50, MINUTOS_MUESTRA);
    EEPROM.commit();
    Serial.println("Nuevo parametro MINUTOS_MUESTRA: " + String(MINUTOS_MUESTRA));
  }
  if (server.hasArg("NUM_MUESTRAS")) {
    NUM_MUESTRAS = server.arg("NUM_MUESTRAS").toInt();
    EEPROM.put(100, NUM_MUESTRAS);
    EEPROM.commit();
    Serial.println("Nuevo parametro NUM_MUESTRAS: " + String(NUM_MUESTRAS));
  }
  if (server.hasArg("HUMEDAD_MIN")) {
    HUMEDAD_MIN = server.arg("HUMEDAD_MIN").toInt();
    EEPROM.put(60, HUMEDAD_MIN);
    EEPROM.commit();
    Serial.println("Nuevo parametro HUMEDAD_MIN: " + String(HUMEDAD_MIN));
  }

  if (server.hasArg("SEGUNDOS_RIEGO")) {
    SEGUNDOS_RIEGO = server.arg("SEGUNDOS_RIEGO").toInt();
    EEPROM.put(70, SEGUNDOS_RIEGO);
    EEPROM.commit();
    Serial.println("Nuevo parametro SEGUNDOS_RIEGO: " + String(SEGUNDOS_RIEGO));
  }
  if (server.hasArg("DIAS_MAX")) {
    DIAS_MAX = server.arg("DIAS_MAX").toInt();
    EEPROM.put(80, DIAS_MAX);
    EEPROM.commit();
    Serial.println("Nuevo parametro DIAS_MAX: " + String(DIAS_MAX));
  }
  if (server.hasArg("HORAS_ENTRE_RIEGO_MIN")) {
    HORAS_ENTRE_RIEGO_MIN = server.arg("HORAS_ENTRE_RIEGO_MIN").toInt();
    EEPROM.put(90, HORAS_ENTRE_RIEGO_MIN);
    EEPROM.commit();
    Serial.println("Nuevo parametro HORAS_ENTRE_RIEGO_MIN: " + String(HORAS_ENTRE_RIEGO_MIN));
  }

  if (server.hasArg("accion") && server.arg("accion") == "regar")
    regar();

  if (server.hasArg("accion") && server.arg("accion") == "compruebaHumedad")
    compruebaHumedad();

  String ultimosRiegos = getUltimosRiegos();
  String ultimasHumedades = getUltimasHumedades();
  char str[15] = "";
  timeToString(str, sizeof(str));
  String strRoot = "<!DOCTYPE HTML><html>\
  <body>\
  <h1>Programa de riego</h1>\
  Cada MINUTOS_MUESTRA, miramos la humedad. Tomamos NUM_MUESTRAS y hacemos la media para mayor precision. Si es inferior a HUMEDAD_MIN, regamos durante SEGUNDOS_RIEGO. Si han pasado DIAS_MAX desde el ultimo riego, regamos igualmente.\
  Nos aseguramos que entre riego y riego pasan un minimo de HORAS_ENTRE_RIEGO_MIN.<br><br>\
  Tiempo desde el ultimo encendido: " + String(str) + "<br>";

  strRoot += "              <form>\
                   <label for = \"fname\">Fecha y hora:</label>\
    <input type=\"date\" id=\"dia\" name=\"dia\" value=\"" + String(year()) + "-" + transformaAdosDigitos(month()) + "-" + transformaAdosDigitos(day()) + "\">\
    <input type=\"time\" id=\"hora\" name=\"hora\" value=\"" + transformaAdosDigitos(hour()) + ":" + transformaAdosDigitos(minute()) + "\">\
    <input type=\"submit\" value=\"Envia\"><br>\
  </form> \
  \
  <form>\
    <label for=\"fname\">MINUTOS_MUESTRA:</label>\
    <input type=\"number\" style=\"width: 3em;\" min=\"1\" max=\"59\" id=\"MINUTOS_MUESTRA\" name=\"MINUTOS_MUESTRA\" value=\"" +  MINUTOS_MUESTRA + "\">\
    <input type=\"submit\" value=\"Envia\"><br>\
  </form> \
  <form>\
    <label for=\"fname\">NUM_MUESTRAS:</label>\
    <input type=\"number\" style=\"width: 3em;\" min=\"1\" max=\"100\" id=\"NUM_MUESTRAS\" name=\"NUM_MUESTRAS\" value=\"" + NUM_MUESTRAS + "\">\
    <input type=\"submit\" value=\"Envia\"><br>\
  </form>\
  <form>\
    <label for=\"fname\">HUMEDAD_MIN:</label>\
    <input type=\"number\" style=\"width: 4em;\" min=\"1\" max=\"1000\" id=\"HUMEDAD_MIN\" name=\"HUMEDAD_MIN\" value=\"" + HUMEDAD_MIN + "\">\
    <input type=\"submit\" value=\"Envia\"><br>\
  </form>\
  <form>\
    <label for=\"fname\">SEGUNDOS_RIEGO:</label>\
    <input type=\"number\" style=\"width: 3em;\" min=\"1\" max=\"200\" id=\"SEGUNDOS_RIEGO\" name=\"SEGUNDOS_RIEGO\" value=\"" + SEGUNDOS_RIEGO + "\">\
    <input type=\"submit\" value=\"Envia\"><br>\
  </form>\
  <form>\
    <label for=\"fname\">DIAS_MAX:</label>\
    <input type=\"number\" style=\"width: 3em;\" min=\"1\" max=\"30\" id=\"DIAS_MAX\" name=\"DIAS_MAX\" value=\"" + DIAS_MAX + "\">\
    <input type=\"submit\" value=\"Envia\"><br>\
  </form>\
  <form>\
    <label for=\"fname\">HORAS_ENTRE_RIEGO_MIN:</label>\
    <input type=\"number\" style=\"width: 3em;\" min=\"1\" max=\"300\" id=\"HORAS_ENTRE_RIEGO_MIN\" name=\"HORAS_ENTRE_RIEGO_MIN\" value=\"" + HORAS_ENTRE_RIEGO_MIN + "\">\
    <input type=\"submit\" value=\"Envia\"><br>\
  </form>\
  Ultimas mediciones humedad: " + ultimasHumedades + "<br>\
  Ultimos riegos: " + ultimosRiegos + "<br>\
  <form>\
    <button name=\"accion\" type=\"submit\" value=\"regar\">Regar ahora</button><br>\
  </form> \
  <form>\
    <button name=\"accion\" type=\"submit\" value=\"compruebaHumedad\">Comprobar humedad</button><br>\
  </form> \
  </body>\
  </html>";

  server.send(200, "text/html", strRoot);

}


//transforma un entero a string. Si tiene solo 1 posición, añade un 0 delante
String transformaAdosDigitos(int i)
{
  if (i < 10)
    return "0" + String(i);
  return String(i);
}

//llamada necesaria para la librería de tiempo
time_t requestSync()
{
  return 0; // the time will be sent later in response to serial mesg
}


//divide strings y retorna un substring
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

//muestra la hora
void digitalClockDisplay() {
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year());
  Serial.println();
}

//utilidad para mostrar la hora
void printDigits(int digits) {
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

//cuando se pulsa el boton FLASH, reactivamos el wifi
void onPressed() {
  Serial.println("Button has been pressed!");
  initWifi();
}

//guarda los últimos 10 registros de la humedad en la EEPROM
void guardaEepromHumedad(int valor) {
  EEPROM.put(200 + (contador_muestras_humedad * 10), valor);
  EEPROM.commit();
  if (contador_muestras_humedad < 9)
    contador_muestras_humedad++;
  else
    contador_muestras_humedad = 0;

  EEPROM.put(110, contador_muestras_humedad);
  EEPROM.commit();
}

//guarda las fechas de los últimos 10 riegos en la EEPROM
void guardaEepromRiego(time_t valor) {

  EEPROM.put(300 + (contador_riegos * 10), valor);
  EEPROM.commit();
  if (contador_riegos < 9)
    contador_riegos++;
  else
    contador_riegos = 0;
  EEPROM.put(120, contador_riegos);
  EEPROM.commit();
}

//devuelve un string con los últimos 10 riegos
String getUltimosRiegos()
{
  time_t ahora = now();
  String strFinal = "";

  int contador_tmp = contador_riegos - 1;
  if (contador_tmp < 0)
    contador_tmp = 9;
  for (int i = 0; i < 10; i++)
  {
    time_t tmp;
    EEPROM.get(300 + (contador_tmp * 10), tmp);
    int mins = (int)((ahora - tmp) / 60.0);
    if (mins > 0)
      strFinal = "Hace " + String(mins) + " minutos; " + strFinal;
    contador_tmp--;

    if (contador_tmp < 0)
      contador_tmp = 9;

  }
  if (strFinal == "")
    strFinal = "No hay registro de los ultimos riegos en memoria. La hora es incorrecta o el dispositivo se ha reiniciado recientemente";

  return strFinal;

}

//devuelve un string con los últimas 10 humedades registradas
String getUltimasHumedades()
{
  String strFinal = "";

  int contador_tmp = contador_muestras_humedad - 1;
  if (contador_tmp < 0)
    contador_tmp = 9;
  for (int i = 0; i < 10; i++)
  {
    int tmp;
    EEPROM.get(200 + (contador_tmp * 10), tmp);
    if (tmp > 9)
      strFinal = String(tmp) + "; " + strFinal;
    contador_tmp--;

    if (contador_tmp < 0)
      contador_tmp = 9;

  }
  if (strFinal == "")
    strFinal = "No hay lecturas de humedades en memoria";
  return strFinal;
}

//si es la primera vez que conectamos la placa, se inicializa la eeprom de riegos y humedades
void inicializarEeprom()
{
  Serial.println("Inicializando Eeprom, tardaremos 10 segundos...");

  for (int i = 0; i < 10; i++)
  {
    EEPROM.put(200 + (i * 10), i);
    EEPROM.commit();

  }

  for (int i = 0; i < 10; i++)
  {
    delay(1000);
    time_t tmp = now();
    EEPROM.put(300 + (i * 10), tmp);
    EEPROM.commit();


  }
}

//utilidad de hora a string
void timeToString(char* string, size_t size)
{
  unsigned long nowMillis = millis();
  unsigned long seconds = nowMillis / 1000;
  int days = seconds / 86400;
  seconds %= 86400;
  byte hours = seconds / 3600;
  seconds %= 3600;
  byte minutes = seconds / 60;
  seconds %= 60;
  snprintf(string, size, "%04d:%02d:%02d:%02d", days, hours, minutes, seconds);
}


//comrpueba la humedad y la guarda en la eeprom
int compruebaHumedad()
{
  int humedad=0;
  //activamos el sensor de humedad
  digitalWrite(HUM, HIGH);
  delay(100);//wait 10 milliseconds
  Serial.println("Leyendo humedad. Muestra: ");

  //tomamos X muestras de humedad y hacemos la media
  for (int i = 0; i < NUM_MUESTRAS; i++)
  {
    int ahoraHumedad = analogRead(AnalogIn);
    Serial.print(i);
    Serial.print(". Valor: ");
    Serial.println(ahoraHumedad);

    humedad = (int)humedad + (int)ahoraHumedad;
    delay(100);


  }
  //apagamos el sensor de humedad
  digitalWrite(HUM, LOW);

  humedad = humedad / NUM_MUESTRAS;
  Serial.print("Media humedad: ");
  Serial.println(humedad);
  ultimaLecturaHumedad = millis();
  //guardamos un registro de las 10 ultimas lectuas de humedad
  guardaEepromHumedad(humedad);
  return humedad;
}
// end of code.
