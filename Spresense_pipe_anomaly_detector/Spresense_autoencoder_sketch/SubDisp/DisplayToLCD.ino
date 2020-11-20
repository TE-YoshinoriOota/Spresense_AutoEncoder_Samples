#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

#define _cs  10
#define _dc   9   
#define _rst  8

Adafruit_ILI9341 tft = Adafruit_ILI9341(_cs, _dc, _rst);

/* Text position (rotated coordinate) */
#define TX 35
#define TY 210

/* Graph area */
#define GRAPH_WIDTH  (200) // Spectrum value
#define GRAPH_HEIGHT (320) // Sample step

/* Graph position */
#define GX ((240 - GRAPH_WIDTH) / 2) + 20
#define GY ((320 - GRAPH_HEIGHT) / 2)

/* NG Mark */
#define NG_X (GRAPH_WIDTH-30)
#define NG_Y (GRAPH_HEIGHT-30)
#define NG_W (20)


/* Scale */
int range = 40;

void setupLcd() {
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  tft.setCursor(TX, TY);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.println("FFT Spectrum Analyzer");
  tft.setRotation(2);
}

unsigned long showSpectrum(float *data, bool bNG) {
  static uint16_t frameBuf[GRAPH_HEIGHT][GRAPH_WIDTH];
  unsigned long calc_start;
  int val;
  int i, j;
  uint16_t colormap[] = {
    ILI9341_MAGENTA,
    ILI9341_BLUE,
    ILI9341_CYAN,
    ILI9341_GREEN,
    ILI9341_YELLOW,
    ILI9341_ORANGE,
    ILI9341_RED,
  };

  calc_start = micros();
  int maxvalue = GRAPH_WIDTH;
  int one_step = maxvalue / 7;
  int index;

  float f_max = 0.0;
  float f_min = 10000.0;
  for (int i = 0; i < GRAPH_HEIGHT; ++i) {
    if (!isnan(data[i]) && data[i] > 0.0) {
       if (data[i] > f_max) f_max = data[i];
       if (data[i] < f_min) f_min = data[i];
    } else {
      data[i] = 0.0;
    }
  }
  MPLog("max: %f min: %f\n", f_max, f_min);
  
  for (i = 0; i < GRAPH_HEIGHT; ++i) {
    val = (data[i]-f_min)/(f_max-f_min)*GRAPH_WIDTH+1;
    val = (val > GRAPH_WIDTH) ? GRAPH_WIDTH: val;
    for (j = 0; j < GRAPH_WIDTH; ++j) {
      index = j / one_step;
      if (index > 6) index = 6;
      frameBuf[i][j] = (j > val) ? ILI9341_BLACK : colormap[index];
      // when if finding error, showing red rectangle
      if (bNG) {
        if ((i > NG_Y) && (i < (NG_Y + NG_W))
         && (j > NG_X) && (j < (NG_X + NG_W)))
           frameBuf[i][j] = ILI9341_RED;
      }
    }
  }

  tft.drawRGBBitmap(GX, GY, (uint16_t*)frameBuf, GRAPH_WIDTH, GRAPH_HEIGHT);
  return micros() - calc_start;
}
