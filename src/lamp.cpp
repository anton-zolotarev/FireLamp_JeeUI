/*
Copyright © 2020 Dmytro Korniienko (kDn)
JeeUI2 lib used under MIT License Copyright (c) 2019 Marsel Akhkamov

    This file is part of FireLamp_JeeUI.

    FireLamp_JeeUI is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FireLamp_JeeUI is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FireLamp_JeeUI.  If not, see <https://www.gnu.org/licenses/>.

  (Этот файл — часть FireLamp_JeeUI.

   FireLamp_JeeUI - свободная программа: вы можете перераспространять ее и/или
   изменять ее на условиях Стандартной общественной лицензии GNU в том виде,
   в каком она была опубликована Фондом свободного программного обеспечения;
   либо версии 3 лицензии, либо (по вашему выбору) любой более поздней
   версии.

   FireLamp_JeeUI распространяется в надежде, что она будет полезной,
   но БЕЗО ВСЯКИХ ГАРАНТИЙ; даже без неявной гарантии ТОВАРНОГО ВИДА
   или ПРИГОДНОСТИ ДЛЯ ОПРЕДЕЛЕННЫХ ЦЕЛЕЙ. Подробнее см. в Стандартной
   общественной лицензии GNU.

   Вы должны были получить копию Стандартной общественной лицензии GNU
   вместе с этой программой. Если это не так, см.
   <https://www.gnu.org/licenses/>.)
*/

#include "lamp.h"
#include "main.h"
#include "misc.h"

extern LAMP myLamp; // Объект лампы

void LAMP::lamp_init()
{
  FastLED.addLeds<WS2812B, LAMP_PIN, COLOR_ORDER>(leds, NUM_LEDS)  /*.setCorrection(TypicalLEDStrip)*/;
  brightness(0, false);                          // начинаем с полностью потушеной матрицы 0-й яркости
  if (CURRENT_LIMIT > 0){
    FastLED.setMaxPowerInVoltsAndMilliamps(5, CURRENT_LIMIT); // установка максимального тока БП
  }
  FastLED.clear();                                            // очистка матрицы
  //FastLED.show(); // для ESP32 вызывает перезагрузку циклическую!!!
  // ПИНЫ
#ifdef MOSFET_PIN                                         // инициализация пина, управляющего MOSFET транзистором в состояние "выключен"
  pinMode(MOSFET_PIN, OUTPUT);
#ifdef MOSFET_LEVEL
  digitalWrite(MOSFET_PIN, !MOSFET_LEVEL);
#endif
#endif

#ifdef ALARM_PIN                                          // инициализация пина, управляющего будильником в состояние "выключен"
  pinMode(ALARM_PIN, OUTPUT);
#ifdef ALARM_LEVEL
  digitalWrite(ALARM_PIN, !ALARM_LEVEL);
#endif
#endif

  // TELNET
#if defined(LAMP_DEBUG) && DEBUG_TELNET_OUTPUT
  telnetServer.begin();
  for (uint8_t i = 0; i < 100; i++)                         // пауза 10 секунд в отладочном режиме, чтобы успеть подключиться по протоколу telnet до вывода первых сообщений
  {
    handleTelnetClient();
    delay(100);
    ESP.wdtFeed();
  }
#endif

#ifdef VERTGAUGE
      if(VERTGAUGE){
        xStep = WIDTH / 4;
        xCol = 4;
        if(xStep<2) {
          xStep = WIDTH / 3;
          xCol = 3;
        } else if(xStep<2) {
          xStep = WIDTH / 2;
          xCol = 2;
        } else if(xStep<2) {
          xStep = 1;
          xCol = 1;
        }

        yStep = HEIGHT / 4;
        yCol = 4;
        if(yStep<2) {
          yStep = HEIGHT / 3;
          yCol = 3;
        } else if(yStep<2) {
          yStep = HEIGHT / 2;
          yCol = 2;
        } else if(yStep<2) {
          yStep = 1;
          yCol = 1;
        }
    }
#endif
}

void LAMP::handle()
{
#ifdef MIC_EFFECTS
  static unsigned long mic_check;
  if(isMicOn && (ONflag || isMicCalibration()) && !isAlarm() && mic_check + MIC_POLLRATE < millis()){
    micHandler();
    mic_check = millis();
  }
#endif

#if defined(LAMP_DEBUG) && DEBUG_TELNET_OUTPUT
  handleTelnetClient();
#endif

  // все что ниже, будет выполняться раз в 0.999 секундy
  static unsigned long wait_handlers;
  if (wait_handlers + 999U > millis())
      return;
  wait_handlers = millis();

#ifdef LAMP_DEBUG
EVERY_N_SECONDS(15){
  // fps counter
  LOG(printf, "Eff:%d FPS: %u\n", effects.getEn(), fps);
  LOG(printf_P, PSTR("MEM stat: %d, HF: %d, Time: %s\n"), ESP.getFreeHeap(), ESP.getHeapFragmentation(), myLamp.timeProcessor.getFormattedShortTime().c_str());
}
  fps = 0;
#endif

  // будильник обрабатываем раз в секунду
  alarmWorker();

  if(isEffectsDisabledUntilText && !isStringPrinting) {
    setBrightness(0,false,false); // напечатали, можно гасить матрицу :)
    isEffectsDisabledUntilText = false;
  }

  // отложенное включение/выключение
  if(isOffAfterText && !isStringPrinting) {
    isOffAfterText = false;
    changePower(false);
  }

  newYearMessageHandle();
  periodicTimeHandle();
  ConfigSaveCheck(); // для взведенного таймера автосохранения настроек

#ifdef OTA
  otaManager.HandleOtaUpdate();                       // ожидание и обработка команды на обновление прошивки по воздуху
#endif

  //timeProcessor.handleTime();                         // Обновление времени

  // обработчик событий (пока не выкину в планировщик)
  if (isEventsHandled) {
    events.events_handle();
  }

}

// обработчик будильника "рассвет"
void LAMP::alarmWorker(){
    // static CHSV GSHMEM.dawnColorMinus[6];                                            // цвет "рассвета"
    // static uint8_t GSHMEM.dawnCounter = 0;                                           // счётчик первых шагов будильника
    // static time_t GSHMEM.startmillis;

    if (mode != LAMPMODE::MODE_ALARMCLOCK){
      dawnFlag = false;
      return;
    }

    // проверка рассвета, первый вход в функцию
    if (!dawnFlag){
      GSHMEM.startmillis = millis();
      memset(GSHMEM.dawnColorMinus,0,sizeof(GSHMEM.dawnColorMinus));
      GSHMEM.dawnCounter = 0;
      FastLED.clear();
      brightness(BRIGHTNESS, false);   // не помню, почему тут стояло 255... надо будет проверить работу рассвета :), ниже есть доп. ограничение - DAWN_BRIGHT
      // величина рассвета 0-255
      int16_t dawnPosition = map((millis()-GSHMEM.startmillis)/1000,0,300,0,255); // 0...300 секунд приведенные к 0...255
      dawnPosition = constrain(dawnPosition, 0, 255);
      GSHMEM.dawnColorMinus[0] = CHSV(map(dawnPosition, 0, 255, 10, 35),
        map(dawnPosition, 0, 255, 255, 170),
        map(dawnPosition, 0, 255, 10, DAWN_BRIGHT)
      );
    }

    if (((millis() - GSHMEM.startmillis) / 1000 > (5 + DAWN_TIMEOUT) * 60+30)) {
      // рассвет закончился
      stopAlarm();
      // #if defined(ALARM_PIN) && defined(ALARM_LEVEL)                    // установка сигнала в пин, управляющий будильником
      // digitalWrite(ALARM_PIN, !ALARM_LEVEL);
      // #endif

      // #if defined(MOSFET_PIN) && defined(MOSFET_LEVEL)                  // установка сигнала в пин, управляющий MOSFET транзистором, соответственно состоянию вкл/выкл матрицы
      // digitalWrite(MOSFET_PIN, ONflag ? MOSFET_LEVEL : !MOSFET_LEVEL);
      // #endif
      return;
    }

    // проверка рассвета
    EVERY_N_SECONDS(10){
      // величина рассвета 0-255
      int16_t dawnPosition = map((millis()-GSHMEM.startmillis)/1000,0,300,0,255); // 0...300 секунд приведенные к 0...255
      dawnPosition = constrain(dawnPosition, 0, 255);
      GSHMEM.dawnColorMinus[0] = CHSV(map(dawnPosition, 0, 255, 10, 35),
        map(dawnPosition, 0, 255, 255, 170),
        map(dawnPosition, 0, 255, 10, DAWN_BRIGHT)
      );
      GSHMEM.dawnCounter++; //=GSHMEM.dawnCounter%(sizeof(GSHMEM.dawnColorMinus)/sizeof(CHSV))+1;

      for (uint8_t i = sizeof(GSHMEM.dawnColorMinus) / sizeof(CHSV) - 1; i > 0U; i--){
          GSHMEM.dawnColorMinus[i]=((GSHMEM.dawnCounter > i)?GSHMEM.dawnColorMinus[i-1]:GSHMEM.dawnColorMinus[i]);
      }
    }

#ifdef PRINT_ALARM_TIME
    EVERY_N_SECONDS(1){
      if (timeProcessor.seconds00()) {
        CRGB letterColor;
        hsv2rgb_rainbow(GSHMEM.dawnColorMinus[0], letterColor); // конвертация цвета времени, с учетом текущей точки рассвета
        sendStringToLamp(timeProcessor.getFormattedShortTime().c_str(), letterColor, true);
      }
    }
#endif

    for (uint16_t i = 0U; i < NUM_LEDS; i++) {
        leds[i] = GSHMEM.dawnColorMinus[i%(sizeof(GSHMEM.dawnColorMinus)/sizeof(CHSV))];
    }
    dawnFlag = true;
}

void LAMP::effectsTick(){
  /*
   * Здесь имеет место странная специфика тикера,
   * если где-то в коде сделали детач, но таймер уже успел к тому времени "выстрелить"
   * функция все равно будет запущена в loop(), она просто ждет своей очереди
   */
  if (!_effectsTicker.active() ) return;

  uint32_t _begin = millis();
  if (isAlarm()) {
    doPrintStringToLamp(); // обработчик печати строки
    _effectsTicker.once_ms_scheduled(LED_SHOW_DELAY, std::bind(&LAMP::frameShow, this, _begin));
    return;
  }

  if(!isEffectsDisabledUntilText){
    // посчитать текущий эффект (сохранить кадр в буфер, если ОК)
    if(effects.worker->run(getUnsafeLedsArray(), effects.getCurrent())) {
#ifdef USELEDBUF
      ledsbuff.resize(NUM_LEDS);
      std::copy(leds, leds + NUM_LEDS, ledsbuff.begin());
#endif
    }
  }

  doPrintStringToLamp(); // обработчик печати строки
#ifdef VERTGAUGE
  GaugeMix();
#endif

  if (isEffectsDisabledUntilText || effects.worker->status()) {
    // выводим кадр только если есть текст или эффект
    _effectsTicker.once_ms_scheduled(LED_SHOW_DELAY, std::bind(&LAMP::frameShow, this, _begin));
  } else {
    // иначе возвращаемся к началу обсчета следующего кадра
    _effectsTicker.once_ms_scheduled(EFFECTS_RUN_TIMER, std::bind(&LAMP::effectsTick, this));
  }

}

/*
 * вывод готового кадра на матрицу,
 * и перезапуск эффект-процессора
 */
void LAMP::frameShow(const uint32_t ticktime){
  /*
   * Здесь имеет место странная специфика тикера,
   * если где-то в коде сделали детач, но таймер уже успел к тому времени "выстрелить"
   * функция все равно будет запущена в loop(), она просто ждет своей очереди
   */
  if (!_effectsTicker.active() ) return;

  FastLED.show();
// восстановление кадра с прорисованным эффектом из буфера (без текста и индикаторов)
#ifdef USELEDBUF
  if (!ledsbuff.empty()) {
    std::copy( ledsbuff.begin(), ledsbuff.end(), leds );
    ledsbuff.resize(0);
    ledsbuff.shrink_to_fit();
  }
#endif

  // откладываем пересчет эффекта на время для желаемого FPS, либо
  // на минимальный интервал в следующем loop()
  int32_t delay = EFFECTS_RUN_TIMER + ticktime - millis();
  if (delay < LED_SHOW_DELAY) delay = LED_SHOW_DELAY;
  _effectsTicker.once_ms_scheduled(delay, std::bind(&LAMP::effectsTick, this));
#ifdef LAMP_DEBUG
  ++fps;
#endif
}

#ifdef VERTGAUGE
    void LAMP::GaugeShow(unsigned val, unsigned max, byte hue) {
      gauge_time = millis();
      gauge_val = val;
      gauge_max = max;
      gauge_hue = hue;
    }

    void LAMP::GaugeMix() {
      if (gauge_time + 3000 < millis()) return;

#if (VERTGAUGE==1)
      byte ind = (byte)((gauge_val + 1) * HEIGHT / (float)gauge_max + 1);
      for (byte x = 0; x <= xCol * (xStep - 1); x += xStep) {
        for (byte y = 0; y < HEIGHT ; y++) {
          if (ind > y)
            drawPixelXY(x, y, CHSV(gauge_hue, 255, 255));
          else
            drawPixelXY(x, y,  0);
        }
      }
#else
      byte ind = (byte)((gauge_val + 1) * WIDTH / (float)gauge_max + 1);
      for (byte y = 0; y <= yCol * (yStep - 1) ; y += yStep) {
        for (byte x = 0; x < WIDTH ; x++) {
          if (ind > x)
            drawPixelXY((x + y) % WIDTH, y, CHSV(gauge_hue, 255, 255));
          else
            drawPixelXY((x + y) % WIDTH, y,  0);
        }
      }
#endif
    }
#endif


LAMP::LAMP() : docArrMessages(512), tmConfigSaveTime(0), tmStringStepTime(DEFAULT_TEXT_SPEED), tmNewYearMessage(0), _fadeTicker(), _fadeeffectTicker()
#ifdef OTA
    , otaManager((void (*)(CRGB, uint32_t, uint16_t))(&showWarning))
#endif
    {
      MIRR_V = false; // отзрекаливание по V
      MIRR_H = false; // отзрекаливание по H
      dawnFlag = false; // флаг устанавливается будильником "рассвет"
      ONflag = false; // флаг включения/выключения
      isFaderON = true; // признак того, что используется фейдер для смены эффектов
      isGlobalBrightness = false; // признак использования глобальной яркости для всех режимов

      isStringPrinting = false; // печатается ли прямо сейчас строка?
      isEffectsDisabledUntilText = false;
      isOffAfterText = false;
      isEventsHandled = true;
      _brt =0;
      _steps = 0;
      _brtincrement = 0;
#ifdef MIC_EFFECTS
      isCalibrationRequest = false; // находимся ли в режиме калибровки микрофона
      isMicOn = true; // глобальное испльзование микрофона
      micAnalyseDivider = 1; // анализ каждый раз
#endif

      lamp_init(); // инициализация и настройка лампы
    }

void LAMP::changePower() {changePower(!ONflag);}

void LAMP::changePower(bool flag) // флаг включения/выключения меняем через один метод
{
  stopAlarm();            // любая активность в интерфейсе - отключаем будильник
  if (flag == ONflag) return;  // пропускаем холостые вызовы
  LOG(printf_P, PSTR("Lamp powering %s\n"), flag ? "ON": "Off");
  ONflag = flag;

  if (flag){
    effectsTimer(T_ENABLE);
  } else  {
    fadelight(0, FADE_TIME, std::bind(&LAMP::effectsTimer, this, SCHEDULER::T_DISABLE));  // гасим эффект-процессор
    demoTimer(T_DISABLE);     // гасим Демо-таймер
  }

#if defined(MOSFET_PIN) && defined(MOSFET_LEVEL)          // установка сигнала в пин, управляющий MOSFET транзистором, соответственно состоянию вкл/выкл матрицы
      digitalWrite(MOSFET_PIN, (ONflag ? MOSFET_LEVEL : !MOSFET_LEVEL));
#endif

      if (CURRENT_LIMIT > 0){
        FastLED.setMaxPowerInVoltsAndMilliamps(5, CURRENT_LIMIT); // установка максимального тока БП, более чем актуально))). Проверил, без этого куска - ограничение по току не работает :)
      }
}


uint32_t LAMP::getPixelNumber(uint16_t x, uint16_t y) // получить номер пикселя в ленте по координатам
{
  if ((THIS_Y % 2 == 0) || MATRIX_TYPE)                     // если чётная строка
  {
    return ((uint32_t)THIS_Y * SEGMENTS * _WIDTH + THIS_X)%NUM_LEDS;
  }
  else                                                      // если нечётная строка
  {
    return ((uint32_t)THIS_Y * SEGMENTS * _WIDTH + _WIDTH - THIS_X - 1)%NUM_LEDS;
  }
}

uint32_t LAMP::getPixColor(uint32_t thisSegm) // функция получения цвета пикселя по его номеру
{
  uint32_t thisPixel = thisSegm * SEGMENTS;
  if (thisPixel > NUM_LEDS - 1) return 0;
  return (((uint32_t)leds[thisPixel].r << 16) | ((uint32_t)leds[thisPixel].g << 8 ) | (uint32_t)leds[thisPixel].b);
}

void LAMP::fillAll(CRGB color) // залить все
{
  for (int32_t i = 0; i < NUM_LEDS; i++)
  {
    leds[i] = color;
  }
}

void LAMP::drawPixelXY(int16_t x, int16_t y, CRGB color) // функция отрисовки точки по координатам X Y
{
  if (x < 0 || x > (int16_t)(WIDTH - 1) || y < 0 || y > (int16_t)(HEIGHT - 1)) return;
  uint32_t thisPixel = getPixelNumber((uint16_t)x, (uint16_t)y) * SEGMENTS;
  for (uint16_t i = 0; i < SEGMENTS; i++)
  {
    leds[thisPixel + i] = color;
  }
}

void LAMP::drawPixelXYF(float x, float y, CRGB color)
{
  // extract the fractional parts and derive their inverses
  uint8_t xx = (x - (int)x) * 255, yy = (y - (int)y) * 255, ix = 255 - xx, iy = 255 - yy;
  // calculate the intensities for each affected pixel
  #define WU_WEIGHT(a,b) ((uint8_t) (((a)*(b)+(a)+(b))>>8))
  uint8_t wu[4] = {WU_WEIGHT(ix, iy), WU_WEIGHT(xx, iy),
                   WU_WEIGHT(ix, yy), WU_WEIGHT(xx, yy)};
  // multiply the intensities by the colour, and saturating-add them to the pixels
  for (uint8_t i = 0; i < 4; i++) {
    int16_t xn = x + (i & 1), yn = y + ((i >> 1) & 1);
    CRGB clr = myLamp.getPixColorXY(xn, yn);
    clr.r = qadd8(clr.r, (color.r * wu[i]) >> 8);
    clr.g = qadd8(clr.g, (color.g * wu[i]) >> 8);
    clr.b = qadd8(clr.b, (color.b * wu[i]) >> 8);
    myLamp.drawPixelXY(xn, yn, clr);
  }
}


void LAMP::drawLine(int x1, int y1, int x2, int y2, CRGB color){
  int deltaX = abs(x2 - x1);
  int deltaY = abs(y2 - y1);
  int signX = x1 < x2 ? 1 : -1;
  int signY = y1 < y2 ? 1 : -1;
  int error = deltaX - deltaY;

  drawPixelXY(x2, y2, color);
  while (x1 != x2 || y1 != y2) {
      drawPixelXY(x1, y1, color);
      int error2 = error * 2;
      if (error2 > -deltaY) {
          error -= deltaY;
          x1 += signX;
      }
      if (error2 < deltaX) {
          error += deltaX;
          y1 += signY;
      }
  }
}

void LAMP::drawLineF(float x1, float y1, float x2, float y2, CRGB color){
  float deltaX = std::abs(x2 - x1);
  float deltaY = std::abs(y2 - y1);
  float error = deltaX - deltaY;

  float signX = x1 < x2 ? 0.5 : -0.5;
  float signY = y1 < y2 ? 0.5 : -0.5;

  while (true) {
      if ((signX > 0 && x1 > x2+signX) || (signX < 0 && x1 < x2+signX)) break;
      if ((signY > 0 && y1 > y2+signY) || (signY < 0 && y1 < y2+signY)) break;
      drawPixelXYF(x1, y1, color);
      float error2 = error;
      if (error2 > -deltaY) {
          error -= deltaY;
          x1 += signX;
      }
      if (error2 < deltaX) {
          error += deltaX;
          y1 += signY;
      }
  }
}

void LAMP::drawCircle(int16_t x0, int16_t y0, int16_t radius, CRGB color){
  int16_t x = 0, y = radius, error = 0;
  int16_t delta = 1 - 2 * radius;

  while (y >= 0) {
    drawPixelXY(x0 + x, y0 + y, color);
    drawPixelXY(x0 + x, y0 - y, color);
    drawPixelXY(x0 - x, y0 + y, color);
    drawPixelXY(x0 - x, y0 - y, color);
    error = 2 * (delta + y) - 1;
    if (delta < 0 && error <= 0) {
      ++x;
      delta += 2 * x + 1;
      continue;
    }
    error = 2 * (delta - x) - 1;
    if (delta > 0 && error > 0) {
      --y;
      delta += 1 - 2 * y;
      continue;
    }
    ++x;
    delta += 2 * (x - y);
    --y;
  }
}

// void drawCircle(int16_t x0, int16_t y0, uint16_t radius, const CRGB & color){
//   int a = radius, b = 0;
//   int radiusError = 1 - a;

//   if (radius == 0) {
//     myLamp.drawPixelXY(x0, y0, color);
//     return;
//   }

//   while (a >= b)  {
//     myLamp.drawPixelXY(a + x0, b + y0, color);
//     myLamp.drawPixelXY(b + x0, a + y0, color);
//     myLamp.drawPixelXY(-a + x0, b + y0, color);
//     myLamp.drawPixelXY(-b + x0, a + y0, color);
//     myLamp.drawPixelXY(-a + x0, -b + y0, color);
//     myLamp.drawPixelXY(-b + x0, -a + y0, color);
//     myLamp.drawPixelXY(a + x0, -b + y0, color);
//     myLamp.drawPixelXY(b + x0, -a + y0, color);
//     b++;
//     if (radiusError < 0)
//       radiusError += 2 * b + 1;
//     else
//     {
//       a--;
//       radiusError += 2 * (b - a + 1);
//     }
//   }
// }

void LAMP::drawCircleF(float x0, float y0, float radius, CRGB color){
  float x = 0, y = radius, error = 0;
  float delta = 1 - 2 * radius;

  while (y >= 0) {
    drawPixelXYF(x0 + x, y0 + y, color);
    drawPixelXYF(x0 + x, y0 - y, color);
    drawPixelXYF(x0 - x, y0 + y, color);
    drawPixelXYF(x0 - x, y0 - y, color);
    error = 2 * (delta + y) - 1;
    if (delta < 0 && error <= 0) {
      ++x;
      delta += 2 * x + 1;
      continue;
    }
    error = 2 * (delta - x) - 1;
    if (delta > 0 && error > 0) {
      --y;
      delta += 1 - 2 * y;
      continue;
    }
    ++x;
    delta += 2 * (x - y);
    --y;
  }
}

void LAMP::startAlarm(){
  storedMode = ((mode == LAMPMODE::MODE_ALARMCLOCK) ? storedMode: mode);
  mode = LAMPMODE::MODE_ALARMCLOCK;
  effectsTimer(T_ENABLE);
}

void LAMP::stopAlarm(){
  dawnFlag = false;
  if (mode != LAMPMODE::MODE_ALARMCLOCK) return;

  myLamp.setBrightness(myLamp.getNormalizedLampBrightness(), false, false);
  mode = (storedMode != LAMPMODE::MODE_ALARMCLOCK? storedMode : LAMPMODE::MODE_NORMAL); // возвращаем предыдущий режим
  LOG(println, F("Отключение будильника рассвет."));
  if (!ONflag) {
      effectsTimer(T_DISABLE);
      FastLED.clear();
      FastLED.show();
  }
  brightness(getNormalizedLampBrightness());
}

/*
 * запускаем режим "ДЕМО"
 */
void LAMP::startDemoMode(byte tmout)
{
  storedEffect = ((effects.getEn() == EFF_WHITE_COLOR) ? storedEffect : effects.getEn()); // сохраняем предыдущий эффект, если только это не белая лампа
  mode = LAMPMODE::MODE_DEMO;
  randomSeed(millis());
  remote_action(RA::RA_DEMO_NEXT, NULL);
  myLamp.sendStringToLamp(String(PSTR("- Demo ON -")).c_str(), CRGB::Green);
  demoTimer(T_ENABLE, tmout);
}

void LAMP::startNormalMode()
{
  mode = LAMPMODE::MODE_NORMAL;
  demoTimer(T_DISABLE);
  if (storedEffect != EFF_NONE) {    // ничего не должно происходить, включаемся на текущем :), текущий всегда определен...
    remote_action(RA::RA_EFFECT, String(storedEffect).c_str(), NULL);
  } else
  if(effects.getEn() == EFF_NONE) { // если по каким-то причинам текущий пустой, то выбираем рандомный
    remote_action(RA::RA_EFF_RAND, NULL);
  }
}
#ifdef OTA
void LAMP::startOTAUpdate()
{
  if (mode == LAMPMODE::MODE_OTA) return;
  storedMode = mode;
  mode = LAMPMODE::MODE_OTA;

  effects.moveBy(EFF_MATRIX); // принудительное включение режима "Матрица" для индикации перехода в режим обновления по воздуху
  FastLED.clear();
  changePower(true);
  sendStringToLamp(String(PSTR("- OTA UPDATE ON -")).c_str(), CRGB::Green);
  otaManager.startOtaUpdate();
}
#endif
bool LAMP::fillStringManual(const char* text,  const CRGB &letterColor, bool stopText, bool isInverse, int32_t pos, int8_t letSpace, int8_t txtOffset, int8_t letWidth, int8_t letHeight)
{
  static int32_t offset = (MIRR_V ? 0 : WIDTH);

  if(pos)
    offset = (MIRR_V ? 0 + pos : WIDTH - pos);

  if (!text || !strlen(text))
  {
    return true;
  }

  uint16_t i = 0, j = 0;
  while (text[i] != '\0')
  {
    if ((uint8_t)text[i] > 191)                           // работаем с русскими буквами
    {
      i++;
    }
    else
    {
      if(!MIRR_V)
        drawLetter(text[i], offset + (int16_t)j * (letWidth + letSpace), letterColor, letSpace, txtOffset, isInverse, letWidth, letHeight);
      else
        drawLetter(text[i], offset - (int16_t)j * (letWidth + letSpace), letterColor, letSpace, txtOffset, isInverse, letWidth, letHeight);
      i++;
      j++;
    }
  }

  if(!stopText)
    (MIRR_V ? offset++ : offset--);
  if ((!MIRR_V && offset < (int32_t)(-j * (letWidth + letSpace))) || (MIRR_V && offset > (int32_t)(j * (letWidth + letSpace))+(signed)WIDTH))       // строка убежала
  {
    offset = (MIRR_V ? 0 : WIDTH);
    return true;
  }
  if(pos) // если задана позиция, то считаем что уже отобразили
  {
    offset = (MIRR_V ? 0 : WIDTH);
    return true;
  }

  return false;
}

void LAMP::drawLetter(uint16_t letter, int16_t offset,  const CRGB &letterColor, uint8_t letSpace, int8_t txtOffset, bool isInverse, int8_t letWidth, int8_t letHeight)
{
  uint16_t start_pos = 0, finish_pos = letWidth + letSpace;

  if (offset < (int16_t)-letWidth || offset > (int16_t)WIDTH)
  {
    return;
  }
  if (offset < 0)
  {
    start_pos = (uint16_t)-offset;
  }
  if (offset > (int16_t)(WIDTH - letWidth))
  {
    finish_pos = (uint16_t)(WIDTH - offset);
  }

  for (uint16_t i = start_pos; i < finish_pos; i++)
  {
    uint8_t thisByte;

    if((finish_pos - i <= letSpace) || ((letWidth - 1 - i)<0))
      thisByte = 0x00;
    else
    {
      thisByte = getFont(letter, i);
    }

    for (uint16_t j = 0; j < letHeight; j++)
    {
      bool thisBit = thisByte & (1 << (letHeight - 1 - j));

      // рисуем столбец (i - горизонтальная позиция, j - вертикальная)
      if (thisBit)
      {
        if(!isInverse)
          drawPixelXY(offset + i, txtOffset + j, letterColor);
        else
          setLedsfadeToBlackBy(getPixelNumber(offset + i, txtOffset + j), FADETOBLACKVALUE);
          //drawPixelXY(offset + i, txtOffset + j, (isInverse ? CRGB::Black : letterColor));
      }
      else
      {
        if(isInverse)
          drawPixelXY(offset + i, txtOffset + j, letterColor);
        else
          setLedsfadeToBlackBy(getPixelNumber(offset + i, txtOffset + j), FADETOBLACKVALUE);
          //drawPixelXY(offset + i, txtOffset + j, (isInverse ? letterColor : CRGB::Black));
      }
    }
  }
}

uint8_t LAMP::getFont(uint8_t asciiCode, uint8_t row)       // интерпретатор кода символа в массиве fontHEX (для Arduino IDE 1.8.* и выше)
{
  asciiCode = asciiCode - '0' + 16;                         // перевод код символа из таблицы ASCII в номер согласно нумерации массива

  if (asciiCode <= 90)                                      // печатаемые символы и английские буквы
  {
    return pgm_read_byte(&fontHEX[asciiCode][row]);
  }
  else if (asciiCode >= 112 && asciiCode <= 159)
  {
    return pgm_read_byte(&fontHEX[asciiCode - 17][row]);
  }
  else if (asciiCode >= 96 && asciiCode <= 111)
  {
    return pgm_read_byte(&fontHEX[asciiCode + 47][row]);
  }

  return 0;
}

void LAMP::sendString(const char* text, const CRGB &letterColor){
  if (!isLampOn()){
      disableEffectsUntilText(); // будем выводить текст, при выкюченной матрице
      setOffAfterText();
      changePower(true);
      setBrightness(1, false, false); // выводить будем минимальной яркостью getNormalizedLampBrightness()
      sendStringToLamp(text, letterColor, true);
  } else {
      sendStringToLamp(text, letterColor);
  }
}

void LAMP::sendStringToLamp(const char* text, const CRGB &letterColor, bool forcePrint, int8_t textOffset, int16_t fixedPos)
{
  if((!ONflag && !forcePrint) || (isAlarm() && !forcePrint)) return; // если выключена, или если будильник, но не задан принудительный вывод - то на выход
  if(textOffset==-128) textOffset=this->txtOffset;

  if(text==nullptr){ // текст пустой
    if(!isStringPrinting){ // ничего сейчас не печатается
      if(docArrMessages.isNull()){ // массив пустой
        return; // на выход
      }
      else { // есть что печатать
        JsonArray arr = docArrMessages.as<JsonArray>(); // используем имеющийся
        JsonObject var=arr[0]; // извлекаем очередной
        doPrintStringToLamp(var[F("s")], (var[F("c")].as<unsigned long>()), (var[F("o")].as<int>()), (var[F("f")].as<int>())); // отправляем
        arr.remove(0); // удаляем отправленный
      }
    } else {
        // текст на входе пустой, идет печать
        return; // на выход
    }
  } else { // текст не пустой
    if(!isStringPrinting){ // ничего сейчас не печатается
      doPrintStringToLamp(text, letterColor, textOffset, fixedPos); // отправляем
    } else { // идет печать, помещаем в очередь
      JsonArray arr; // добавляем в очередь

      if(!docArrMessages.isNull())
        arr = docArrMessages.as<JsonArray>(); // используем имеющийся
      else
        arr = docArrMessages.to<JsonArray>(); // создаем новый

      JsonObject var = arr.createNestedObject();
      var[F("s")]=text;
      var[F("c")]=((unsigned long)letterColor.r<<16)+((unsigned long)letterColor.g<<8)+(unsigned long)letterColor.b;
      var[F("o")]=textOffset;
      var[F("f")]=fixedPos;

      LOG(println, docArrMessages.as<String>());

      String tmp; // Тут шаманство, чтобы не ломало JSON
      serializeJson(docArrMessages, tmp);
      deserializeJson(docArrMessages, tmp);
    }
  }
}

void LAMP::doPrintStringToLamp(const char* text,  const CRGB &letterColor, const int8_t textOffset, const int16_t fixedPos)
{
  static String toPrint;
  static CRGB _letterColor;

  isStringPrinting = true;
  int8_t offs=(textOffset==-128?txtOffset:textOffset);

  if(text!=nullptr && text[0]!='\0'){
    toPrint.concat(text);
    toPrint.replace(F("%TM"), timeProcessor.getFormattedShortTime());
    toPrint.replace(F("%IP"), WiFi.localIP().toString());
    _letterColor = letterColor;
  }

  if(toPrint.length()==0) {
    isStringPrinting = false;
    return; // нечего печатать
  } else {
    isStringPrinting = true;
  }

  if(tmStringStepTime.isReadyManual()){
    if(!fillStringManual(toPrint.c_str(), _letterColor, false, isAlarm(), fixedPos, (fixedPos? 0 : LET_SPACE), offs)){ // смещаем
      tmStringStepTime.reset();
    }
    else {
      isStringPrinting = false;
      toPrint.clear(); // все напечатали
      sendStringToLamp(); // получаем новую порцию
    }
  } else {
    fillStringManual(toPrint.c_str(), _letterColor, true, isAlarm(), fixedPos, (fixedPos? 0 : LET_SPACE), offs);
  }
}

void LAMP::newYearMessageHandle()
{
  if(!tmNewYearMessage.isReady() || timeProcessor.isDirtyTime())
    return;

  {
    char strMessage[256]; // буффер
    time_t calc = NEWYEAR_UNIXDATETIME - timeProcessor.getUnixTime(); // тут забит гвоздями 2020 год, не работоспособно

    if(calc<0) {
      sprintf_P(strMessage, NY_MDG_STRING2, localtime(TimeProcessor::now())->tm_year);
    } else if(calc<300){
      sprintf_P(strMessage, NY_MDG_STRING1, (int)calc, PSTR("секунд"));
    } else if(calc/60<60){
      sprintf_P(strMessage, NY_MDG_STRING1, (int)(calc/60), PSTR("минут"));
    } else if(calc/(60*60)<60){
      sprintf_P(strMessage, NY_MDG_STRING1, (int)(calc/(60*60)), PSTR("часов"));
    } else {
      byte calcN=(int)(calc/(60*60*24))%10; // остаток от деления на 10
      String str;
      if(calcN>=2 && calcN<=4)
        str = F("дня");
      else if(calc!=11 && calcN==1)
        str = F("день");
      else
        str = F("дней");
      sprintf_P(strMessage, NY_MDG_STRING1, (int)(calc/(60*60*24)), str.c_str());
    }

    LOG(printf_P, PSTR("Prepared message: %s\n"), strMessage);
    sendStringToLamp(strMessage, LETTER_COLOR);
  }
}

void LAMP::periodicTimeHandle()
{
  const tm* t = localtime(timeProcessor.now());
  //LOG(println, tm);
  if(! t->tm_sec )
    return;

  time_t tm = t->tm_hour * 60 + t->tm_min;

  switch (enPeriodicTimePrint)
  {
    case PERIODICTIME::PT_EVERY_1:
      if(tm%60)
        sendStringToLamp(timeProcessor.getFormattedShortTime().c_str(), CRGB::Blue);
      break;
    case PERIODICTIME::PT_EVERY_5:
      if(!(tm%5) && tm%60)
        sendStringToLamp(timeProcessor.getFormattedShortTime().c_str(), CRGB::Blue);
      break;
    case PERIODICTIME::PT_EVERY_10:
      if(!(tm%10) && tm%60)
        sendStringToLamp(timeProcessor.getFormattedShortTime().c_str(), CRGB::Blue);
      break;
    case PERIODICTIME::PT_EVERY_15:
      if(!(tm%15) && tm%60)
        sendStringToLamp(timeProcessor.getFormattedShortTime().c_str(), CRGB::Blue);
      break;
    case PERIODICTIME::PT_EVERY_30:
      if(!(tm%30) && tm%60)
        sendStringToLamp(timeProcessor.getFormattedShortTime().c_str(), CRGB::Blue);
      break;
    case PERIODICTIME::PT_EVERY_60:
      if(!(tm%60))
        sendStringToLamp(timeProcessor.getFormattedShortTime().c_str(), CRGB::Red);
      break;

    default:
      break;
  }
  if(enPeriodicTimePrint!=PERIODICTIME::PT_EVERY_60 && enPeriodicTimePrint!=PERIODICTIME::PT_NOT_SHOW && !(tm%60))
    sendStringToLamp(timeProcessor.getFormattedShortTime().c_str(), CRGB::Red);
}

#ifdef MIC_EFFECTS
void LAMP::micHandler()
{
  static uint8_t counter=0;

  if(mw==nullptr && !isCalibrationRequest){ // обычный режим
    //if(millis()%1000) return; // отладка
    mw = new MICWORKER(mic_scale,mic_noise);
    samp_freq = mw->process(noise_reduce); // возвращаемое значение - частота семплирования
    last_min_peak = mw->getMinPeak();
    last_max_peak = mw->getMaxPeak();

    if(!counter) // раз на N измерений берем частоту, т.к. это требует обсчетов
      last_freq = mw->analyse(); // возвращаемое значение - частота главной гармоники
    if(micAnalyseDivider)
      counter = (counter+1)%(0x01<<(micAnalyseDivider-1)); // как часто выполнять анализ
    else
      counter = 1; // при micAnalyseDivider == 0 - отключено

    // EVERY_N_SECONDS(1){
    //   LOG(println, counter);
    // }

    //LOG(println, last_freq);
    //mw->debug();
    delete mw;
    mw = nullptr;
  } else {
    if(mw==nullptr){ // калибровка начало
      mw = new MICWORKER();
      mw->calibrate();
    } else { // калибровка продолжение
      mw->calibrate();
    }
    if(!mw->isCaliblation()){ // калибровка конец
      mic_noise = mw->getNoise();
      mic_scale = mw->getScale();
      isCalibrationRequest = false; // завершили
      delete mw;
      mw = nullptr;

      //iGLOBAL.isMicCal = false;
      remote_action(RA::RA_MIC, NULL);
    }
  }
}
#endif

void LAMP::fadelight(const uint8_t _targetbrightness, const uint32_t _duration, std::function<void(void)> callback) {
    LOG(printf, PSTR("Fading to: %d\n"), _targetbrightness);
    _fadeTicker.detach();

    uint8_t _maxsteps = _duration / FADE_STEPTIME;
    _brt = getBrightness();
    uint8_t _brtdiff = abs(_targetbrightness - _brt);

    if (_brtdiff > FADE_MININCREMENT * _maxsteps) {
        _steps = _maxsteps;
    } else {
        _steps = _brtdiff/FADE_MININCREMENT;
    }

    if (_steps < 3) {
        brightness(_targetbrightness);
        if (callback != nullptr) _fadeTicker.once_ms_scheduled(0, callback);
        return;
    }

    _brtincrement = (_targetbrightness - _brt) / _steps;

    //_SPTO(Serial.printf_P, F_fadeinfo, _brt, _targetbrightness, _steps, _brtincrement)); _SPLN("");
    _fadeTicker.attach_ms(FADE_STEPTIME, std::bind(&LAMP::fader, this, _targetbrightness, callback));
}

/*
 * Change global brightness with or without fade effect
 * fade applied in non-blocking way
 * FastLED dim8 function applied internaly for natural brightness controll
 * @param uint8_t _brt - target brigtness level 0-255
 * @param bool fade - use fade effect on brightness change
 */
void LAMP::setBrightness(const uint8_t _brt, const bool fade, const bool natural){
    LOG(printf_P, PSTR("Set brightness: %u\n"), _brt);
    if (fade) {
        fadelight(_brt);
    } else {
        brightness(_brt, natural);
    }
}

/*
 * Get current brightness
 * FastLED brighten8 function applied internaly for natural brightness compensation
 * @param bool natural - return compensated or absolute brightness
 */
uint8_t LAMP::getBrightness(const bool natural){
    return (natural ? brighten8_raw(FastLED.getBrightness()) : FastLED.getBrightness());
}


/*
 * Set global brightness
 * @param bool natural
 */
void LAMP::brightness(const uint8_t _brt, bool natural){
    uint8_t _cur = natural ? brighten8_video(FastLED.getBrightness()) : FastLED.getBrightness();
    if ( _cur == _brt) return;

    if (_brt) {
      FastLED.setBrightness(natural ? dim8_video(_brt) : _brt);
    } else {
      FastLED.setBrightness(0); // полностью гасим лапу если нужна 0-я яркость
      FastLED.show();
    }
}

/*
 * Fade light callback
 * @param uint8_t _tgtbrt - last step brightness
 */
void LAMP::fader(const uint8_t _tgtbrt, std::function<void(void)> callback){
  --_steps;
  if (! _steps) {   // on last step
      if (callback != nullptr) {
        _fadeTicker.once_ms_scheduled(0, callback);
      } else { _fadeTicker.detach(); }
      _brt = _tgtbrt;
  } else {
      _brt += _brtincrement;
  }

  brightness(_brt);
}

/*
 * переключатель эффектов для других методов,
 * может использовать фейдер, выбирать случайный эффект для демо
 * @param EFFSWITCH action - вид переключения (пред, след, случ.)
 * @param fade - переключаться через фейдер или сразу
 */
void LAMP::switcheffect(EFFSWITCH action, bool fade, EFF_ENUM effnb, bool skip) {
  switcheffectIdx(action, fade, effects.getBy(effnb), skip);
}

void LAMP::switcheffectIdx(EFFSWITCH action, bool fade, int idx, bool skip) {
  LOG(printf_P, PSTR("EFFSWITCH=%d, fade=%d, idx=%d\n"), action, fade, idx);

  if (!skip) {
    switch (action) {
    case EFFSWITCH::SW_NEXT :
        effects.setSelected(effects.getNext());
        break;
    case EFFSWITCH::SW_NEXT_DEMO :
        effects.setSelected(effects.getBy(1));
        break;
    case EFFSWITCH::SW_PREV :
        effects.setSelected(effects.getPrev());
        break;
    case EFFSWITCH::SW_SPECIFIC :
        effects.setSelected(effects.getByIdx(idx));
        break;
    case EFFSWITCH::SW_RND :
        effects.setSelected(effects.getBy(random(0, effects.getModeAmount())));
        break;
    case EFFSWITCH::SW_WHITE_HI:
        effects.setSelected(effects.getBy(EFF_WHITE_COLOR));
        break;
    case EFFSWITCH::SW_WHITE_LO:
        effects.setSelected(effects.getBy(EFF_WHITE_COLOR));
        break;
    default:
        return;
    }
    // тухнем "вниз" только на включенной лампе
    if (fade && ONflag) {
      fadelight(FADE_MINCHANGEBRT, FADE_TIME, std::bind(&LAMP::switcheffectIdx, this, action, fade, idx, true));
      return;
    }
  }

  changePower(true);  // любой запрос на смену эффекта автоматом включает лампу
  effects.moveSelected();

  bool natural = true;
  switch (action) {
  case EFFSWITCH::SW_WHITE_HI:
      setLampBrightness(255); // здесь яркость ползунка в UI, т.е. ставим 255 в самое крайнее положение, а дальше уже будет браться приведенная к BRIGHTNESS яркость
      natural = false;
      break;
  case EFFSWITCH::SW_WHITE_LO:
      setLampBrightness(1); // здесь яркость ползунка в UI, т.е. ставим 1 в самое крайнее положение, а дальше уже будет браться приведенная к BRIGHTNESS яркость
      fade = natural = false;
      break;
  default:;
  }

  // отрисовать текущий эффект
  effects.worker->run(getUnsafeLedsArray(), effects.getCurrent());
  setBrightness(getNormalizedLampBrightness(), fade, natural);
}

/*
 * включает/выключает режим "демо"
 * @param SCHEDULER enable/disable/reset - вкл/выкл/сброс
 */
void LAMP::demoTimer(SCHEDULER action, byte tmout){
//  LOG.printf_P(PSTR("demoTimer: %u\n"), action);
  switch (action)
  {
  case SCHEDULER::T_DISABLE :
    _demoTicker.detach();
    break;
  case SCHEDULER::T_ENABLE :
    _demoTicker.attach_scheduled(tmout, std::bind(&remote_action, RA::RA_DEMO_NEXT, NULL));
    break;
  case SCHEDULER::T_RESET :
    if (isAlarm()) stopAlarm(); // тут же сбросим и будильник
    if (_demoTicker.active() ) demoTimer(T_ENABLE);
    break;
  default:
    return;
  }
}

/*
 * включает/выключает таймер обработки эффектов
 * @param SCHEDULER enable/disable/reset - вкл/выкл/сброс
 */
void LAMP::effectsTimer(SCHEDULER action) {
//  LOG.printf_P(PSTR("effectsTimer: %u\n"), action);
  switch (action)
  {
  case SCHEDULER::T_DISABLE :
    _effectsTicker.detach();
    break;
  case SCHEDULER::T_ENABLE :
    _effectsTicker.once_ms_scheduled(EFFECTS_RUN_TIMER, std::bind(&LAMP::effectsTick, this));
    break;
  case SCHEDULER::T_RESET :
    if (_effectsTicker.active() ) effectsTimer(T_ENABLE);
    break;
  default:
    return;
  }
}

//-----------------------------
// ------------- мигающий цвет (не эффект! используется для отображения краткосрочного предупреждения; блокирующий код!) -------------
void LAMP::showWarning(
  CRGB::HTMLColorCode color,                                               /* цвет вспышки                                                 */
  uint32_t duration,                                        /* продолжительность отображения предупреждения (общее время)   */
  uint16_t blinkHalfPeriod)                                 /* продолжительность одной вспышки в миллисекундах (полупериод) */
{
  uint32_t blinkTimer = millis();
  enum BlinkState { OFF = 0, ON = 1 } blinkState = BlinkState::OFF;
  myLamp.fadelight(myLamp.getLampBrightness());    // установка яркости для предупреждения
  FastLED.clear();
  delay(2);
  FastLED.show();

  for (uint16_t i = 0U; i < NUM_LEDS; i++)                  // установка цвета всех диодов в WARNING_COLOR
  {
    myLamp.setLeds(i, color);
  }

  uint32_t startTime = millis();
  while (millis() - startTime <= (duration + 5))            // блокировка дальнейшего выполнения циклом на время отображения предупреждения
  {
    if (millis() - blinkTimer >= blinkHalfPeriod)           // переключение вспышка/темнота
    {
      blinkTimer = millis();
      blinkState = (BlinkState)!blinkState;
      myLamp.brightness(blinkState == BlinkState::OFF ? 0 : myLamp.getLampBrightness());
      delay(1);
      FastLED.show();
    }
    delay(50);
  }

  FastLED.clear();
  myLamp.fadelight(myLamp.isLampOn() ? myLamp.getLampBrightness() : 0);  // установка яркости, которая была выставлена до вызова предупреждения
  delay(1);
  FastLED.show();
  // наверное это не актуально
  //myLamp.setLoading();                                       // принудительное отображение текущего эффекта (того, что был активен перед предупреждением)
}
//-----------------------------
