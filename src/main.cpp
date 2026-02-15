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

// #define ADC_VOLT_PULLUP 200000     //ohm
// #define ADC_VOLT_PULLDOWN 20000    //ohm
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
    200000, //CONFIG_ADC_VOLT_PULLUP
    20000,  //CONFIG_ADC_VOLT_PULLDOWN
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

Shell shell(Serial);

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

int8_t saveConfig()
{
    return ee_save();
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
volatile bool reset_button_pressed = false;
bool config_reset_flag=false;

void reset_button_isr()
{
    if(digitalRead(GPIO_WDT_RESET_BUTTON)==LOW)//pressed
    {
        resetMillis = millis();
        reset_button_pressed=true;
    }
    else
    {
        if(reset_button_pressed && millis() - resetMillis >= 10000) //released 10s
        {
            //do reset
            config_reset_flag=true;
        }
        reset_button_pressed=false;
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
    shell.println("MCU WDG Enabled");
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
    // shell.print("Voltage: ");shell.print(volt);shell.println();
    return volt;
}

bool isWakeUp()
{
    return digitalRead(GPIO_WAKE)==HIGH;
}

void prepareSleepMode()
{
    shell.println("Preparing for Sleep Mode");
    Serial.flush();
    iwdg_feed();
    NVIC_SystemReset();
}

void enterSleepMode()
{
    digitalWrite(GPIO_RUN_LED,LOW);
    shell.println("Entering Sleep Mode");
    Serial.flush();
    rcu_periph_clock_enable(RCU_PMU);
    pmu_flag_clear(PMU_FLAG_WAKEUP);
    pmu_flag_clear(PMU_FLAG_STANDBY);
    pmu_wakeup_pin_enable(PMU_WAKEUP_PIN0);
    pmu_to_standbymode(WFI_CMD);
}

//cmd

void printConfig()
{
    for(int i=0;i<CONFIG_COUNT;i++)
    {
        shell.print(config_string[i]);
        shell.print(": ");
        shell.print(getConfigValue( (ConfigID)i) );
        shell.print(SHELL_NEW_LINE);
    }
    shell.print("Current State: ");shell.print(getSystemStateString(sys_state));shell.println();
    shell.print("UVLO Timeout Count: ");shell.print(uvlo_count);shell.println();
    shell.print("WDT Timeout Count: ");shell.print(wdt_count);shell.println();
    shell.print("Timer Count: ");shell.print(time_count);shell.println();
}

void cmd_print(Shell& s, char* args)
{
    printConfig();
}

void cmd_save(Shell& s, char* args)
{
    int8_t result=saveConfig();

    s.println("Saved");
    s.print("Current page: ");s.print((int32_t)result);s.println();
}

void cmd_restorefactory(Shell& s, char* args)
{
    setConfigDefaults();
    int8_t result=saveConfig();
    s.println("Resetting to defaults");
    s.print("Current page: ");s.print((int32_t)result);s.println();
}

void cmd_restartdc(Shell& s, char* args)
{
    if(sys_state==STANDBY)
    {
        s.println("Error: cannot restart DC on StandBy mode");
        return;
    }
    s.println("Disabling DC...");
    digitalWrite(GPIO_EN_DC,LOW);
    delay(1000);
    s.println("Enabling DC...");
    digitalWrite(GPIO_EN_DC,LOW);
    s.println("Done");
}

//cmd v
void cmd_v_vref_get(Shell& s, char* args)
{
    s.print("VREF: ");s.print(getConfigValue(CONFIG_ADC_VREF));s.println("mV");
}

void cmd_v_vref_set(Shell& s, char* args)
{
    int32_t result=atoi(args);
    if(result<3000 || result>4000)
    {
        s.println("Error: VREF must between 3000 and 4000 mV");
        s.print("Your input: ");s.print(result);s.println();
        return;
    }
    setConfigValue(CONFIG_ADC_VREF,result);
    s.print("VREF set to: ");s.print(getConfigValue(CONFIG_ADC_VREF));s.println("mV");
}

void cmd_v_pull_get(Shell& s, char* args)
{
    s.print("Pull Up: ");s.print(getConfigValue(CONFIG_ADC_VOLT_PULLUP));
    s.println();
    s.print("Pull Down: ");s.print(getConfigValue(CONFIG_ADC_VOLT_PULLDOWN));
    s.println();
}

void cmd_v_pull_set(Shell& s, char* args)
{
    char s_pullup[SHELL_MAX_COMMAND_LENGTH],s_pulldown[SHELL_MAX_COMMAND_LENGTH];
    splitArgs(args,s_pullup,s_pulldown);
    if(strlen(s_pullup)==0||strlen(s_pulldown)==0)
    {
        s.println("Usage: set pull [pullup] [pulldown]");
        return;
    }
    int pullup=atoi(s_pullup);
    int pulldown=atoi(s_pulldown);
    if(pullup<=0 || pulldown<=0)
    {
        s.println("PullUp and PullDown must larger than 0");
        s.print("Your input: PullUp: ");s.print((int32_t)pullup);s.print(" PullDown: ");s.print((int32_t)pulldown);s.println();
        return;
    }
    setConfigValue(CONFIG_ADC_VOLT_PULLUP,(uint32_t)pullup);
    setConfigValue(CONFIG_ADC_VOLT_PULLDOWN,(uint32_t)pulldown);
    s.print("Set PullUp: ");s.print((int32_t)pullup);s.print(" PullDown: ");s.print((int32_t)pulldown);s.println();
}

void cmd_v_uvlo_get(Shell& s, char* args)
{
    s.print("UVLO: ");s.print(getConfigValue(CONFIG_VOLT_UVLO));s.println("mV");
}

void cmd_v_uvlo_set(Shell& s, char* args)
{
    int32_t result=atoi(args);
    uint32_t wakeup=getConfigValue(CONFIG_VOLT_WAKEUP);
    if(result<7000 || result>=wakeup)
    {
        s.print("Error: VREF must higher than 7000mV and below Wakeup ");s.print(wakeup);s.println("mV");
        s.print("Your input: ");s.print(result);s.println();
        return;
    }
    setConfigValue(CONFIG_VOLT_UVLO,result);
    s.print("UVLO Voltage set to: ");s.print(getConfigValue(CONFIG_VOLT_UVLO));s.println("mV");
}

void cmd_v_wakeup_get(Shell& s, char* args)
{
    s.print("WakeUp: ");s.print(getConfigValue(CONFIG_VOLT_WAKEUP));s.println("mV");
}

void cmd_v_wakeup_set(Shell& s, char* args)
{
    int32_t result=atoi(args);
    uint32_t uvlo=getConfigValue(CONFIG_VOLT_UVLO);
    if(result<=uvlo || result>40000)
    {
        s.print("Error: VREF must higher than UVLO ");s.print(uvlo);s.println(" mV and below 40000mV");
        s.print("Your input: ");s.print(result);s.println();
        return;
    }
    setConfigValue(CONFIG_VOLT_WAKEUP,result);
    s.print("WakeUp Voltage set to: ");s.print(getConfigValue(CONFIG_VOLT_WAKEUP));s.println("mV");
}

void cmd_volt(Shell& s, char* args)
{
    s.print("Current Input Voltage: ");s.print(getVoltage());s.println("mV");
}

CommandInterpreter intp_get(shell,"get","Get Config");
CommandInterpreter intp_set(shell,"set","Set Config");

// CommandInterpreter intp_time(shell,"t","Time Specified");

void registerCommands()
{
    shell.registerCommand("volt","Get Current Input Voltage",&cmd_volt);
    shell.registerCommand("print","Print All Config",&cmd_print);
    shell.registerCommand("restartdc","PowerOff DC and re-PowerOn",&cmd_restartdc);
    shell.registerCommand("save","Save Config",&cmd_save);
    shell.registerCommand("restorefactory","Restore Factory Config",&cmd_restorefactory);

    intp_get.registerCommand("v_vref","Get VREF (mV)",&cmd_v_vref_get);
    intp_get.registerCommand("v_pull","Get VIN Resistance",&cmd_v_pull_get);
    intp_get.registerCommand("v_uvlo","Get Low Voltage Shutdown Voltage (mV)",&cmd_v_uvlo_get);
    intp_get.registerCommand("v_wakeup","Get Wake Up Voltage (mV)",&cmd_v_wakeup_get);

    intp_set.registerCommand("v_vref","Set VREF (mV)",&cmd_v_vref_set);
    intp_set.registerCommand("v_pull","Set VIN Resistance",&cmd_v_pull_set);
    intp_set.registerCommand("v_uvlo","Set Low Voltage Shutdown Voltage (mV)",&cmd_v_uvlo_set);
    intp_set.registerCommand("v_wakeup","Set Wake Up Voltage (mV)",&cmd_v_wakeup_set);

    shell.registerCommandInterpreter(&intp_get);
    shell.registerCommandInterpreter(&intp_set);

    // shell.registerCommandInterpreter(&intp_time);
}

void setup() {
    Serial.begin(115200);
    shell.println("MCU Reset");
    analogReadResolution(ADC_SET_RESOLUTION);
    pinMode(GPIO_WAKE,INPUT);
    pinMode(GPIO_VOLT_INPUT_ADC,INPUT_ANALOG);
    pinMode(GPIO_EN_ROUTER,OUTPUT);
    pinMode(GPIO_EN_DC,OUTPUT);
    pinMode(GPIO_WDT_RESET_BUTTON,INPUT);
    attachInterrupt(digitalPinToInterrupt(GPIO_WDT_RESET_BUTTON),&reset_button_isr, CHANGE);
    pinMode(GPIO_WDT_INPUT,INPUT);
    attachInterrupt(digitalPinToInterrupt(GPIO_WDT_INPUT),&wdt_reset_isr, CHANGE);
    pinMode(GPIO_SHUTDOWN_NOTIFY,OUTPUT);
    pinMode(GPIO_RUN_LED,OUTPUT);

    digitalWrite(GPIO_EN_ROUTER,LOW);
    digitalWrite(GPIO_EN_DC,LOW);
    digitalWrite(GPIO_SHUTDOWN_NOTIFY,LOW);

    int8_t ee_result=ee_init(EE_VERSION);
    if(ee_result<0)
    {
        shell.println("Using default config");
        setConfigDefaults();
    }
    else
    {
        shell.print("Loaded config from EEPROM page ");shell.print((int32_t)ee_result);shell.println();
    }
    printConfig();

    shell.begin();
    registerCommands();

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
        shell.println("WDT Button: Resetting to defaults");
        setConfigDefaults();
        saveConfig();
        config_reset_flag=false;
    }
    int c;
    while((c=Serial.read())>0)
    {
        if(sys_state==STANDBY && isPrintable(c)) uvlo_count=0;//dont sleep on serial active
        shell.inputChar((char)c);
    }
    shell.prompt_flush();
    delay(1);
}

void setSystemState(SystemState state)
{
    shell.print("SystemState: ");shell.println(getSystemStateString(state));
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
        shell.println("Shutdown Notify Sent");
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
            shell.print("UVLO Count: ");shell.print(uvlo_count);shell.println();
            if(uvlo_count>getConfigValue(CONFIG_TIMEOUT_UVLO)) 
            {
                shell.println("UVLO Timeout");
                setSystemState(SHUTTING_DOWN);
                break;
            }
        }
        else uvlo_count=0;
        //WDT
        wdt_count++;
        if(digitalRead(GPIO_WDT_RESET_BUTTON)==LOW) wdt_count=0;
        // shell.print("WDT Count: ");shell.print(wdt_count);shell.println();
        if(wdt_count>getConfigValue(CONFIG_TIMEOUT_WDT)) 
        {
            shell.println("WDT Timeout");
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