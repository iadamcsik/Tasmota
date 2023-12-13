/* xsns_128_TCS34725.ino support for TCS34725 ambient light sensor

  Copyright (C) 2018  Theo Arends , Gerhard Mutz and Adafruit

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_I2C
#ifdef USE_TCS34725

#define XSNS_128       128
#define XI2C_80        80  // See I2CDEVICES.md

#include <Wire.h>
#include <Adafruit_TCS34725.h>
// about 2,2 k flash

//
// An experimental wrapper class that implements the improved lux and color temperature from
// TAOS and a basic autorange mechanism.
//
// Written by ductsoup, public domain
//

// RGB Color Sensor with IR filter and White LED - TCS34725
// I2C 7-bit address 0x29, 8-bit address 0x52
//
// http://www.adafruit.com/product/1334
// http://learn.adafruit.com/adafruit-color-sensors/overview
// http://www.adafruit.com/datasheets/TCS34725.pdf
// http://www.ams.com/eng/Products/Light-Sensors/Color-Sensor/TCS34725
// http://www.ams.com/eng/content/view/download/265215 <- DN40, calculations
// http://www.ams.com/eng/content/view/download/181895 <- DN39, some thoughts on autogain
// http://www.ams.com/eng/content/view/download/145158 <- DN25 (original Adafruit calculations)
//
// some magic numbers for this device from the DN40 application note
#define TCS34725_R_Coef 0.136
#define TCS34725_G_Coef 1.000
#define TCS34725_B_Coef -0.444
#define TCS34725_GA 1.0
#define TCS34725_DF 310.0
#define TCS34725_CT_Coef 3810.0
#define TCS34725_CT_Offset 1391.0

// Autorange class for TCS34725
class tcs34725 {
public:
  tcs34725(void);

  boolean begin(void);
  void getData(void);
  void getRawData_noDelay(uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *c);
  boolean isAvailable, isSaturated;
  uint16_t againx, atime, atime_ms;
  uint16_t r, g, b, c, h, s, v;
  uint16_t ir;
  uint16_t r_comp, g_comp, b_comp, c_comp;
  uint16_t saturation, saturation75;
  float cratio, cpl, ct, lux, maxlux;

private:
  struct tcs_agc {
    tcs34725Gain_t ag;
    uint8_t at;
    uint16_t mincnt;
    uint16_t maxcnt;
  };
  static const tcs_agc agc_lst[];
  uint16_t agc_cur;

  void setGainTime(void);
  Adafruit_TCS34725 tcs;
};
//
// Gain/time combinations to use and the min/max limits for hysteresis
// that avoid saturation. They should be in order from dim to bright.
//
// Also set the first min count and the last max count to 0 to indicate
// the start and end of the list.
//
const tcs34725::tcs_agc tcs34725::agc_lst[] = {
  { TCS34725_GAIN_60X, TCS34725_INTEGRATIONTIME_614MS,     0, 20000 },
  { TCS34725_GAIN_60X, TCS34725_INTEGRATIONTIME_154MS,  4990, 63000 },
  { TCS34725_GAIN_16X, TCS34725_INTEGRATIONTIME_154MS, 16790, 63000 },
  { TCS34725_GAIN_4X,  TCS34725_INTEGRATIONTIME_154MS, 15740, 63000 },
  { TCS34725_GAIN_1X,  TCS34725_INTEGRATIONTIME_154MS, 15740, 0 }
};
tcs34725::tcs34725() : agc_cur(0), isAvailable(0), isSaturated(0) {
}

// initialize the sensor
boolean tcs34725::begin(void) {
  AddLog(LOG_LEVEL_INFO, PSTR("TCS34725 init started"));
  tcs = Adafruit_TCS34725(agc_lst[agc_cur].at, agc_lst[agc_cur].ag);
  if ((isAvailable = tcs.begin()))
    setGainTime();
  tcs.setInterrupt(true);
  return(isAvailable);
}

// Set the gain and integration time
void tcs34725::setGainTime(void) {
  tcs.setGain(agc_lst[agc_cur].ag);
  tcs.setIntegrationTime(agc_lst[agc_cur].at);
  atime = int(agc_lst[agc_cur].at);
  atime_ms = ((256 - atime) * 2.4);
  switch(agc_lst[agc_cur].ag) {
  case TCS34725_GAIN_1X:
    againx = 1;
    break;
  case TCS34725_GAIN_4X:
    againx = 4;
    break;
  case TCS34725_GAIN_16X:
    againx = 16;
    break;
  case TCS34725_GAIN_60X:
    againx = 60;
    break;
  }
}


void tcs34725::getRawData_noDelay(uint16_t *r, uint16_t *g, uint16_t *b, uint16_t *c)
{
  *c = tcs.read16(TCS34725_CDATAL);
  *r = tcs.read16(TCS34725_RDATAL);
  *g = tcs.read16(TCS34725_GDATAL);
  *b = tcs.read16(TCS34725_BDATAL);
}

// Retrieve data from the sensor and do the calculations
void tcs34725::getData(void) {
  // read the sensor and autorange if necessary
  tcs.getRawData(&r, &g, &b, &c);
  //getRawData_noDelay(&r, &g, &b, &c);
  while(1) {
    if (agc_lst[agc_cur].maxcnt && c > agc_lst[agc_cur].maxcnt)
      agc_cur++;
    else if (agc_lst[agc_cur].mincnt && c < agc_lst[agc_cur].mincnt)
      agc_cur--;
    else break;

    setGainTime();
    delay((256 - atime) * 2.4 * 2); // shock absorber
    tcs.getRawData(&r, &g, &b, &c);
    //getRawData_noDelay(&r, &g, &b, &c);
    break;
  }

  // DN40 calculations
  ir = (r + g + b > c) ? (r + g + b - c) / 2 : 0;
  r_comp = r - ir;
  g_comp = g - ir;
  b_comp = b - ir;
  c_comp = c - ir;
  cratio = float(ir) / float(c);

  saturation = ((256 - atime) > 63) ? 65535 : 1024 * (256 - atime);
  saturation75 = (atime_ms < 150) ? (saturation - saturation / 4) : saturation;
  isSaturated = (atime_ms < 150 && c > saturation75) ? 1 : 0;
  cpl = (atime_ms * againx) / (TCS34725_GA * TCS34725_DF);
  maxlux = 65535 / (cpl * 3);

  lux = (TCS34725_R_Coef * float(r_comp) + TCS34725_G_Coef * float(g_comp) + TCS34725_B_Coef * float(b_comp)) / cpl;
  ct = TCS34725_CT_Coef * float(b_comp) / float(r_comp) + TCS34725_CT_Offset;

  // HSV calc

  float rt, gt, bt;
  if (c_comp > 0) {
    rt = float(r_comp) / float(c_comp);
    gt = float(g_comp) / float(c_comp);
    bt = float(b_comp) / float(c_comp);
  } else {
    rt = gt = bt = 0;
  }
  float c_max = std::max({rt, gt, bt});
  float c_min = std::min({rt, gt, bt});
  float delta = c_max - c_min;

  if (c_max == rt) {
    float h_tmp = 60 * fmodf((gt - bt) / delta, 6.0);
    h = (h_tmp < 0) ? (360 + h_tmp) : h_tmp;
  } else if (c_max == gt) {
    h = 60 * ((bt - rt) / delta + 2);
  } else if (c_max == bt) {
    h = 60 * ((rt - gt) / delta + 4);
  } else {
    h = 0;
  }
  
}

tcs34725 rgb_sensor;
uint8_t TCS34725_ready;

void TCS34725_Detect() {
  AddLog(LOG_LEVEL_INFO, PSTR("detecting TCS34725"));
  if (!I2cSetDevice(TCS34725_ADDRESS)) {
    return;
  }
  if (rgb_sensor.begin()==true) {
    TCS34725_ready=1;
    I2cSetActiveFound(TCS34725_ADDRESS, "TCS34725");
  } else {
    // error
    AddLog(LOG_LEVEL_INFO, PSTR("TCS34725 init error"));
  }
}


void TCS34725_EverySecond() {
  if (TCS34725_ready) {
    rgb_sensor.getData();
  }
}


#define D_LUX "Lux"
#define D_COLOR_TEMP "ColorTemp"
#define D_RED "red"
#define D_GREEN "green"
#define D_BLUE "blue"
#define D_AMBIENT "ambient"

#ifdef USE_WEBSERVER
const char HTTP_SNS_TCS34725[] PROGMEM =
 "{s}TCS34725 " D_LUX "{m}%d " D_LUX "{e}"
 "{s}TCS34725 " D_COLOR_TEMP "{m}%d " D_UNIT_KELVIN "{e}"
 "{s}TCS34725 " D_RED "{m}%d " "{e}"
 "{s}TCS34725 " D_GREEN "{m}%d " "{e}"
 "{s}TCS34725 " D_BLUE "{m}%d " "{e}"
 "{s}TCS34725 " D_AMBIENT "{m}%d " "{e}"
 "{s}TCS34725 " D_HUE "{m}%d " "{e}";
#endif  // USE_WEBSERVER


const char JSON_TCS34725[] PROGMEM = ",\"TCS34725\":{\"" D_LUX "\":%d,\"" D_COLOR_TEMP "\":%d,\"R\":%d,\"G\":%d,\"B\":%d,\"C\":%d,\"H\":%d}";


void TCS34725_Show(boolean json)
{
  if (!TCS34725_ready) {
    return;
  }
  if (json) {
    ResponseAppend_P(JSON_TCS34725,(uint32_t)rgb_sensor.lux,(uint32_t)rgb_sensor.ct,(uint32_t)rgb_sensor.r,(uint32_t)rgb_sensor.g,(uint32_t)rgb_sensor.b,(uint32_t)rgb_sensor.c,(uint32_t)rgb_sensor.h);
#ifdef USE_WEBSERVER
  } else
  {
    WSContentSend_PD(HTTP_SNS_TCS34725, (uint32_t)rgb_sensor.lux, (uint32_t)rgb_sensor.ct, (uint32_t)rgb_sensor.r, (uint32_t)rgb_sensor.g, (uint32_t)rgb_sensor.b, (uint32_t)rgb_sensor.c,(uint32_t)rgb_sensor.h);
#endif
  }
}


/*********************************************************************************************\
 * Interface
\*********************************************************************************************/



bool Xsns128(uint32_t function)
{
  bool result = false;

  if (!I2cEnabled(XI2C_80)) { return false; }

    switch (function) {
      case FUNC_INIT:
        TCS34725_Detect();
        break;
      case FUNC_EVERY_SECOND:
        TCS34725_EverySecond();
        break;
      case FUNC_JSON_APPEND:
        TCS34725_Show(1);
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
        TCS34725_Show(0);
        break;
#endif  // USE_WEBSERVER

  }
  return result;
}

#endif  // USE_TCS34725
#endif  // USE_I2C
