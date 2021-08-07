#include <shTaskManager.h>
#include <shButton.h>
#include "autoLightManager.h"
#include <avr/sleep.h>
#include <FastLED.h>
#include <TM1637Display.h>

TM1637Display tm(11, 10); // CLK, DAT

CRGB leds[3]; // массив адресных светодиодов-индикаторов режима автосвета

shTaskManager tasks(5); // создаем список задач

shHandle sleep_on_timer;     // таймер ухода в сон через 10 минут после отключения зажигания
shHandle data_guard;         // отслеживание изменения уровня на входных пинах
shHandle leds_guard;         // управление светодиодами-индикаторами автосвета
shHandle light_sensor_guard; // отслеживание показаний датчика света
shHandle low_beam_off_timer; // таймер отключения ближнего света при превышении порога датчика света

bool engine_run_flag = false;            // флаг запуска двигателя
byte auto_light_mode = AUTOLIGHT_MODE_0; // текущий режим автосвета
uint16_t light_sensor_threshold = 200;   // текущие показания датчика света

// кнопки управления автосветом
shButton btnAlm1(BTN_ALM1_PIN);
shButton btnAlm2(BTN_ALM2_PIN);
shButton btnAlm3(BTN_ALM3_PIN);

// адреса ячеек памяти для хранения настроек автосвета
uint8_t EEMEM e_sleep_on;      // время ухода в сон после отключения зажигания, мин
uint16_t EEMEM e_al_threshold; // порог включения ближнего света, 0-1023
uint8_t EEMEM e_al_mode;       // режим автосвета
// ===================================================

void allTick()
{
  tasks.tick();
  checkBtnAlm();
}

// ===================================================

void checkIgnition()
{
  tasks.stopTask(sleep_on_timer);
  // запуск всего остановленного
}

void checkInputData()
{
  if (!digitalRead(INGNITION_PIN))
  {
    if (!tasks.getTaskState(sleep_on_timer))
    {
      engine_run_flag = false;
      setLightRelay();
      tasks.startTask(sleep_on_timer);
    }
  }
  else
  {
    if (!tasks.getTaskState(sleep_on_timer))
    {
      tasks.stopTask(sleep_on_timer);
    }
    if (!engine_run_flag && digitalRead(ENGINE_RUN_PIN))
    {
      engine_run_flag = true;
      runLightMode();
    }
  }
}

void powerOffTimer()
{
  tasks.stopTask(sleep_on_timer);
  // отключить индикаторы

  // и перевести МК в спящий режим
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_mode();
}

// ===================================================

void lightSensorRead()
{
  light_sensor_threshold = (light_sensor_threshold * 2 + analogRead(LIGHT_SENSOR_PIN)) / 3;

  // тут же управление светом при работе от датчика света
  if (auto_light_mode == AUTOLIGHT_MODE_3 && engine_run_flag)
  {
    if (light_sensor_threshold < eeprom_read_word(&e_al_threshold))
    { // если порог снизился до уровня включения БС, то включить БС и остановить таймер отключения БС
      setLightRelay(2);
      tasks.stopTask(low_beam_off_timer);
    }
    else
    { // если наоборот и таймер отключения БС еще не запущен, запустить его
      if (!tasks.getTaskState(low_beam_off_timer))
      {
        tasks.startTask(low_beam_off_timer);
      }
    }
  }
  // отладочный вывод показаний датчика
  tm.showNumberDec(light_sensor_threshold);
  // и здесь же управление яркостью дисплея и индикаторов
  uint16_t t = eeprom_read_word(&e_al_threshold);
  if (light_sensor_threshold <= t)
  {
    FastLED.setBrightness(50);
    tm.setBrightness(2);
  }
  else if (light_sensor_threshold > t + 50)
  {
    FastLED.setBrightness(255);
    tm.setBrightness(7);
  }
  FastLED.show();
}

void runLightMode()
{
  byte x = (auto_light_mode != AUTOLIGHT_MODE_3) ? auto_light_mode : 1;
  if (light_sensor_threshold <= eeprom_read_word(&e_al_threshold))
  {
    x = 2;
  }
  setLightRelay(x);
}

void setLightRelay(byte rel = 0)
{
  digitalWrite(RELAY_1_PIN, (rel == 1) && engine_run_flag);
  digitalWrite(RELAY_2_PIN, (rel == 2) && engine_run_flag);
}

void setAutoLightMode(byte mode_btn)
{
  // если текущий режим равен нажатой кнопке, то установить нулевой режим (включить ручной), иначе установить режим, равный нажатой кнопке
  auto_light_mode = (mode_btn == auto_light_mode) ? AUTOLIGHT_MODE_0 : mode_btn;
  // сохранить новый режим
  eeprom_update_byte(&e_al_mode, auto_light_mode);
  // переключить свет; 
  runLightMode();
}

void checkBtnAlm()
{
  if (btnAlm1.getButtonState() == BTN_DOWN)
  {
    setAutoLightMode(AUTOLIGHT_MODE_1);
  }
  else if (btnAlm2.getButtonState() == BTN_DOWN)
  {
    setAutoLightMode(AUTOLIGHT_MODE_2);
  }
  else
  {
    switch (btnAlm3.getButtonState())
    {
    case BTN_ONECLICK:
      setAutoLightMode(AUTOLIGHT_MODE_3);
      break;
    case BTN_LONGCLICK:
      // здесь настройка порога срабатывания
      break;
    }
  }
}

void setLeds()
{
  if (tasks.getTaskState(sleep_on_timer))
  { // если запущен таймер ухода в сон, индикаторы выключить
    for (byte i = 0; i < 3; i++)
    {
      leds[i] = CRGB::Black;
    }
  }
  else
  { // иначе определить цвета: красный - режим выключен, зеленый - режим включен, свет не горит (двигатель не заведен), голубой - режим включен, горит БС, желтый - горят противотуманки
    for (byte i = 0; i < 3; i++)
    {
      leds[i] = CRGB::Red;
    }
    switch (auto_light_mode)
    {
    case AUTOLIGHT_MODE_1:
      leds[0] = (engine_run_flag) ? CRGB::Yellow : CRGB::Green;
      break;
    case AUTOLIGHT_MODE_2:
      leds[1] = (engine_run_flag) ? CRGB::Blue : CRGB::Green;
      break;
    case AUTOLIGHT_MODE_3:
      if (!engine_run_flag)
      {
        leds[2] = CRGB::Green;
      }
      else
      {
        if (digitalRead(RELAY_1_PIN))
        {
          leds[2] = CRGB::Yellow;
        }
        else if (digitalRead(RELAY_2_PIN))
        {
          leds[2] = CRGB::Blue;
        }
      }
      break;
    }
  }
  FastLED.show();
}

void lowBeamOff()
{
  if (auto_light_mode == AUTOLIGHT_MODE_3 && !light_sensor_threshold < eeprom_read_word(&e_al_threshold))
  {
    setLightRelay(1);
  }
  tasks.stopTask(low_beam_off_timer);
}

// ===================================================

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);

  FastLED.addLeds<WS2812B, LEDS_DATA_PIN, GRB>(leds, 3);

  pinMode(INGNITION_PIN, INPUT);
  pinMode(ENGINE_RUN_PIN, INPUT);
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);

  // настройки автосвета
  auto_light_mode = eeprom_read_byte(&e_al_mode);
  if (auto_light_mode > AUTOLIGHT_MODE_3)
  {
    auto_light_mode = AUTOLIGHT_MODE_0;
    eeprom_update_byte(&e_al_mode, auto_light_mode);
  }
  if (eeprom_read_word(&e_al_threshold) > 1023)
  {
    eeprom_update_word(&e_al_threshold, 700);
  }

  // настройка кнопок
  btnAlm3.setVirtualClickOn(true);
  btnAlm3.setLongClickMode(LCM_ONLYONCE);
  btnAlm3.setTimeout(1000);
  // даблклик в управлении светом не используется, поэтому уменьшаем его время, чтобы ускорить выдачу события BTN_ONECLICK
  btnAlm3.setDblClickTimeout(100);

  // прерывания
  attachInterrupt(0, checkIgnition, RISING);

  // подключение задач
  byte t = eeprom_read_byte(&e_sleep_on);
  if (t > 60)
  {
    t = 10;
  }
  sleep_on_timer = tasks.addTask(t * 60000ul, powerOffTimer, false);
  data_guard = tasks.addTask(200, checkInputData);
  leds_guard = tasks.addTask(100, setLeds);
  light_sensor_guard = tasks.addTask(50, lightSensorRead);
  low_beam_off_timer = tasks.addTask(30000, lowBeamOff, false);
}

void loop()
{
  allTick();
}