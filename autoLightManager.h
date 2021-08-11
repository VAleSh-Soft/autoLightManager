#pragma once
#include <Arduino.h>
#include <DS3231.h>

// ==== пины =========================================

#define IGNITION_PIN 2     // пин, на который приходит сигнал с линии зажигания
#define ENGINE_RUN_PIN 4    // пин, на который приходит сигнал с вывода D генератора или HIGH при запущенном двигателе
#define BTN_ALM1_PIN 7      // пин кнопки включения первого режима автосвета
#define BTN_ALM2_PIN 8      // пин кнопки включения второго режима автосвета
#define BTN_ALM3_PIN 9      // пин кнопки включения третьего режима автосвета
#define BTN_CLOCK_SET_PIN 6 // пин кнопки включения режима настройки часов
#define BTN_CLOCK_UP_PIN 5  // пин кнопки смены значений в режиме настройки
#define LEDS_DATA_PIN A0    // дата-пин адресных светодиодов-индикаторов
#define RELAY_1_PIN A1      // пин реле включения противотуманок
#define RELAY_2_PIN A2      // пин реле включения ближнего света
#define LIGHT_SENSOR_PIN A6 // пин датчика света

// ==== разное =======================================

// набор обязательных автоматических действий
void allTick();

// таймер автовозврата к режиму показа текущего времени
void returnToDefModeDisplay();

// ==== управление спящим режимом ====================

// отслеживание включения зажигания в прерывании
void checkIgnition();

// отслеживание запуска двигателя и отключения зажигания
void checkInputData();

// таймер ухода в сон через 10 минут после отключения зажигания
void powerOffTimer();

// ==== управление автосветом ========================

#define AUTOLIGHT_MODE_0 0 // режим автосвета - все выключено, ручной режим
#define AUTOLIGHT_MODE_1 1 // режим автосвета - только противотуманки
#define AUTOLIGHT_MODE_2 2 // режим автосвета - только ближний свет
#define AUTOLIGHT_MODE_3 3 // режим автосвета - работа датчика света

// переключение режима автосвета
// - mode_btn - режим, выбираемый нажатой кнопкой
void setAutoLightMode(byte mode_btn);

// начальный старт света при включении любого режима при запущенном двигателе
void runLightMode();

// обработка данных с датчика света
void lightSensorRead();

// отключение ближнего света через 30 секунд после превышения порога датчика света
void lowBeamOff();

// переключение света согласно установленного режима
// - rel - реле, подлежащее включению; 1 - реле противотуманок, 2 - реле ближнего света, 0 - все выключить
void setLightRelay(byte rel = 0);

// управление светодиодами-индикаторами автосвета
void setLeds();

// вывод настройки порога срабатывания датчика света
void showLightThresholdSetting();

// обработка кнопок ==================================

// опрос кнопок управления автосветом
void checkBtnAlm();

// опрос кнопок управления часами
void checkClockBtn();

// опрос кнопки btnClockUp; возвращает true, если данные были изменены
// data - данные, подлежащие изменению при отработке нажатий кнопки
// min_data - минимальное значение данных
// max_data - максимальное значение данных
bool checkBtnUp(uint16_t &_data, int min_data, int max_data);

// ==== дисплей ======================================

#define DISPLAY_MODE_SHOW_TIME 0            // основной режим - вывод времени на индикатор
#define DISPLAY_MODE_SET_HOUR 1             // режим настройки часов
#define DISPLAY_MODE_SET_MINUTE 2           // режим настройки минут
#define DISPLAY_MODE_SET_TIMEOUT 3          // режим настройки времени ухода в спящий режим
#define DISPLAY_MODE_SET_LIGHT_THRESHOLD 10 // режим настройки порога срабатывания датчика света

// вывод на экран данных в режиме настройки времени
void showTimeSettingData(byte hour, byte minute);

// ==== часы =========================================

// таймер блинка
void blinkTimer();

// перезапуск таймера блинка
void restartBlinkTimer();

// вывод времени на индикатор
void showTime(DateTime dt, bool force = false);
void showTime(byte hour, byte minute, bool force = false);

// сохранение времени после настройки
void saveTime(byte hour, byte minute);

// режим настройки времени
void showTimeSetting();
