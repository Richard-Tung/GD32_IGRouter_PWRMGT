/*
    Copyright 2026 Richard Tung <dyq@dyq.me>
*/

#include <Arduino.h>

#include "gd32_flashee.h"
#include "shell.h"

#define GPIO_WAKE PA0               //Input ActiveHigh, Connect to ACC
#define GPIO_VOLT_INPUT_ADC PA1
#define GPIO_EN_ROUTER PA2          //Output ActiveHigh
#define GPIO_EN_DC PA3              //Output ActiveHigh
#define GPIO_WDT_RESET_BUTTON PA4   //Input PullUp ActiveLow
#define GPIO_WDT_INPUT PA5          //Input PullDown ActiveChange
#define GPIO_SHUTDOWN_NOTIFY PA6    //Output ActiveHigh
#define GPIO_RUN_LED PB1            //ActiveHigh

#define EE_VERSION 1

#define ADC_SET_RESOLUTION 12
#define ADC_MAX 4095

// #define ADC_VOLT_PULLUP 200     //K
// #define ADC_VOLT_PULLDOWN 20    //K
// #define ADC_VREF 3330           //mV

// #define VOLT_UVLO 11800
// #define VOLT_WAKEUP 13000

// #define TIME_WAKEUP 2           //s
// #define TIME_STARTING 30        //s
// #define TIME_SHUTDOWN 30        //s
// #define TIME_ENTER_SLEEP 10     //s
// #define TIMEOUT_WDT 60          //s
// #define TIMEOUT_UVLO 10         //s

enum ConfigID {
    CONFIG_ADC_VOLT_PULLUP,
    CONFIG_ADC_VOLT_PULLDOWN,
    CONFIG_ADC_VREF,
    CONFIG_VOLT_UVLO,
    CONFIG_VOLT_WAKEUP,
    CONFIG_TIME_WAKEUP,
    CONFIG_TIME_STARTING,
    CONFIG_TIME_SHUTDOWN,
    CONFIG_TIME_ENTER_SLEEP,
    CONFIG_TIMEOUT_WDT,
    CONFIG_TIMEOUT_UVLO,
    CONFIG_COUNT
};

static_assert(CONFIG_COUNT <= FLASH_EE_STORAGE_SIZE, "Too many config items for EEPROM");

const uint32_t config_defaults[CONFIG_COUNT]={
    200,    //CONFIG_ADC_VOLT_PULLUP
    20,     //CONFIG_ADC_VOLT_PULLDOWN
    3330,   //CONFIG_ADC_VREF
    11800,  //CONFIG_VOLT_UVLO
    13000,  //CONFIG_VOLT_WAKEUP
    2,      //CONFIG_TIME_WAKEUP
    30,     //CONFIG_TIME_STARTING
    30,     //CONFIG_TIME_SHUTDOWN
    10,     //CONFIG_TIME_ENTER_SLEEP
    60,     //CONFIG_TIMEOUT_WDT
    10,     //CONFIG_TIMEOUT_UVLO
};

const char* config_string[CONFIG_COUNT] {
    "ADC_VOLT_PULLUP",
    "ADC_VOLT_PULLDOWN",
    "ADC_VREF",
    "VOLT_UVLO",
    "VOLT_WAKEUP",
    "TIME_WAKEUP",
    "TIME_STARTING",
    "TIME_SHUTDOWN",
    "TIME_ENTER_SLEEP",
    "TIMEOUT_WDT",
    "TIMEOUT_UVLO",
};

//ee

uint32_t getConfigValue(ConfigID id)
{
    uint32_t buffer;
    if(!ee_get(id,buffer)) return config_defaults[id];
    return buffer;
}

void setConfigValue(ConfigID id,uint32_t value)
{
    ee_set(id,value);
}

void setConfigDefaults()
{
    for(int i=0;i<CONFIG_COUNT;i++)
    {
        setConfigValue((ConfigID)i,config_defaults[i]);
    }
}

void printConfig()
{
    for(int i=0;i<CONFIG_COUNT;i++)
    {
        Serial.print(config_string[i]);
        Serial.print(": ");
        Serial.println(getConfigValue( (ConfigID)i) );
    }
}

void saveConfig()
{
    ee_save();
}

enum SystemState {
    STANDBY,
    STARTING,
    RUNNING,
    SHUTTING_DOWN,
    RESTARTING
};

const char* getSystemStateString(SystemState state) 
{
    switch(state) {
        case STANDBY: return "STANDBY";
        case STARTING: return "STARTING";
        case RUNNING: return "RUNNING";
        case SHUTTING_DOWN: return "SHUTTING_DOWN";
        case RESTARTING: return "RESTARTING";
        default: return "UNKNOWN";
    }
}

SystemState sys_state = STANDBY;
uint32_t wdt_count=0;
uint32_t uvlo_count=0;
uint32_t time_count=0;

void SystemStateLoop();
void setSystemState(SystemState state);

bool LEDState = false;
void UpdateLED()
{
    LEDState=!LEDState;
    digitalWrite(GPIO_RUN_LED,LEDState ? HIGH: LOW);
}

void wdt_reset_isr()
{
    //Serial.println("WDT Reset");
    wdt_count=0;
}

volatile unsigned long resetMillis = 0;
bool config_reset_flag=false;

void reset_button_pressed()
{
    resetMillis = millis();
}

void reset_button_released()
{
    if(millis() - resetMillis >= 10000) //10s
    {
        //do reset
        config_reset_flag=true;
    }
    wdt_count=0;
}

void iwdg_setup()
{
    rcu_osci_on(RCU_IRC40K);
    while(SUCCESS != rcu_osci_stab_wait(RCU_IRC40K));
    fwdgt_write_enable();
    fwdgt_config(1563 ,FWDGT_PSC_DIV256);//10s
    fwdgt_enable();
    Serial.println("MCU WDG Enabled");
}

void iwdg_feed()
{
    fwdgt_write_enable();
    fwdgt_counter_reload();
}

uint32_t getVoltage()
{
    // return as mV
    uint32_t buffer=analogRead(GPIO_VOLT_INPUT_ADC);
    uint32_t pullup=getConfigValue(CONFIG_ADC_VOLT_PULLUP);
    uint32_t pulldown=getConfigValue(CONFIG_ADC_VOLT_PULLDOWN);
    uint32_t adc_vref=getConfigValue(CONFIG_ADC_VREF);
    uint32_t volt=adc_vref*buffer/ADC_MAX*(pullup+pulldown)/pulldown;
    Serial.print("Voltage: ");Serial.println(volt);
    return volt;
}

bool isWakeUp()
{
    return digitalRead(GPIO_WAKE)==HIGH;
}

void prepareSleepMode()
{
    Serial.println("Preparing for Sleep Mode");
    Serial.flush();
    iwdg_feed();
    NVIC_SystemReset();
}

void enterSleepMode()
{
    digitalWrite(GPIO_RUN_LED,LOW);
    Serial.println("Entering Sleep Mode");
    Serial.flush();
    rcu_periph_clock_enable(RCU_PMU);
    pmu_flag_clear(PMU_FLAG_WAKEUP);
    pmu_flag_clear(PMU_FLAG_STANDBY);
    pmu_wakeup_pin_enable(PMU_WAKEUP_PIN0);
    pmu_to_standbymode(WFI_CMD);
}

void setup() {
    Serial.begin(115200);
    Serial.println("MCU Reset");
    analogReadResolution(ADC_SET_RESOLUTION);
    pinMode(GPIO_WAKE,INPUT);
    pinMode(GPIO_VOLT_INPUT_ADC,INPUT_ANALOG);
    pinMode(GPIO_EN_ROUTER,OUTPUT);
    pinMode(GPIO_EN_DC,OUTPUT);
    pinMode(GPIO_WDT_RESET_BUTTON,INPUT);
    attachInterrupt(digitalPinToInterrupt(GPIO_WDT_RESET_BUTTON),&reset_button_pressed, FALLING);
    attachInterrupt(digitalPinToInterrupt(GPIO_WDT_RESET_BUTTON),&reset_button_released, RISING);
    pinMode(GPIO_WDT_INPUT,INPUT);
    attachInterrupt(digitalPinToInterrupt(GPIO_WDT_INPUT),&wdt_reset_isr, CHANGE);
    pinMode(GPIO_SHUTDOWN_NOTIFY,OUTPUT);
    pinMode(GPIO_RUN_LED,OUTPUT);

    digitalWrite(GPIO_EN_ROUTER,LOW);
    digitalWrite(GPIO_EN_DC,LOW);
    digitalWrite(GPIO_SHUTDOWN_NOTIFY,LOW);

    if(!ee_init(EE_VERSION))
    {
        Serial.println("Using default config");
        setConfigDefaults();
    }
    else Serial.println("Loaded config from EEPROM");
    printConfig();

    if(rcu_flag_get(RCU_FLAG_SWRST) && getVoltage()<getConfigValue(CONFIG_VOLT_WAKEUP)) enterSleepMode();

    iwdg_setup();

    UpdateLED();
    setSystemState(STANDBY);
    
}

unsigned long previousMillis = 0;

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= 1000) {
        previousMillis = currentMillis;

        SystemStateLoop();
        UpdateLED();
        iwdg_feed();
    }
    if(config_reset_flag)
    {
        setConfigDefaults();
        saveConfig();
        config_reset_flag=false;
    }
    delay(1);
}

void setSystemState(SystemState state)
{
    Serial.print("SystemState: ");Serial.println(getSystemStateString(state));
    sys_state=state;
    switch (sys_state)
    {
    case STARTING:
        time_count=0;
        digitalWrite(GPIO_EN_ROUTER,HIGH);
        digitalWrite(GPIO_EN_DC,HIGH);
        break;
    case RUNNING:
        wdt_count=0;
        uvlo_count=0;
        time_count=0;
        break;
    case SHUTTING_DOWN:
        digitalWrite(GPIO_SHUTDOWN_NOTIFY,HIGH);
        delay(1000);
        digitalWrite(GPIO_SHUTDOWN_NOTIFY,LOW);
        Serial.println("Shutdown Notify Sent");
        time_count=0;
        break;
    case RESTARTING:
        digitalWrite(GPIO_EN_ROUTER,LOW);
        delay(1000);
        digitalWrite(GPIO_EN_ROUTER,HIGH);
        time_count=0;
        break;
    case STANDBY:
    default:
        time_count=0;
        uvlo_count=0;
        break;
    }
    previousMillis=millis();
}

void SystemStateLoop()
{
    switch (sys_state)
    {
    case STARTING:
    case RESTARTING:
        time_count++;
        if(time_count>=getConfigValue(CONFIG_TIME_STARTING)) setSystemState(RUNNING);
        break;
    case RUNNING:
        //UVLO SHUTDOWN
        if(!isWakeUp() && getVoltage()<getConfigValue(CONFIG_VOLT_UVLO))
        {
            uvlo_count++;
            Serial.print("UVLO Count: ");Serial.println(uvlo_count);
            if(uvlo_count>getConfigValue(CONFIG_TIMEOUT_UVLO)) 
            {
                Serial.println("UVLO Timeout");
                setSystemState(SHUTTING_DOWN);
                break;
            }
        }
        else uvlo_count=0;
        //WDT
        wdt_count++;
        if(digitalRead(GPIO_WDT_RESET_BUTTON)==LOW) wdt_count=0;
        Serial.print("WDT Count: ");Serial.println(wdt_count);
        if(wdt_count>getConfigValue(CONFIG_TIMEOUT_WDT)) 
        {
            Serial.println("WDT Timeout");
            setSystemState(RESTARTING);
            break;
        }
        break;
    case SHUTTING_DOWN:
        time_count++;
        if(time_count>getConfigValue(CONFIG_TIME_SHUTDOWN))
        {
            digitalWrite(GPIO_EN_ROUTER,LOW);
            digitalWrite(GPIO_EN_DC,LOW);
            setSystemState(STANDBY);
        }
        break;
    case STANDBY:
    default:
        uint32_t volt=getVoltage();
        if(isWakeUp() || volt>=getConfigValue(CONFIG_VOLT_WAKEUP))
        {
            time_count++;
            uvlo_count=0;
            if(time_count>getConfigValue(CONFIG_TIME_WAKEUP)) setSystemState(STARTING);
        }
        else
        {
            time_count=0;
            uvlo_count++;
            if(uvlo_count>getConfigValue(CONFIG_TIME_ENTER_SLEEP)) prepareSleepMode();
        }
        break;
    }
}