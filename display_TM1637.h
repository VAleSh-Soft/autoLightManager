#pragma once
#include <Arduino.h>
#include <TM1637Display.h> // https://github.com/avishorp/TM1637

// пины для подключения экрана
#define DISPLAY_CLK_PIN 11
#define DISPLAY_DAT_PIN 10

TM1637Display tm(DISPLAY_CLK_PIN, DISPLAY_DAT_PIN);

// ==== класс для вывода данных на экран =============
class Display
{
private:
  byte *data = NULL;
  byte _brightness = 1;

public:
  Display()
  {
    data = new byte[4];
    clear();
  }

  // очистка буфера экрана, сам экран при этом не очищается
  void clear()
  {
    for (byte i = 0; i < 4; i++)
    {
      data[i] = 0x00;
    }
  }

  // очистка экрана
  void sleep()
  {
    clear();
    tm.setSegments(data);
  }

  // установка разряда _index буфера экрана
  void setDispData(byte _index, byte _data)
  {
    if (_index < 4)
    {
      data[_index] = _data;
    }
  }

  // получение значения разряда _index буфера экрана
  byte getDispData(byte _index)
  {
    byte result = 0;
    if (_index < 4)
    {
      result = data[_index];
    }
    return (result);
  }

  // отрисовка на экране содержимого его буфера
  void show()
  {
    bool flag = false;
    static byte _data[4] = {0x00, 0x00, 0x00, 0x00};
    static byte br = 0;
    for (byte i = 0; i < 4; i++)
    {
      flag = _data[i] != data[i];
      if (flag)
      {
        break;
      }
    }
    if (!flag)
    {
      flag = br != _brightness;
    }
    // отрисовка экрана происходит только если изменился хотя бы один разряд или изменилась яркость
    if (flag)
    {
      for (byte i = 0; i < 4; i++)
      {
        _data[i] = data[i];
      }
      br = _brightness;
      tm.setSegments(data);
    }
  }

  // отрисовка на экране  времени; если задать какое-то из значений hour или minute отрицательным, эта часть экрана будет очищена; show_colon - отображать или нет двоеточие между часами и минутами
  void showTimeData(int8_t hour, int8_t minute, bool show_colon)
  {
    clear();
    if (hour >= 0)
    {
      data[0] = encodeDigit(hour / 10);
      data[1] = encodeDigit(hour % 10);
    }
    if (minute >= 0)
    {
      data[2] = encodeDigit(minute / 10);
      data[3] = encodeDigit(minute % 10);
    }
    if (show_colon)
    {
      data[1] |= 0x80; // для показа двоеточия установить старший бит во второй цифре
    }
  }

  // установка яркости экрана; реально яркость будет изменена только после вызова метода show()
  void setBrightness(byte brightness, bool on = true)
  {
    _brightness = brightness;
    tm.setBrightness(brightness, on);
  }

  // преобразование цифры в битовую маску для вывода ее на экран
  byte encodeDigit(byte num)
  {
    return (tm.encodeDigit(num));
  }
};
