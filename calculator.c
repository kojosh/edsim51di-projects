/*
 * 8051 Calculator - Addition of two 16-bit unsigned integers
 * Target: EdSim51 Simulator
 * Compiler: SDCC (Small Device C Compiler)
 *
 * Hardware Connections (4-bit LCD mode):
 *   LCD DB7 -> P1.7
 *   LCD DB6 -> P1.6
 *   LCD DB5 -> P1.5
 *   LCD DB4 -> P1.4
 *   LCD RS  -> P1.3
 *   LCD E   -> P1.2
 *
 * Keypad: All on P0
 *   Rows:    P0.0 - P0.3  (driven low one at a time)
 *   Columns: P0.4 - P0.6  (read back; active low)
 *   '*' key = addition operator
 *   '#' key = equals / compute result
 *
 * Usage:
 *   1. Enter first number  (up to 5 digits, 0-65535)
 *   2. Press '*'           (addition sign shown on LCD)
 *   3. Enter second number (up to 5 digits, 0-65535)
 *   4. Press '#'           (result shown on LCD)
 *   5. Any key resets for a new calculation
 */

#include <8051.h>

/* LCD pin definitions (4-bit mode on P1) */
#define DB7 P1_7
#define DB6 P1_6
#define DB5 P1_5
#define DB4 P1_4
#define RS P1_3
#define E P1_2

/* Global variables for keypad scanning */
unsigned char key = 0;

/* Prototypes */  
void functionSet(void);
void returnHome(void);
void entryModeSet(__bit id, __bit s);
void displayOnOffControl(__bit display, __bit cursor, __bit blinking);
void cursorOrDisplayShift(__bit sc, __bit rl);
void setDdRamAddress(char address);
void sendChar(char c);
void sendString(const __code char* str);
__bit getBit(char c, char bitNumber);
void delay(void);
void ScanKeyPad(void);
void IDCode0(void);
void IDCode1(void);
void IDCode2(void);
void IDCode3(void);
void display_number(unsigned int num);
void clear_display(void);
__bit isOverflow(unsigned int num1, unsigned int num2);

unsigned char __sdcc_external_startup(void){
    return 1;
}

/* ═══════════════════════════════════════════
   LCD Module instructions
═══════════════════════════════════════════ */
void returnHome(void) {
    RS = 0;
    DB7 = 0;
    DB6 = 0;
    DB5 = 0;
    DB4 = 0;
    E = 1;
    E = 0;
    DB5 = 1;
    E = 1;
    E = 0;
    delay();
}

void entryModeSet(__bit id, __bit s) {
    RS = 0;
    DB7 = 0;
    DB6 = 0;
    DB5 = 0;
    DB4 = 0;
    E = 1;
    E = 0;
    DB6 = 1;
    DB5 = id;
    DB4 = s;
    E = 1;
    E = 0;
    delay();
}

void displayOnOffControl(__bit display, __bit cursor, __bit blinking) {
    RS = 0;
    DB7 = 0;
    DB6 = 0;
    DB5 = 0;
    DB4 = 0;
    E = 1;
    E = 0;
    DB7 = 1;
    DB6 = display;
    DB5 = cursor;
    DB4 = blinking;
    E = 1;
    E = 0;
    delay();
}

void cursorOrDisplayShift(__bit sc, __bit rl) {
    RS = 0;
    DB7 = 0;
    DB6 = 0;
    DB5 = 0;
    DB4 = 1;
    E = 1;
    E = 0;
    DB7 = sc;
    DB6 = rl;
    E = 1;
    E = 0;
    delay();
}

void functionSet(void) {
    DB7 = 0;
    DB6 = 0;
    DB5 = 1;
    DB4 = 0;
    RS = 0;
    E = 1;
    E = 0;
    delay();
    E = 1;
    E = 0;
    DB7 = 1;
    E = 1;
    E = 0;
    delay();
}

void setDdRamAddress(char address) {
    RS = 0;
    DB7 = 1;
    DB6 = getBit(address, 6);
    DB5 = getBit(address, 5);
    DB4 = getBit(address, 4);
    E = 1;
    E = 0;
    DB7 = getBit(address, 3);
    DB6 = getBit(address, 2);
    DB5 = getBit(address, 1);
    DB4 = getBit(address, 0);
    E = 1;
    E = 0;
    delay();
}

void sendChar(char c) {
    RS = 1;
    DB7 = getBit(c, 7);
    DB6 = getBit(c, 6);
    DB5 = getBit(c, 5);
    DB4 = getBit(c, 4);
    E = 1;
    E = 0;
    DB7 = getBit(c, 3);
    DB6 = getBit(c, 2);
    DB5 = getBit(c, 1);
    DB4 = getBit(c, 0);
    E = 1;
    E = 0;
    delay();
}

void sendString(const __code char* str) {
    int index = 0;
    while (str[index] != 0) {
        sendChar(str[index]);
        index++;
    }
}

__bit getBit(char c, char bitNumber) {
    return (c >> bitNumber) & 1;
}

__bit isOverflow(unsigned int num1, unsigned int num2) {
    int result = num1 + num2;
    if (result < num1 || result < num2) {
        clear_display();
        sendString("Overflow!");
        delay();
        return 1;
    }
    return 0;
}

void delay(void) {
    char c;
    for (c = 0; c < 50; c++);
}

/* ═══════════════════════════════════════════
   Display utilities
═══════════════════════════════════════════ */
void display_number(unsigned int num)
{
    unsigned char digits[6];
    unsigned char count = 0;
    unsigned int temp = num;
    
    if (num == 0) {
        sendChar('0');
        return;
    }
    
    while (temp > 0) {
        digits[count++] = (temp % 10) + '0';
        temp /= 10;
    }
    
    for (temp = count; temp > 0; temp--) {
        sendChar(digits[temp - 1]);
    }
}

void clear_display(void)
{
    RS = 0;
    DB7 = 0;
    DB6 = 0;
    DB5 = 0;
    DB4 = 0;
    E = 1;
    E = 0;
    delay();
    DB7 = 0;
    DB6 = 0;
    DB5 = 0;
    DB4 = 1;
    E = 1;
    E = 0;
    delay();
    delay();
    delay();
}

/* ------------------------------------------------------------------------- */
/* KEYPAD SCAN                                                               */
/* ------------------------------------------------------------------------- */
void ScanKeyPad(void)
{
    while (1)
    {
        /* Scan Row 3 */
        P0_3 = 0;
        IDCode0();
        P0_3 = 1;

        if (F0)
            break;

        /* Scan Row 2 */
        P0_2 = 0;
        IDCode1();
        P0_2 = 1;

        if (F0)
            break;

        /* Scan Row 1 */
        P0_1 = 0;
        IDCode2();
        P0_1 = 1;

        if (F0)
            break;

        /* Scan Row 0 */
        P0_0 = 0;
        IDCode3();
        P0_0 = 1;

        if (F0)
            break;
    }

    F0 = 0;
}

/* ------------------------------------------------------------------------- */
/* ROW 3                                                                     */
/* ------------------------------------------------------------------------- */
void IDCode0(void)
{
    if (P0_4 == 0)
    {
        F0 = 1;
        key = '3';
    }
    else if (P0_5 == 0)
    {
        F0 = 1;
        key = '2';
    }
    else if (P0_6 == 0)
    {
        F0 = 1;
        key = '1';
    }
}

/* ------------------------------------------------------------------------- */
/* ROW 2                                                                     */
/* ------------------------------------------------------------------------- */
void IDCode1(void)
{
    if (P0_4 == 0)
    {
        F0 = 1;
        key = '6';
    }
    else if (P0_5 == 0)
    {
        F0 = 1;
        key = '5';
    }
    else if (P0_6 == 0)
    {
        F0 = 1;
        key = '4';
    }
}

/* ------------------------------------------------------------------------- */
/* ROW 1                                                                     */
/* ------------------------------------------------------------------------- */
void IDCode2(void)
{
    if (P0_4 == 0)
    {
        F0 = 1;
        key = '9';
    }
    else if (P0_5 == 0)
    {
        F0 = 1;
        key = '8';
    }
    else if (P0_6 == 0)
    {
        F0 = 1;
        key = '7';
    }
}

/* ------------------------------------------------------------------------- */
/* ROW 0                                                                     */
/* ------------------------------------------------------------------------- */
void IDCode3(void)
{
    if (P0_4 == 0)
    {
        F0 = 1;
        key = '#';
    }
    else if (P0_5 == 0)
    {
        F0 = 1;
        key = '0';
    }
    else if (P0_6 == 0)
    {
        F0 = 1;
        key = '*';
    }
}

/* ═══════════════════════════════════════════
   Main program
═══════════════════════════════════════════ */
void main(void) __naked
{
    unsigned int num1 = 0, num2 = 0, result = 0;
    unsigned char digit_count = 0;
    unsigned char state = 0;

    functionSet();
    entryModeSet(1, 0);
    displayOnOffControl(1, 1, 1);
    delay();
    clear_display();

    while(1) {
        ScanKeyPad();
        
        if (key != 0) {
            // Default state: enter first number
            if (state == 0) {
                if (key >= '0' && key <= '9') {
                    if (isOverflow(num1 * 10, key - '0')) {
                        clear_display();
                        sendString("Overflow!");
                        delay();
                        break;
                    }
                    if (digit_count < 5) {
                        if (num1 < 6553 || (num1 == 6553 && key <= '5'))
                            num1 = num1 * 10 + (key - '0');
                        else {
                            clear_display();
                            sendString("Overflow!");
                            delay();
                            break;
                        }
                        sendChar(key);
                        digit_count++;
                    }
                }
                // If '*' is pressed, move to next state to enter second number
                else if (key == '*') {
                    state = 1;
                    digit_count = 0;
                    sendChar('+');
                }
            }
            // State 1: entering second number
            else if (state == 1) {
                if (key >= '0' && key <= '9') {
                    if (isOverflow(num2 * 10, key - '0')) {
                        clear_display();
                        sendString("Overflow!");
                        delay();
                        break;
                    }
                    if (digit_count < 5) {
                        if (num2 < 6553 || (num2 == 6553 && key <= '5'))
                            num2 = num2 * 10 + (key - '0');
                        else {
                            clear_display();
                            sendString("Overflow!");
                            delay();
                            break;
                        }
                        sendChar(key);
                        digit_count++;
                    }
                }
                // If '#' is pressed, compute and display result
                else if (key == '#') {
                    if (isOverflow(num1, num2)) {
                        clear_display();
                        break;
                    }
                    result = num1 + num2;
                    state = 2;
                    sendChar('=');
                    display_number(result);
                }
            }
            // State 2: result displayed, any key resets the calculator
            else if (state == 2) {
                num1 = 0;
                num2 = 0;
                result = 0;
                digit_count = 0;
                state = 0;

                clear_display();
                sendChar(key);
                sendChar(key);
                sendChar(key);

                if (key >= '0' && key <= '9') {
                    num1 = num1 * 10 + (key - '0');
                    sendChar(key);
                    digit_count++;
                }
            }
        }
    }
}
