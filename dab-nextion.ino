#include "Nextion.h"
#include "DABShield.h"

// Use bit-bang SPI code
#define DAB_SPI_BITBANG

// Maximum volume setting
#define MAXVOLUME 63

// How many milliseconds between timer servicing
#define TICKTIME 100

// Number of timers
#define MAXTIMERS 3

// Timer ID numbers
#define TIMER_RSSI 0
#define TIMER_FREQ 1
#define TIMER_INFO 2

// Default ensemble frequency ID (29 = 229.648 = BBC National)
#define DEFAULTFREQ 29

// Default volume setting
#define DEFAULTVOL MAXVOLUME

typedef struct
{
  boolean active;
  unsigned long int ticks;
  unsigned long int initTicks;
  void (*handlerFn)(void);
} timer_t;

typedef struct
{
    timer_t timers[MAXTIMERS];
    unsigned long int lastTime;
} sysTimers_t;

NexText tEnsemble = NexText(0, 10, "ensemble");
NexText tTime = NexText(0, 4, "time");
NexText tRssi = NexText(0, 6, "rssi");
NexText tSnr = NexText(0, 7, "snr");

NexText t1Ensemble = NexText(1, 2, "ensemble1");
NexText t1Freq = NexText(1, 3, "freq1");
NexText t1PgmText = NexText(1, 12, "pgmtext1");

NexButton bFreq = NexButton(0, 8, "frequency");
NexButton bService = NexButton(0, 9, "service");
NexButton bVolDown = NexButton(0, 3, "b2");
NexButton bVolUp = NexButton(0, 5, "b3");
NexButton bInfo = NexButton(0, 2, "info");
NexButton bBack = NexButton(1, 1, "back");

sysTimers_t sysTimers;

DAB Dab;

uint8_t freqIndex;
uint8_t serviceId;
DABTime dabTime;
uint8_t vol;
boolean changingFreq;
unsigned long int lastServiceData;
uint8_t activePage;
uint8_t pgmTextPtr;
unsigned long int pgmTextCount;

char pgmText[DAB_MAX_SERVICEDATA_LEN];

//SPI Ports for BIT
const byte slaveSelectPin = 8;

const byte SCKPin = 13;
const byte MISOPin = 12;
const byte MOSIPin = 11;

NexTouch *nexListenList[] =
{
  &bFreq,
  &bService,
  &bVolDown,
  &bVolUp,
  &bInfo,
  &bBack,
  NULL
};

char freqStr[16];

char *monthNames[] =
{
  "Jan",
  "Feb",
  "Mar",
  "Apr",
  "May",
  "Jun",
  "Jul",
  "Aug",
  "Sep",
  "Oct",
  "Nov",
  "Dec"
};

void initSysTimers()
{
  int c;
  
  sysTimers.lastTime = millis();
  for(c = 0; c < MAXTIMERS; c++)
  {
    sysTimers.timers[c].active = false;
  }
}

void initTimer(int timerId, void (*handlerFn)(void))
{
  timer_t *tPtr;

  tPtr = &sysTimers.timers[timerId];
  tPtr -> active = false;
  tPtr -> handlerFn = handlerFn;
}

void startTimer(int timerId, unsigned long int maxTime)
{
  timer_t *tPtr;

  tPtr = &sysTimers.timers[timerId];
  tPtr -> active = true;
  tPtr -> ticks = maxTime / TICKTIME;
  tPtr -> initTicks = tPtr -> ticks;
}

void stopTimer(int timerId)
{
  sysTimers.timers[timerId].active = false;
}

void timerTask()
{
  unsigned long int timeNow;
  int c;
  timer_t *tPtr;
  
  timeNow = millis();
  if((timeNow - sysTimers.lastTime) > TICKTIME)
  {
    sysTimers.lastTime = timeNow;
    for(c = 0; c < MAXTIMERS; c++)
    {
      tPtr = &sysTimers.timers[c];
      if(tPtr -> active == true)
      {
        if(tPtr -> ticks)
        {
          tPtr -> ticks--;
        }
        else
        {
          tPtr -> ticks = tPtr -> initTicks;
          tPtr -> handlerFn();
        }
      }
    }
  }
}

void updateFreqLabel()
{
  uint32_t fKhz;
  
  // Update button label
  fKhz = Dab.freq_khz(freqIndex);
  sprintf(freqStr, "%03d.%03d MHz", (uint16_t)(fKhz / 1000), (uint16_t)(fKhz % 1000));
  bFreq.setText(freqStr);  
}

void buttonVolUp(void *ptr)
{
  if(vol < MAXVOLUME)
  {
    vol++;
    Dab.vol(vol);
  }
}

void buttonVolDown(void *ptr)
{
  if(vol > 0)
  {
    vol--;
    Dab.vol(vol);
  }
}

void buttonFrequency(void *ptr)
{
  changingFreq = true;
      
  freqIndex++;
  if(freqIndex == DAB_FREQS)
  {
    freqIndex = 0;
  }

  updateFreqLabel();

  startTimer(TIMER_FREQ, 2000);
  
  bService.setText("Tuning");
  tEnsemble.setText("");
  tRssi.setText("");
  tSnr.setText("");
}

void setService()
{
    Dab.set_service(serviceId);
    bService.setText(Dab.service[serviceId].Label);
}

void updateInfoScreen()
{
  if(Dab.valid == true)
  {
    t1Ensemble.setText(Dab.Ensemble);
    t1Freq.setText(freqStr);
  }
  else
  {
    t1Ensemble.setText("");
  }
}

void buttonService(void *ptr)
{ 
  if(changingFreq == false)
  {
    serviceId++;
    if(serviceId == DAB_MAX_SERVICES)
    {
      serviceId = 0;
    }
    setService();
  }
}

void buttonInfo(void *ptr)
{
  activePage = 1;
  pgmTextPtr = 0;
  updateInfoScreen();
  startTimer(TIMER_INFO, 1000);
}

void buttonBack(void *ptr)
{
  activePage = 0;
  stopTimer(TIMER_INFO);
}

void setFrequency()
{
  changingFreq = false;
  
  stopTimer(TIMER_FREQ);
  
  bService.setText("");
  tRssi.setText("");
  tSnr.setText("");

  // Tune
  Dab.tune(freqIndex);

  // Select service 0
  serviceId = 0;
  if(Dab.servicevalid() == true)
  {
    tEnsemble.setText(Dab.Ensemble);
    setService();
  }
  else
  {
    bService.setText("No service");
  }
}

void showSigStrength()
{
  int c;
  char strbuff[32];
  int rssi;
    
  Dab.status();

  if(Dab.valid == true)
  {
    rssi = constrain(map(Dab.signalstrength, 25, 64, 0, 28), 0, 28);

    memset(strbuff, ' ', 32);
    for(c = 0; c < rssi; c++)
    {
      strbuff[c] = '>';
    }
    strbuff[c] = '\0';

    tRssi.setText(strbuff);
    sprintf(strbuff, "%d dB", Dab.snr);
    tSnr.setText(strbuff);
  }
  else
  {
    tRssi.setText("");
    tSnr.setText("");
  }
}

void showDabTime()
{
  DABTime dabTimeNow;
  char txtBuff[16];

  Dab.time(&dabTimeNow);
  if(dabTimeNow.Minutes != dabTime.Minutes)
  {
    memcpy(&dabTime, &dabTimeNow, sizeof(dabTime));

    sprintf(txtBuff, "%02d %s   %02d:%02d", dabTime.Days, monthNames[dabTime.Months - 1], dabTime.Hours, dabTime.Minutes);
    tTime.setText(txtBuff);
  }
}

void DABSpiMsg(unsigned char *data, uint32_t len)
{
  uint32_t i;
  uint32_t l;
  unsigned char spiByte;
  
  digitalWrite(SCKPin, LOW);
  digitalWrite (slaveSelectPin, LOW);
  for(l=0; l<len; l++)
  {
    spiByte = data[l];
    for (i = 0; i < 8; i++)
    {
      digitalWrite(MOSIPin, (spiByte & 0x80) ? HIGH : LOW);
      delayMicroseconds(1);
      digitalWrite(SCKPin, HIGH);
      spiByte = (spiByte << 1) | digitalRead(MISOPin);
      digitalWrite(SCKPin, LOW);
      delayMicroseconds(1);
    }
    data[l] = spiByte;
  }
  digitalWrite (slaveSelectPin, HIGH);
}

void ServiceData()
{
  char strbuff[32];

  if(activePage == 1)
  {    
    if(strcmp(pgmText, Dab.ServiceData) != 0)
    {
      Serial.println(pgmTextCount);
      pgmTextCount = 0;
      
      strcpy(pgmText, Dab.ServiceData); 
      Serial.print("(");
      Serial.print(strlen(pgmText));
      Serial.print(") ");
      Serial.println(pgmText);

      t1PgmText.setText(strbuff);
    }
    else
    {
      pgmTextCount++;
      Serial.print(".");
    }
  }
}

void setup()
{  
  Serial.begin(9600); 
  Serial.println("Ed's DAB receiver");
   
  nexInit(38400);

  bFreq.attachPop(buttonFrequency, &bFreq);
  bService.attachPop(buttonService, &bService);
  bVolUp.attachPop(buttonVolUp, &bVolUp);
  bVolDown.attachPop(buttonVolDown, &bVolDown);
  bInfo.attachPop(buttonInfo, &bInfo);
  bBack.attachPop(buttonBack, &bBack);

  tEnsemble.setText("");
  tTime.setText("");
  tRssi.setText("");
  tSnr.setText("");
  bVolDown.setText("Vol down");
  bVolUp.setText("Vol up");

  serviceId = 0;
  freqIndex = DEFAULTFREQ;
  changingFreq = false;
  vol = DEFAULTVOL;
  activePage = 0;
  pgmText[0] = '\0';
  pgmTextCount = 0;

  initSysTimers();
  initTimer(TIMER_RSSI, showSigStrength);
  initTimer(TIMER_FREQ, setFrequency);
  initTimer(TIMER_INFO, updateInfoScreen);
  startTimer(TIMER_RSSI, 1000);

  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(slaveSelectPin, OUTPUT);
  pinMode(SCKPin, OUTPUT);
  pinMode(MOSIPin, OUTPUT);
  pinMode(MISOPin, INPUT_PULLUP);
  digitalWrite(slaveSelectPin, HIGH);

  Dab.setCallback(ServiceData);
  Dab.begin(0);
  if(Dab.error != 0)
  {
    tRssi.setText("DAB init error");
  }
  
  lastServiceData = millis();

  setFrequency();
  updateFreqLabel();

  Dab.vol(vol);
}

void loop()
{
  Dab.task();
  
  nexLoop(nexListenList);

  timerTask();

  if(activePage == 0)
  {
    showDabTime();
  }
}
