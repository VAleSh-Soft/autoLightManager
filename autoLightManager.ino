#include <Wire.h>
#include <FastLED.h>       // https://github.com/FastLED/FastLED
#include <DS3231.h>        // https://github.com/NorthernWidget/DS3231
#include <shTaskManager.h> // https://github.com/VAleSh-Soft/shTaskManager
#include "autoLightManager.h"
#include "display_TM1637.h"
#include <avr/sleep.h>

#define USE_BUTTON_FLAG
#include <shButton.h>      // https://github.com/VAleSh-Soft/shButton

DisplayTM1637 disp(DISPLAY_CLK_PIN, DISPLAY_DAT_PIN);
DS3231 clock; // SDA - A4, SCL - A5
RTClib RTC;

CRGB leds[3]; // массив адресных светодиодов-индикаторов режима автосвета

shTaskManager tasks(11); // создаем список задач

shHandle sleep_on_timer;          // таймер ухода в сон через 10 минут после отключения зажигания
shHandle blink_timer;             // таймер блинка
shHandle low_beam_off_timer;      // таймер отключения ближнего света при превышении порога датчика света
shHandle data_guard;              // отслеживание изменения уровня на входных пинах
shHandle display_guard;           // вывод данных на экран
shHandle light_sensor_guard;      // отслеживание показаний датчика света
shHandle leds_guard;              // управление светодиодами-индикаторами автосвета
shHandle return_to_default_mode;  // таймер автовозврата в режим показа времени из любого режима настройки
shHandle show_set_time_mode;      // режим настройки времени
shHandle show_temp_mode;          // режим показа температуры
shHandle show_other_setting_mode; // режим настройки времени перехода в спящий режим

bool engine_run_flag = false;              // флаг запуска двигателя
byte auto_light_mode = AUTOLIGHT_MODE_0;   // текущий режим автосвета
uint16_t light_sensor_threshold = 200;     // текущие показания датчика света
byte displayMode = DISPLAY_MODE_SHOW_TIME; // текущий режим работы экрана
bool blink_flag = false;                   // флаг блинка, используется всем, что должно мигать
byte color_1;                              // индекс цвета индикатора работы ПТФ
byte color_2;                              // индекс цвета индикатора работы ближнего света

// ==== класс кнопок с предварительной настройкой ====
const uint8_t BTN_FLAG_NONE = 0; // флаг кнопки - ничего не делать
const uint8_t BTN_FLAG_NEXT = 1; // флаг кнопки - изменить значение
const uint8_t BTN_FLAG_EXIT = 2; // флаг кнопки - возврат в режим показа текущего времени

class almButton : public shButton
{
private:
public:
  almButton(byte button_pin) : shButton(button_pin)
  {
    shButton::setTimeoutOfLongClick(1000);
    shButton::setLongClickMode(LCM_ONLYONCE);
    shButton::setVirtualClickOn(true);
    shButton::setTimeoutOfDblClick(100); // т.к. двойной клик нигде не используется, уменьшаем его интервал, чтобы ускорить выдачу события BTN_ONECLICK
    shButton::setTimeoutOfDebounce(60);
  }

  byte getButtonState()
  {
    byte _state = shButton::getButtonState();
    switch (_state)
    {
    case BTN_DOWN:
    case BTN_DBLCLICK:
    case BTN_LONGCLICK:
      // в любом режиме, кроме стандартного, каждый клик любой кнопки перезапускает таймер автовыхода в стандартный режим
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
almButton btnMode1(BTN_MODE1_PIN);
almButton btnMode2(BTN_MODE2_PIN);
almButton btnMode3(BTN_MODE3_PIN);
// кнопки управления часами
almButton btnClockSet(BTN_CLOCK_SET_PIN);
almButton btnClockUp(BTN_CLOCK_UP_PIN);

// адреса ячеек памяти для хранения настроек модуля
uint8_t EEMEM e_show_temp_to_run; // показ температуры при выходе из спящего режима
uint8_t EEMEM e_turn_on_delay;    // задержка включения света при старте двигателя
uint8_t EEMEM e_color_1;          // цвет индикации работы ПТФ (индекс в списке доступных цветов)
uint8_t EEMEM e_color_2;          // цвет индикации работы ближнего света (индекс в списке доступных цветов)
uint8_t EEMEM e_sleep_on;         // время ухода в сон после отключения зажигания, мин
uint16_t EEMEM e_al_threshold;    // порог включения ближнего света, 0-1023
uint8_t EEMEM e_al_mode;          // режим автосвета

// ===================================================
void checkButton()
{
  checkSetButton();
  checkUpButton();
  checkModeButton();
}

void checkSetButton()
{
  switch (btnClockSet.getButtonState())
  {
  case BTN_ONECLICK:
    switch (displayMode)
    {
    case DISPLAY_MODE_SET_HOUR:
    case DISPLAY_MODE_SET_MINUTE:
    case DISPLAY_MODE_SET_TIMEOUT:
    case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
    case DISPLAY_MODE_SET_COLOR_1:
      btnClockSet.setButtonFlag(BTN_FLAG_NEXT);
      break;
    case DISPLAY_MODE_SET_TURN_ON_DELAY:
    case DISPLAY_MODE_SET_COLOR_2:
    case DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN:
      btnClockSet.setButtonFlag(BTN_FLAG_EXIT);
      break;
    }
    break;
  case BTN_LONGCLICK:
    switch (displayMode)
    {
    case DISPLAY_MODE_SHOW_TIME:
      displayMode = DISPLAY_MODE_SET_HOUR;
      break;
    case DISPLAY_MODE_SHOW_TEMP:
      displayMode = DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN;
      tasks.stopTask(show_temp_mode);
      break;
    default:
      btnClockSet.setButtonFlag(BTN_FLAG_EXIT);
      break;
    }
    break;
  }
}

void checkUpButton()
{
  switch (displayMode)
  {
  case DISPLAY_MODE_SHOW_TIME:
    if (btnClockUp.getButtonState() == BTN_ONECLICK)
    {
      displayMode = DISPLAY_MODE_SHOW_TEMP;
    }
    break;
  case DISPLAY_MODE_SHOW_TEMP:
    if (btnClockUp.getButtonState() == BTN_ONECLICK)
    {
      returnToDefMode();
    }
    break;
  case DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN:
    // при настройке типа "вкл/выкл" реагировать только на короткий клик кнопки
    if (btnClockUp.getButtonState() == BTN_DOWN)
    {
      btnClockUp.setButtonFlag(BTN_FLAG_NEXT);
    }
    break;
  default:
    switch (btnClockUp.getButtonState())
    {
    case BTN_DOWN:
    case BTN_DBLCLICK:
    case BTN_LONGCLICK:
      btnClockUp.setButtonFlag(BTN_FLAG_NEXT);
      break;
    }
    break;
  }
}

void checkModeButton()
{
  if (btnMode1.getButtonState() == BTN_ONECLICK)
  {
    setAutoLightMode(AUTOLIGHT_MODE_1);
  }
  if (btnMode2.getButtonState() == BTN_ONECLICK)
  {
    setAutoLightMode(AUTOLIGHT_MODE_2);
  }
  switch (btnMode3.getButtonState())
  {
  case BTN_ONECLICK:
    switch (displayMode)
    {
    case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
      btnMode3.setButtonFlag(BTN_FLAG_NEXT);
      break;
    default:
      setAutoLightMode(AUTOLIGHT_MODE_3);
      break;
    }
    break;
  case BTN_LONGCLICK:
    // здесь запуск настройки порога срабатывания датчика света
    if (digitalRead(IGNITION_PIN) && displayMode == DISPLAY_MODE_SHOW_TIME)
    {
      displayMode = DISPLAY_MODE_SET_LIGHT_THRESHOLD;
    }
    break;
  }
}
// ===================================================
void powerOffTimer()
{
  tasks.stopTask(sleep_on_timer);
  tasks.stopTask(low_beam_off_timer);
  tasks.stopTask(data_guard);
  tasks.stopTask(display_guard);
  returnToDefMode();

  // отключить экран
  disp.sleep();

  // и перевести МК в спящий режим
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_mode();
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
      if (displayMode != DISPLAY_MODE_SET_LIGHT_THRESHOLD)
        returnToDefMode();
    }
  }
  else
  {
    if (tasks.getTaskState(sleep_on_timer))
    {
      tasks.stopTask(sleep_on_timer);
    }
    if (!engine_run_flag)
    {
      static byte n = 0;
      if (digitalRead(ENGINE_RUN_PIN))
      { // поднимать флаг запуска двигателя и, соответственно, включать свет только по истечении времени задержки;
        if (++n >= eeprom_read_byte(&e_turn_on_delay))
        {
          engine_run_flag = true;
          runLightMode();
        }
      }
      else
      {
        n = 0;
      }
    }
  }
}

CRGB _getColor(byte mode, byte index)
{
  // перечень доступных для настройки цветов индикаторов
  static const PROGMEM uint32_t colors[2][10] = {
      {/*CRGB::SaddleBrown*/ 0x8B4513,
       /*CRGB::DarkGoldenrod*/ 0xB8860B,
       /*CRGB::MediumOrchid*/ 0xBA55D3,
       /*CRGB::OrangeRed*/ 0xFF4500,
       /*CRGB::Yellow*/ 0xFFFF00,
       /*CRGB::Tomato*/ 0xFF6347,
       /*CRGB::DarkOrange*/ 0xFF8C00,
       /*CRGB::Orange*/ 0xFFA500,
       /*CRGB::LightSalmon*/ 0xFFA07A,
       /*CRGB::White*/ 0xFFFFFF},
      {/*CRGB::Blue*/ 0x0000FF,
       /*CRGB::Navy*/ 0x000080,
       /*CRGB::DarkBlue*/ 0x00008B,
       /*CRGB::MediumBlue*/ 0x0000CD,
       /*CRGB::Cyan*/ 0x00FFFF,
       /*CRGB::DarkCyan*/ 0x008B8B,
       /*CRGB::DeepSkyBlue*/ 0x00BFFF,
       /*CRGB::DodgerBlue*/ 0x1E90FF,
       /*CRGB::Aqua*/ 0x00FFFF,
       /*CRGB::Teal*/ 0x008080}};
  if (mode > 1)
  {
    mode = 1;
  }
  if (index > 9)
  {
    index = 0;
  }
  return (pgm_read_dword(&colors[mode][index]));
}

void setLeds()
{
  if (!digitalRead(IGNITION_PIN) && !((displayMode >= DISPLAY_MODE_SET_LIGHT_THRESHOLD) &&
                                      (displayMode <= DISPLAY_MODE_SET_COLOR_2)))
  { // если отключено зажигание (и не идет настройка уровня датчика света и цветов иникаторов), индикаторы выключить
    for (byte i = 0; i < 3; i++)
    {
      leds[i] = CRGB::Black;
    }
  }
  else
  { // если включен режим настройки порога срабатывания датчика света, все иникаторы вывести голубым, если режимы настройки цветов индикаторов - соответствующим цветом
    if ((displayMode >= DISPLAY_MODE_SET_LIGHT_THRESHOLD) &&
        (displayMode <= DISPLAY_MODE_SET_COLOR_2))
    {
      CRGB col;
      switch (displayMode)
      {
      case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
        col = CRGB::Blue;
        break;
      case DISPLAY_MODE_SET_COLOR_1:
        col = _getColor(0, color_1);
        break;
      case DISPLAY_MODE_SET_COLOR_2:
        col = _getColor(1, color_2);
        break;
      }
      for (byte i = 0; i < 3; i++)
      {
        leds[i] = col;
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
        leds[0] = (engine_run_flag) ? _getColor(0, color_1) : CRGB::Green;
        break;
      case AUTOLIGHT_MODE_2:
        leds[1] = (engine_run_flag) ? _getColor(1, color_2) : CRGB::Green;
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
            leds[2] = _getColor(0, color_1);
          }
          else if (digitalRead(RELAY_2_PIN))
          {
            leds[2] = _getColor(1, color_2);
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

void lightSensorRead()
{
  light_sensor_threshold = (light_sensor_threshold * 2 + analogRead(LIGHT_SENSOR_PIN)) / 3;
  uint16_t t = eeprom_read_word(&e_al_threshold);

  // тут же управление светом при работе от датчика света
  if (auto_light_mode == AUTOLIGHT_MODE_3 && engine_run_flag)
  {
    if (light_sensor_threshold <= t)
    { // если уровень снизился до порога включения БС, то включить БС и остановить таймер отключения БС
      setLightRelay(2);
      tasks.stopTask(low_beam_off_timer);
    }
    else if (light_sensor_threshold > t + 50)
    { // если уровень превысил порог включения БС, а таймер отключения БС еще не запущен, запустить его
      if (!tasks.getTaskState(low_beam_off_timer))
      {
        tasks.startTask(low_beam_off_timer);
      }
    }
  }

  // и здесь же управление яркостью экрана и индикаторов - вне зависимость от режима работы
  if (light_sensor_threshold <= t)
  {
    disp.setBrightness(2);
    FastLED.setBrightness(50);
  }
  else if (light_sensor_threshold > t + 50)
  {
    disp.setBrightness(7);
    FastLED.setBrightness(255);
  }
}

void blink()
{
  if (!tasks.getTaskState(blink_timer))
  {
    tasks.startTask(blink_timer);
    blink_flag = false;
  }
  else
  {
    blink_flag = !blink_flag;
  }
}

void returnToDefMode()
{
  switch (displayMode)
  {
  case DISPLAY_MODE_SHOW_TEMP:
    displayMode = DISPLAY_MODE_SHOW_TIME;
    break;
  case DISPLAY_MODE_SHOW_TIME:
    break;
  default:
    btnClockSet.setButtonFlag(BTN_FLAG_EXIT);
    break;
  }
  tasks.stopTask(show_temp_mode);
  tasks.stopTask(return_to_default_mode);
}

void showTimeSetting()
{
  static bool time_checked = false;
  static byte curHour = 0;
  static byte curMinute = 0;

  if (!tasks.getTaskState(show_set_time_mode))
  {
    tasks.startTask(show_set_time_mode);
    tasks.startTask(return_to_default_mode);
    restartBlink();
    time_checked = false;
  }

  if (!time_checked)
  {
    DateTime dt = RTC.now();
    curHour = dt.hour();
    curMinute = dt.minute();
  }

  // опрос кнопок =====================
  if (btnClockSet.getButtonFlag() > BTN_FLAG_NONE)
  {
    if (time_checked)
    {
      saveTime(curHour, curMinute);
      time_checked = false;
    }
    if (btnClockSet.getButtonFlag() == BTN_FLAG_NEXT)
    {
      checkData(displayMode, DISPLAY_MODE_SET_TURN_ON_DELAY);
    }
    else
    {
      displayMode = DISPLAY_MODE_SHOW_TIME;
    }
    btnClockSet.setButtonFlag(BTN_FLAG_NONE);
    if (displayMode > DISPLAY_MODE_SET_MINUTE)
    {
      tasks.stopTask(show_set_time_mode);
      tasks.stopTask(return_to_default_mode);
      return;
    }
  }

  if (btnClockUp.getButtonFlag() == BTN_FLAG_NEXT)
  {
    switch (displayMode)
    {
    case DISPLAY_MODE_SET_HOUR:
      checkData(curHour, 23);
      break;
    case DISPLAY_MODE_SET_MINUTE:
      checkData(curMinute, 59);
      break;
    }
    time_checked = true;
    btnClockUp.setButtonFlag(BTN_FLAG_NONE);
  }

  // вывод данных на экран ============
  showTimeData(curHour, curMinute);
}

void showTemp()
{
  if (!tasks.getTaskState(show_temp_mode))
  {
    tasks.startTask(return_to_default_mode);
    tasks.startTask(show_temp_mode);
  }

  disp.showTemp(int(clock.getTemperature()));
}

void showDisplay()
{
  disp.show();
}

byte getCurLightData()
{
  // показания датчика ограничить пределом 100% ))
  byte result = (light_sensor_threshold > 1000) ? 100 : light_sensor_threshold / 10;
  return (result);
}

void showOtherSetting()
{
  static byte _data;
  static bool flag = false;

  if (!tasks.getTaskState(show_other_setting_mode))
  {
    restartBlink();
    switch (displayMode)
    {
    case DISPLAY_MODE_SET_TIMEOUT:
      _data = eeprom_read_byte(&e_sleep_on);
      break;
    case DISPLAY_MODE_SET_TURN_ON_DELAY:
      _data = eeprom_read_byte(&e_turn_on_delay) / 5;
      break;
    case DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN:
      _data = eeprom_read_byte(&e_show_temp_to_run);
      break;
    case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
      _data = eeprom_read_word(&e_al_threshold) / 10;
      break;
    case DISPLAY_MODE_SET_COLOR_1:
      _data = eeprom_read_byte(&e_color_1);
      break;
    case DISPLAY_MODE_SET_COLOR_2:
      _data = eeprom_read_byte(&e_color_2);
      break;
    }
    tasks.startTask(show_other_setting_mode);
    tasks.startTask(return_to_default_mode);
  }

  // опрос кнопок
  if (btnClockUp.getButtonFlag() == BTN_FLAG_NEXT)
  {
    switch (displayMode)
    {
    case DISPLAY_MODE_SET_TIMEOUT:
      checkData(_data, 60, 1);
      break;
    case DISPLAY_MODE_SET_TURN_ON_DELAY:
      checkData(_data, 10, 0);
      break;
    case DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN:
      _data = !_data;
      break;
    case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
      checkData(_data, 90, 10);
      break;
    case DISPLAY_MODE_SET_COLOR_1:
    case DISPLAY_MODE_SET_COLOR_2:
      checkData(_data, 9);
      (displayMode == DISPLAY_MODE_SET_COLOR_1) ? color_1 = _data : color_2 = _data;
      break;
    }

    flag = true;
    btnClockUp.setButtonFlag(BTN_FLAG_NONE);
  }

  if ((btnMode3.getButtonFlag() == BTN_FLAG_NEXT) && (displayMode == DISPLAY_MODE_SET_LIGHT_THRESHOLD))
  { // клик на кнопку третьего режима сразу вводит текущее значение с датчика света
    _data = getCurLightData();
    if (_data > 90)
    {
      _data = 90;
    }
    flag = true;
    btnMode3.setButtonFlag(BTN_FLAG_NONE);
  }

  if (btnClockSet.getButtonFlag() > BTN_FLAG_NONE)
  {
    if (flag)
    {
      switch (displayMode)
      {
      case DISPLAY_MODE_SET_TIMEOUT:
        eeprom_update_byte(&e_sleep_on, _data);
        tasks.setTaskInterval(sleep_on_timer, _data * 60000ul, false);
        break;
      case DISPLAY_MODE_SET_TURN_ON_DELAY:
        eeprom_update_byte(&e_turn_on_delay, _data * 5);
        break;
      case DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN:
        eeprom_update_byte(&e_show_temp_to_run, _data);
        break;
      case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
        eeprom_update_word(&e_al_threshold, _data * 10);
        break;
      case DISPLAY_MODE_SET_COLOR_1:
        eeprom_update_byte(&e_color_1, _data);
        break;
      case DISPLAY_MODE_SET_COLOR_2:
        eeprom_update_byte(&e_color_2, _data);
        break;
      }
      flag = false;
    }
    if (btnClockSet.getButtonFlag() == BTN_FLAG_NEXT)
    {
      checkData(displayMode, DISPLAY_MODE_SET_COLOR_2);
    }
    else
    {
      displayMode = DISPLAY_MODE_SHOW_TIME;
    }
    btnClockSet.setButtonFlag(BTN_FLAG_NONE);
    tasks.stopTask(show_other_setting_mode);
    tasks.stopTask(return_to_default_mode);

    return;
  }

  // вывод данных на экран
  switch (displayMode)
  {
  case DISPLAY_MODE_SET_TIMEOUT:
  case DISPLAY_MODE_SET_TURN_ON_DELAY:
  case DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN:
    showSettingData(_data);
    break;
  case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
    // если нажата кнопка первого режима, выводить на экран текущий уровень освещенности, иначе настраиваемое значение
    if (btnMode1.isButtonClosed())
    {
      showSettingData(getCurLightData());
    }
    else
    {
      showSettingData(_data);
    }
    break;
  case DISPLAY_MODE_SET_COLOR_1:
  case DISPLAY_MODE_SET_COLOR_2:
    showSettingData(_data + 1);
    break;
  }
}

// ===================================================
void checkIgnition()
{
  tasks.stopTask(sleep_on_timer);
  // если модуль спал, включить отображение температуры, если задано
  if (!tasks.getTaskState(display_guard))
  {
    displayMode = (eeprom_read_byte(&e_show_temp_to_run)) ? DISPLAY_MODE_SHOW_TEMP : DISPLAY_MODE_SHOW_TIME;
    tasks.startTask(display_guard);
  }
  else
  {
    displayMode = DISPLAY_MODE_SHOW_TIME;
  }
  // иногда при выходе из спящего режима при нулевой задержке свет включается сразу,
  // выключение и включение зажигания свет отключает, и дальше все работает как должно;
  // возможно, это проблема проводки автомобиля или какие-то наведенные импульсы в цепи
  // проверки запуска двигателя; здесь мы задерживаем считывание состояния входных
  // пинов на 200 мс после включения зажигания, перезапуская соответствующий таймер
  engine_run_flag = false;
  tasks.startTask(data_guard);
}

// ===================================================
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

void runLightMode()
{
  byte x = (auto_light_mode != AUTOLIGHT_MODE_3) ? auto_light_mode : 1;
  if (auto_light_mode == AUTOLIGHT_MODE_3 && light_sensor_threshold <= eeprom_read_word(&e_al_threshold))
  {
    x = 2;
  }
  setLightRelay(x);
}

void setLightRelay(byte rel)
{
  digitalWrite(RELAY_1_PIN, (auto_light_mode && (rel == 1) && engine_run_flag));
  digitalWrite(RELAY_2_PIN, (auto_light_mode && (rel == 2) && engine_run_flag));
}

// ===================================================
void showTime(DateTime dt)
{
  disp.showTime(dt.hour(), dt.minute(), blink_flag);
}

void showTimeData(byte hour, byte minute)
{
  // если наступило время блинка и кнопка Up не нажата, то стереть соответствующие разряды; при нажатых кнопках Up/Down во время изменения данных ничего не мигает
  if (!blink_flag && !btnClockUp.isButtonClosed())
  {
    switch (displayMode)
    {
    case DISPLAY_MODE_SET_HOUR:
      hour = -1;
      break;
    case DISPLAY_MODE_SET_MINUTE:
      minute = -1;
      break;
    }
  }
  disp.showTime(hour, minute, false);
}

void showSettingData(byte data)
{
  disp.clear();
  switch (displayMode)
  {
  case DISPLAY_MODE_SET_TIMEOUT:
  case DISPLAY_MODE_SET_TURN_ON_DELAY:
  case DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN:
    disp.setDispData(0, disp.encodeDigit(displayMode - 2));
    disp.setDispData(1, 0x5c);
    break;
  case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
    disp.setDispData(0, 0x38);
    break;
  case DISPLAY_MODE_SET_COLOR_1:
    disp.setDispData(0, 0x54);
    break;
  case DISPLAY_MODE_SET_COLOR_2:
    disp.setDispData(0, 0x1c);
    break;
  }
  // отрисовка данных только если не наступил блинк или нажата какая-то из кнопок Up, Set или  btnMode1; при нажатых кнопках ничего не мигает
  if (blink_flag || btnClockUp.isButtonClosed() || btnClockSet.isButtonClosed() || (displayMode == DISPLAY_MODE_SET_LIGHT_THRESHOLD && btnMode1.isButtonClosed()))
  {
    switch (displayMode)
    {
    case DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN:
      switch (data)
      {
      case 0:
        disp.setDispData(3, 0x08);
        break;
      default:
        disp.setDispData(3, 0x5c);
        break;
      }
      break;
    default:
      if (data >= 100)
      {
        disp.setDispData(1, disp.encodeDigit(data / 100));
        data = data % 100;
      }
      if (data >= 10)
      {
        disp.setDispData(2, disp.encodeDigit(data / 10));
      }
      disp.setDispData(3, disp.encodeDigit(data % 10));
      break;
    }
  }
}

// ===================================================
void saveTime(byte hour, byte minute)
{
  clock.setHour(hour);
  clock.setMinute(minute);
  clock.setSecond(0);
}

// ===================================================
void checkData(byte &_data, byte _max_data, byte _min_data)
{
  if (++_data > _max_data)
  {
    _data = _min_data;
  }
}

void restartBlink()
{
  tasks.stopTask(blink_timer);
  blink();
}

void setDisplay()
{
  switch (displayMode)
  {
  case DISPLAY_MODE_SHOW_TIME:
    showTime(RTC.now());
    break;
  case DISPLAY_MODE_SET_HOUR:
  case DISPLAY_MODE_SET_MINUTE:
    if (!tasks.getTaskState(show_set_time_mode))
    {
      showTimeSetting();
    }
    break;
  case DISPLAY_MODE_SET_TIMEOUT:
  case DISPLAY_MODE_SET_TURN_ON_DELAY:
  case DISPLAY_MODE_SET_LIGHT_THRESHOLD:
  case DISPLAY_MODE_SET_COLOR_1:
  case DISPLAY_MODE_SET_COLOR_2:
  case DISPLAY_MODE_SET_SHOW_TEMP_TO_RUN:
    if (!tasks.getTaskState(show_other_setting_mode))
    {
      showOtherSetting();
    }
    break;
  case DISPLAY_MODE_SHOW_TEMP:
    if (!tasks.getTaskState(show_temp_mode))
    {
      showTemp();
    }
    break;
  }
}

// ===================================================
void setup()
{
  // Serial.begin(115200);

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
    eeprom_update_word(&e_al_threshold, 800);
  }
  color_1 = eeprom_read_byte(&e_color_1);
  if (color_1 > 9)
  {
    color_1 = 4;
    eeprom_update_byte(&e_color_1, color_1);
  }
  color_2 = eeprom_read_byte(&e_color_2);
  if (color_2 > 9)
  {
    color_2 = 0;
    eeprom_update_byte(&e_color_1, color_2);
  }
  if (eeprom_read_byte(&e_turn_on_delay) > 50)
  {
    eeprom_update_byte(&e_turn_on_delay, 5);
  }

  // прерывания =================================
  attachInterrupt(0, checkIgnition, RISING);

  // Часы =======================================
  Wire.begin();
  clock.setClockMode(false); // 24-часовой режим

  // кнопки =====================================
  btnClockUp.setLongClickMode(LCM_CLICKSERIES);
  btnClockUp.setIntervalOfSerial(100);

  // задачи =====================================
  byte t = eeprom_read_byte(&e_sleep_on);
  if ((t > 60) || (t == 0))
  {
    t = 10;
    eeprom_update_byte(&e_sleep_on, t);
  }

  sleep_on_timer = tasks.addTask(t * 60000ul, powerOffTimer, false);
  data_guard = tasks.addTask(200, checkInputData);
  low_beam_off_timer = tasks.addTask(30000, lowBeamOff, false);
  blink_timer = tasks.addTask(500, blink);
  return_to_default_mode = tasks.addTask(6000, returnToDefMode, false);
  show_set_time_mode = tasks.addTask(100, showTimeSetting, false);
  show_other_setting_mode = tasks.addTask(100, showOtherSetting, false);
  show_temp_mode = tasks.addTask(500, showTemp, false);
  light_sensor_guard = tasks.addTask(100, lightSensorRead);
  display_guard = tasks.addTask(100, showDisplay);
  leds_guard = tasks.addTask(100, setLeds);
}

void loop()
{
  checkButton();
  tasks.tick();
  setDisplay();
}