#pragma once
#include <Arduino.h>
#include <DS3231.h>

// ==== пины =========================================
#define IGNITION_PIN 2      // пин, на который приходит сигнал с линии зажигания
#define ENGINE_RUN_PIN 4    // пин, на который приходит сигнал с вывода D генератора или HIGH при запущенном двигателе
#define BTN_MODE1_PIN 7     // пин кнопки включения первого режима автосвета
#define BTN_MODE2_PIN 8     // пин кнопки включения второго режима автосвета
#define BTN_MODE3_PIN 9     // пин кнопки включения третьего режима автосвета
#define BTN_CLOCK_SET_PIN 6 // пин кнопки включения режима настройки часов
#define BTN_CLOCK_UP_PIN 5  // пин кнопки смены значений в режиме настройки
#define LEDS_DATA_PIN A0    // дата-пин адресных светодиодов-индикаторов
#define RELAY_1_PIN A1      // пин реле включения противотуманок
#define RELAY_2_PIN A2      // пин реле включения ближнего света
#define LIGHT_SENSOR_PIN A6 // пин датчика света
#define DISPLAY_CLK_PIN 11  // пин для подключения экрана - CLK
#define DISPLAY_DAT_PIN 10  // пин для подключения экрана - DAT

// ==== опрос кнопок =================================
void checkButton();
void checkSetButton();
void checkUpButton();
void checkModeButton();

// ==== задачи =======================================
void powerOffTimer();    // таймер ухода в сон через 10 минут после отключения зажигания
void checkInputData();   // отслеживание запуска двигателя и отключения зажигания
void setLeds();          // управление светодиодами - индикаторами автосвета
void lowBeamOff();       // отключение ближнего света через 30 секунд после превышения порога датчика света
void blink();            // таймер блинка
void returnToDefMode();  // таймер автовозврата к режиму показа текущего времени
void showTimeSetting();  // режим настройки времени
void showTemp();         // вывод температуры на экран
void showDisplay();      // отрисовка информации на экране
void lightSensorRead();  // опрос данных с датчика света
void showOtherSetting(); // режим настройки прочих параметров

// ==== управление спящим режимом ====================
// отслеживание включения зажигания в прерывании
void checkIgnition();

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

// переключение света согласно установленного режима
// - rel - реле, подлежащее включению; 1 - реле противотуманок, 2 - реле ближнего света, 0 - все выключить
void setLightRelay(byte rel = 0);

// вывод настройки порога срабатывания датчика света
void showLightThresholdSetting();

// ==== экран ========================================
#define DISPLAY_MODE_SHOW_TIME 0           // основной режим - вывод времени на экран
#define DISPLAY_MODE_SET_HOUR 1            // режим настройки часов
#define DISPLAY_MODE_SET_MINUTE 2          // режим настройки минут
#define DISPLAY_MODE_SET_TIMEOUT 3         // режим настройки времени ухода в спящий режим
#define DISPLAY_MODE_SET_TURN_ON_DELAY 4   // режим настройки задержки включения света
#define DISPLAY_MODE_SET_LIGHT_THRESHOLD 5 // режим настройки порога срабатывания датчика света
#define DISPLAY_MODE_SET_COLOR_1 6         // режим настройки цвета индикации работы ПТФ или ДХО
#define DISPLAY_MODE_SET_COLOR_2 7         // режим настройки цвета индикации работа ближнего света
#define DISPLAY_MODE_SHOW_TEMP 20          // режим вывода температуры

// вывод на экран времени
void showTime(DateTime dt);
// вывод на экран данных в режиме настройки времени
void showTimeData(byte hour, byte minute);
// вывод на экран данных в других режимах настройки
//  data - данные для вывода
void showSettingData(byte data);

// ==== часы =========================================
// сохранение времени после настройки
void saveTime(byte hour, byte minute);

// ==== разное =======================================
// изменение данных по клику кнопки с контролем выхода за предельное значение
void checkData(byte &_data, byte _max_data, byte _min_data = 0);

void restartBlink();

// вывод на экран взависимости от режима
void setDisplay();
