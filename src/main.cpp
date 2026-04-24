#include "mbed.h"
#include "deque"
#include "string"

//Initialise pins
DigitalIn col1(PB_12);
DigitalIn col2(PB_13);
DigitalIn col3(PB_15);
DigitalIn col4(PC_6);
DigitalOut row1(PB_3);
DigitalOut row2(PB_5);
DigitalOut row3(PC_7);
DigitalOut row4(PA_15);
DigitalOut buzzer(PE_10);
AnalogIn gas(A2);
AnalogIn pot(A0);
AnalogIn temp(A1);

//constant strings used in printing to terminal
const string POT_NAME = "POTENTIOMETER";
const string TEMP_NAME = "TEMPERATURE";
const string GAS_NAME = "GAS";
const string TEMP_WARNING = "TEMPERATURE WARNING";
const string GAS_WARNING = "GAS WARNING";
const string SYSTEM_NORMAL = "ALL SYSTEMS NORMAL";
constexpr int MAX_LOGS = 5;

//constants
constexpr int NUM_OF_KEYS = 4;
constexpr int DEBOUNCE_TIME = 30;

//RTC config
void RTCConfig() {
    struct tm t;
    printf("\nENTER CURRENT YEAR: YYYY\n");
    scanf("%d", &t.tm_year);
    printf("ENTER CURRENT MONTH: MM\n");
    scanf("%d", &t.tm_mon);
    printf("ENTER CURRENT DAY: DD\n");
    scanf("%d", &t.tm_mday);
    printf("ENTER CURRENT HOUR: HH\n");
    scanf("%d", &t.tm_hour);
    printf("ENTER CURRENT MINUTE: MM\n");
    scanf("%d", &t.tm_min);
    printf("ENTER CURRENT SECOND: SS\n");
    scanf("%d", &t.tm_sec);

    // adjust because time is weird in C
    t.tm_year = t.tm_year - 1900;
    t.tm_mon = t.tm_mon - 1;

    set_time(mktime(&t));

    time_t seconds = time(NULL);
    printf("TIME SET: %s", ctime(&seconds));
}

//communication with board
UnbufferedSerial uartUsb(USBTX, USBRX, 115200);

//define matrix
char matrixPad[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

DigitalOut matrixRow[4] = {row1, row2, row3, row4};
DigitalIn matrixCol[4] = {col1, col2, col3, col4};

//Global variables
bool alarmActive = 0;
bool tempAlarmActive = 0;
bool gasAlarmActive = 0;
char newKey = '\0';
float potValOld;

// Define the enum system states (to control entire code in main loop)
typedef enum {
    STM_MONITOR,
    STM_ENTR_PASSCODE,
    STM_ALARM
} systemState_t;

systemState_t currentState = STM_MONITOR;

//struct to define info to be stored in logs
typedef struct {
    string alarmType;
    time_t alarmTime{};
} logEvent_t;

//deque to manage log events
deque<logEvent_t> logEvents;

//initialise stuff used in code
void init() {
    col1.mode(PullDown);
    col2.mode(PullDown);
    col3.mode(PullDown);
    col4.mode(PullDown);
    alarmActive = 0;
    RTCConfig();
    printf("\nSYSTEM ACTIVE\n");
}

//Function to continuously read and print data from sensor
float readSensor(PinName pin, const string &sensorName) {
    AnalogIn sensor(pin);
    sensor.read();
    //allaows internal cap. to discharge before reading stopps bleeding problem from pot to sensor value, doesnt really work...will problably be fine once external power source is used
    ThisThread::sleep_for(10ms); //try lrt capacitor settle nice easy bleeding fix
    float sensorVal = sensor.read();
    //printf("%s raw sensor value: %f \n", sensorName.c_str(), sensorVal); //only used to test functionality
    return sensorVal;
}

//Function to map the potentiometer to an acceptable threshold range
float mapPot(float potVal, float maxVal, float minVal) {
    return potVal * (maxVal - minVal) + minVal; //actually need float values now as want decimals for temp sensor
}

//function to set new threshold ranges using potentiometer
void updateThresholds() {
    ThisThread::sleep_for(10ms);
    float potValCurrent = pot.read();
    if (abs(potValCurrent - potValOld) > 0.02f) {
        //+-0.2 is 2% change, should be greater than any ADC fluctuations
        float tempThreshold = mapPot(potValCurrent, 37.0, 25.0);
        float gasThreshold = mapPot(potValCurrent, 800.0, 0.0);
        printf("\nNew threshold levels:\nTemp: %0.f degrees\nGas: %0.f PPM\n", tempThreshold, gasThreshold);
    }
}

//function to control the buzzer
void buzzerState() {
    buzzer = alarmActive;
}

//Function to evaluate sensor data
void evaluateData(const string &sensorName, PinName pin) {
    float potVal = pot.read();
    float sensorVal = readSensor(pin, sensorName);
    if (sensorName == TEMP_NAME) {
        float currentTemp = sensorVal * 330.0f; //converts sensor value to degrees Celsius
        float thresholdTemp = mapPot(potVal, 37.0, 25.0);
        // printf("Temp value: %.0f degrees Celsius \nThreshold value: %.0f degrees celcius\n", currentTemp,
        // thresholdTemp); //testing
        if (currentTemp > thresholdTemp) {
            //printf("%s ALARM SOUNDING \n", TEMP_WARNING.c_str()); //testing
            alarmActive = 1;
            tempAlarmActive = 1;
        }
    }
    if (sensorName == GAS_NAME) {
        float currentGasLevel = sensorVal * 1700.0f; //roughly converts to ppm
        float thresholdGas = mapPot(potVal, 800.0f, 0.0f);
        //printf("gas level: %.0f ppm \nThreshold value: %.0f ppm \n", currentGasLevel, thresholdGas); //testing
        if (currentGasLevel > thresholdGas) {
            //printf("%s ALARM SOUNDING \n", GAS_WARNING.c_str());//testing
            alarmActive = 1;
            gasAlarmActive = 1;
        }
    }
    buzzerState();
}

//Function to check for a key press on matrix pad
char matrixPressed() {
    for (auto row: matrixRow) {
        row = false;
    }
    for (int row = 0; row < NUM_OF_KEYS; row++) {
        matrixRow[row] = 1;
        ThisThread::sleep_for(DEBOUNCE_TIME);

        for (int col = 0; col < NUM_OF_KEYS; col++) {
            if (matrixCol[col]) {
                return matrixPad[row][col];
            }
        }
        matrixRow[row] = 0;
    }
    return 0;
    //switch to debounce
}

//Function to debounce a keypress
char matrixDebounce() {
    char oldKey = matrixPressed();
    if (oldKey != '\0') {
        ThisThread::sleep_for(DEBOUNCE_TIME);
        newKey = matrixPressed();
        if (newKey == oldKey) {
            return newKey;
            //switch to keypress
        }
    }
    //switch back to scanning
    return 0;
}

//Function that waits for a key to be pressed then outputs the key
char MatrixKeyPress() {
    char key = '\0';
    while (true) {
        key = matrixDebounce();
        if (key != '\0') {
            while (matrixDebounce() != '\0') {
                ThisThread::sleep_for(DEBOUNCE_TIME);
            }
            return key;
        }
        ThisThread::sleep_for(DEBOUNCE_TIME);
    }
}

//Function to take a passcode as an input
string enterPasscode() {
    printf("Please enter your 4-digit passcode:\n");
    char passcodeEntered[5];
    for (int i = 0; i < 4; i++) {
        char key = MatrixKeyPress();
        // printf("*");
        passcodeEntered[i] = key;
    }
    passcodeEntered[4] = '\0';
    printf("\nCode entered: %s\n", passcodeEntered);
    return passcodeEntered;
}

//Function to log the most recent 5 alarms
void logAlarms() {
    time_t rawTime;
    time(&rawTime);
    logEvent_t newEvent;
    string alarmType;
    if (gasAlarmActive) {
        alarmType = GAS_WARNING;
        newEvent = {GAS_WARNING, time(NULL)};
        logEvents.push_front(newEvent); //ensures newest event is top of list
    }
    if (tempAlarmActive) {
        alarmType = TEMP_WARNING;
        newEvent = {TEMP_WARNING, time(NULL)};
        logEvents.push_front(newEvent);
    }
    if (logEvents.size() > MAX_LOGS) {
        logEvents.pop_back(); //'pop' oldest event off the list like it
    }
    //printf("\n%s\nTime and date: %s\n",alarmType.c_str(), ctime(&newEvent.alarmTime)); //testing
}

//Function to print log list -- when # is pressed
void printEventLogs(deque<logEvent_t> printLog) {
    printf("\nAlarm log:\n");
    for (const auto &printLog: printLog) {
        printf("\n%s\nTime and date: %s\n", printLog.alarmType.c_str(), ctime(&printLog.alarmTime));
    }
}

int main() {
    init();
    currentState = STM_MONITOR;
    while (true) {
        string passcode;
        potValOld = pot.read();
        updateThresholds();
        evaluateData(TEMP_NAME, A1);
        evaluateData(GAS_NAME, A2);

        //enter state machine (controls entire systems now)
        switch (currentState) {
            case STM_MONITOR:
                if (alarmActive) {
                    currentState = STM_ALARM;
                }
                if (matrixDebounce() == '#') {
                    printEventLogs(logEvents);
                }

                break;
            case STM_ALARM:
                printf("\nSYSTEM ALARM, ENTER DEACTIVATION PASSCODE: \n");
                logAlarms();
                currentState = STM_ENTR_PASSCODE;

                break;
            case STM_ENTR_PASSCODE:
                passcode = enterPasscode();
                if (passcode == "1486") {
                    printf("\nCORRECT CODE ENTERED, ALARMS DISABLED.\n");
                    alarmActive = 0;
                    tempAlarmActive = 0;
                    gasAlarmActive = 0;
                    buzzerState();
                    currentState = STM_MONITOR;
                } else {
                    printf("\nINCORRECT PASSCODE! TRY AGAIN \n");
                    passcode = '\0';
                }
                break;
        }
    }
}