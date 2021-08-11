#include <shTaskManager.h>
#include <shButton.h>
#include "autoLightManager.h"
#include <avr/sleep.h>
#include <FastLED.h>
#include <TM1637Display.h>
#include <Wire.h>   // Подключаем библиотеку для работы с I2C устройствами
#include <DS3231.h> // Подключаем библиотеку для работы с RTC DS3231

TM1637Display tm(11, 10); // CLK, DAT
DS3231 clock;             // SDA - A4, SCL - A5
RTClib RTC;

CRGB leds[3]; // массив адресных светодиодов-индикаторов режима автосвета

shTaskManager tasks(7); // создаем список задач

shHandle sleep_on_timer;         // таймер ухода в сон через 10 минут после отключения зажигания
shHandle data_guard;             // отслеживание изменения уровня на входных пинах
shHandle leds_guard;             // управление светодиодами-индикаторами автосвета
shHandle light_sensor_guard;     // отслеживание показаний датчика света
shHandle low_beam_off_timer;     // таймер отключения ближнего света при превышении порога датчика света
shHandle return_to_default_mode; // таймер автовозврата в режим показа времени из любого режима настройки
shHandle blink_timer;            // таймер блинка

bool engine_run_flag = false;              // флаг запуска двигателя
byte auto_light_mode = AUTOLIGHT_MODE_0;   // текущий режим автосвета
uint16_t light_sensor_threshold = 200;     // текущие показания датчика света
byte displayMode = DISPLAY_MODE_SHOW_TIME; // текущий режим работы дисплея
bool blink_flag = true;                    // флаг блинка

// ==== класс кнопок с предварительной настройкой ====

class almButton : public shButton
{
public:
  almButton(byte button_pin) : shButton(button_pin)
  {
    shButton::setTimeout(1000);
    shButton::setLongClickMode(LCM_ONLYONCE);
    shButton::setVirtualClickOn(true);
    shButton::setDblClickTimeout(100); // т.к. двойной клик нигде не используется, уменьшаем его интервал, чтобы ускорить выдачу события BTN_ONECLICK
    shButton::setDebounce(60);
  }

  byte getButtonState()
  {
    byte _state = shButton::getButtonState();
    switch (_state)
    {
    case BTN_DOWN:
    case BTN_DBLCLICK:
    case BTN_LONGCLICK:
      // если запущен какой-то диалог настройки, то каждый клик любой кнопки перезапускает таймер автовыхода в стандартный режим
      if (tasks.getTaskState(return_to_default_mode))
      {
        tasks.restartTask(return_to_default_mode);
      }
      break;
    }
    return (_state);
  }
};

// ===================================================

// кнопки управления автосветом
almButton btnAlm1(BTN_ALM1_PIN);
almButton btnAlm2(BTN_ALM2_PIN);
almButton btnAlm3(BTN_ALM3_PIN);
// кнопки управления часами
almButton btnClockSet(BTN_CLOCK_SET_PIN);
almButton btnClockUp(BTN_CLOCK_UP_PIN);

// адреса ячеек памяти для хранения настроек автосвета
uint8_t EEMEM e_sleep_on;      // время ухода в сон после отключения зажигания, мин
uint16_t EEMEM e_al_threshold; // порог включения ближнего света, 0-1023
uint8_t EEMEM e_al_mode;       // режим автосвета
// ===================================================

void allTick()
{
  tasks.tick();
  checkBtnAlm();
  checkClockBtn();
}

void returnToDefModeDisplay()
{
  displayMode = DISPLAY_MODE_SHOW_TIME;
  tasks.stopTask(return_to_default_mode);
}

// ===================================================

void checkIgnition()
{
  tasks.stopTask(sleep_on_timer);
  // запуск всего остановленного
  displayMode = DISPLAY_MODE_SHOW_TIME;
}

void checkInputData()
{
  if (!digitalRead(IGNITION_PIN))
  {
    if (!tasks.getTaskState(sleep_on_timer))
    {
      engine_run_flag = false;
      setLightRelay();
      tasks.startTask(sleep_on_timer);
      displayMode = DISPLAY_MODE_SHOW_TIME;
    }
  }
  else
  {
    if (tasks.getTaskState(sleep_on_timer))
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
  tasks.stopTask(low_beam_off_timer);

  // отключить дисплей
  tm.clear();

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
}

void runLightMode()
{
  byte x = (auto_light_mode != AUTOLIGHT_MODE_3) ? auto_light_mode : 1;
  if (auto_light_mode == AUTOLIGHT_MODE_3 && light_sensor_threshold <= eeprom_read_word(&e_al_threshold))
  {
    x = 2;
  }
  setLightRelay(x);
}

void setLightRelay(byte rel = 0)
{
  digitalWrite(RELAY_1_PIN, (auto_light_mode && (rel == 1) && engine_run_flag));
  digitalWrite(RELAY_2_PIN, (auto_light_mode && (rel == 2) && engine_run_flag));
}

void setAutoLightMode(byte mode_btn)
{
  // режимы менять только при включенном зажигании и не в режиме настройки порога срабатывания датчика света
  if (digitalRead(IGNITION_PIN) && displayMode != DISPLAY_MODE_SET_LIGHT_THRESHOLD)
  { // если текущий режим равен нажатой кнопке, то установить нулевой режим (включить ручной), иначе установить режим, равный нажатой кнопке
    auto_light_mode = (mode_btn == auto_light_mode) ? AUTOLIGHT_MODE_0 : mode_btn;
    // сохранить новый режим
    eeprom_update_byte(&e_al_mode, auto_light_mode);
    // переключить свет;
    runLightMode();
  }
}

void setLeds()
{
  if (!digitalRead(IGNITION_PIN))
  { // если отключено зажигание, индикаторы выключить
    for (byte i = 0; i < 3; i++)
    {
      leds[i] = CRGB::Black;
    }
  }
  else
  { // если включен режим настройки порога срабатывания датчика света, все иникаторы вывести голубым
    if (displayMode == DISPLAY_MODE_SET_LIGHT_THRESHOLD)
    {
      for (byte i = 0; i < 3; i++)
      {
        leds[i] = CRGB::Blue;
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
        leds[0] = (engine_run_flag) ? CRGB::Orange : CRGB::Green;
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
            leds[2] = CRGB::Orange;
          }
          else if (digitalRead(RELAY_2_PIN))
          {
            leds[2] = CRGB::Blue;
          }
        }
        break;
      }
    }
  }
  FastLED.show();
}

void lowBeamOff()
{
  if (auto_light_mode == AUTOLIGHT_MODE_3 && light_sensor_threshold > eeprom_read_word(&e_al_threshold))
  {
    setLightRelay(1);
  }
  tasks.stopTask(low_beam_off_timer);
}

void showLightThresholdSetting()
{
  tm.clear();
  uint16_t data = eeprom_read_word(&e_al_threshold) / 10;
  tasks.startTask(return_to_default_mode);
  bool flag = false;
  while (displayMode != DISPLAY_MODE_SHOW_TIME)
  {
    tasks.tick();
    checkBtnAlm(); // это чтобы не зависали статусы кнопок, режимы здесь все равно переключаться не будут

    // обработка кнопки Up
    if (checkBtnUp(data, 10, 90))
    {
      flag = true;
    }
    // обработка кнопки Set
    switch (btnClockSet.getButtonState())
    {
    case BTN_ONECLICK: // при одиночном клике на кнопку вывести текущие данные с датчика света
      data = light_sensor_threshold / 10;
      break;
    case BTN_LONGCLICK: // при длинном клике на кнопку выйти из настроек
      displayMode = DISPLAY_MODE_SHOW_TIME;
      break;
    }
    tm.showNumberDec(data);
  }
  if (flag)
  {
    eeprom_update_word(&e_al_threshold, data * 10);
  }
  tm.clear();
  tasks.stopTask(return_to_default_mode);
}

// ===================================================

void checkBtnAlm()
{
  if (btnAlm1.getButtonState() == BTN_ONECLICK)
  {
    setAutoLightMode(AUTOLIGHT_MODE_1);
  }
  if (btnAlm2.getButtonState() == BTN_ONECLICK)
  {
    setAutoLightMode(AUTOLIGHT_MODE_2);
  }
  switch (btnAlm3.getButtonState())
  {
  case BTN_ONECLICK:
    setAutoLightMode(AUTOLIGHT_MODE_3);
    break;
  case BTN_LONGCLICK:
    // здесь запуск настройки порога срабатывания датчика света
    if (digitalRead(IGNITION_PIN))
    {
      displayMode = DISPLAY_MODE_SET_LIGHT_THRESHOLD;
    }
    break;
  }
}

void checkClockBtn()
{
  switch (btnClockSet.getButtonState())
  {
  case BTN_LONGCLICK:
    displayMode = DISPLAY_MODE_SET_HOUR;
    break;
  }
  switch (btnClockUp.getButtonState())
  {
  case BTN_LONGCLICK:
    /* code */
    break;
  }
}

bool checkBtnUp(uint16_t &_data, int min_data, int max_data)
{
  bool result = false;
  switch (btnClockUp.getButtonState())
  {
  case BTN_DOWN:
  case BTN_DBLCLICK:
  case BTN_LONGCLICK:
    result = true;
    if (++_data > max_data)
    {
      _data = min_data;
    }
    break;
  }
  return (result);
}

// ===================================================

void blinkTimer()
{
  blink_flag = !blink_flag;
}

void restartBlinkTimer()
{
  blink_flag = true;
  tasks.restartTask(blink_timer);
}

void showTime(DateTime dt, bool force = false)
{
  showTime(dt.hour(), dt.minute(), force);
}

void showTime(byte hour, byte minute, bool force = false)
{
  static bool p = false;
  // вывод делается только в момент смены состояния блинка, т.е. через каждые 500 милисекунд или по флагу принудительного обновления
  if (force || p != blink_flag)
  {
    if (!force)
    {
      p = blink_flag;
    }
    uint16_t h = hour * 100 + minute;
    uint8_t s = (p) ? 0b01000000 : 0;
    tm.showNumberDecEx(h, s, true);
  }
}

void saveTime(byte hour, byte minute)
{
  clock.setHour(hour);
  clock.setMinute(minute);
  clock.setSecond(0);
}

void showTimeSetting()
{
  tasks.startTask(return_to_default_mode);
  restartBlinkTimer();
  bool flag = false;
  uint16_t curHour = 0;
  uint16_t curMinute = 0;
  while (displayMode > DISPLAY_MODE_SHOW_TIME && displayMode < DISPLAY_MODE_SET_TIMEOUT)
  {
    tasks.tick();
    checkBtnAlm();
    if (!flag)
    {
      DateTime dt = RTC.now();
      curHour = dt.hour();
      curMinute = dt.minute();
    }
    // опрос конопок
    switch (btnClockSet.getButtonState()) // кнопка Set
    {
    case BTN_ONECLICK:
      displayMode++;
      if (displayMode == DISPLAY_MODE_SET_TIMEOUT)
        displayMode = DISPLAY_MODE_SHOW_TIME;
      break;
    case BTN_LONGCLICK:
      displayMode = DISPLAY_MODE_SHOW_TIME;
      break;
    }
    switch (displayMode) // кнопка Up
    {
    case DISPLAY_MODE_SET_HOUR:
      if (checkBtnUp(curHour, 0, 23))
      {
        flag = true;
      }
      break;
    case DISPLAY_MODE_SET_MINUTE:
      if (checkBtnUp(curMinute, 0, 59))
      {
        flag = true;
      }
      break;
    }
    // вывод данных на индикатор
    showTimeSettingData(curHour, curMinute);
  }
  if (flag)
  {
    saveTime(curHour, curMinute);
  }
  tasks.stopTask(return_to_default_mode);
  if (displayMode == DISPLAY_MODE_SHOW_TIME)
  {
    restartBlinkTimer();
  }
}

void showTimeSettingData(byte hour, byte minute)
{
  uint8_t data[] = {0xff, 0xff, 0xff, 0xff};
  data[0] = tm.encodeDigit(hour / 10);
  data[1] = tm.encodeDigit(hour % 10);
  data[2] = tm.encodeDigit(minute / 10);
  data[3] = tm.encodeDigit(minute % 10);
  // если наступило время блинка и кнопка Up не нажата, то стереть соответствующие разряды; при нажатой кнопке Up во время изменения данных ничего не мигает
  if (!blink_flag && !btnClockUp.isButtonClosed())
  {
    byte i = (displayMode == DISPLAY_MODE_SET_HOUR) ? 0 : 2;
    data[i] = 0x00;
    data[i + 1] = 0x00;
  }
  tm.setSegments(data);
}

// ===================================================

void setup()
{
  FastLED.addLeds<WS2812B, LEDS_DATA_PIN, GRB>(leds, 3);

  pinMode(IGNITION_PIN, INPUT);
  pinMode(ENGINE_RUN_PIN, INPUT);
  pinMode(RELAY_1_PIN, OUTPUT);
  pinMode(RELAY_2_PIN, OUTPUT);

  // настройки автосвета ========================
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

  // прерывания =================================
  attachInterrupt(0, checkIgnition, RISING);

  // подключение задач ==========================
  byte t = eeprom_read_byte(&e_sleep_on);
  if (t > 60)
  {
    t = 10;
  }
  sleep_on_timer = tasks.addTask(t * 60000ul, powerOffTimer, false);
  data_guard = tasks.addTask(200, checkInputData);
  light_sensor_guard = tasks.addTask(100, lightSensorRead);
  leds_guard = tasks.addTask(100, setLeds);
  low_beam_off_timer = tasks.addTask(30000, lowBeamOff, false);
  return_to_default_mode = tasks.addTask(10000, returnToDefModeDisplay, false);
  blink_timer = tasks.addTask(500, blinkTimer);

  // кнопки =====================================
  btnClockUp.setLongClickMode(LCM_CLICKSERIES);
  btnClockUp.setLongClickTimeout(100);

  // Часы =======================================
  Wire.begin();
  clock.setClockMode(false); // 24-часовой режим
}

void loop()
{
  allTick();

  switch (displayMode)
  {
  case DISPLAY_MODE_SHOW_TIME:
    showTime(RTC.now());
    break;
  case DISPLAY_MODE_SET_HOUR:
  case DISPLAY_MODE_SET_MINUTE:
    showTimeSetting();
    break;
  case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
    showLightThresholdSetting();
    break;
  }
}