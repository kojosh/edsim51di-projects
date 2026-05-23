#include <8051.h>

/* =========================
   LCD pin definitions
   4-bit mode on P1
========================= */

#define DB7 P1_7
#define DB6 P1_6
#define DB5 P1_5
#define DB4 P1_4
#define RS P1_3
#define E P1_2

/* =========================
   Operating mode defines
========================= */

#define MODE_RUN  0   /* Clock running normally    */
#define MODE_TIME 1   /* Time-setting mode (SW0)   */
#define MODE_DATE 2   /* Date-setting mode (SW1)   */

/* =========================
   DDRAM lookup tables
   Maps cur_pos (0-5) to the
   physical DDRAM column,
   skipping ':' and '-'
   Time: HH:MM:SS
     pos 0=H1 1=H2 2=M1 3=M2 4=S1 5=S2
   Date: DD-MM-YY
     pos 0=D1 1=D2 2=Mo1 3=Mo2 4=Y1 5=Y2
========================= */

__code unsigned char time_ddram[6] = {0, 1, 3, 4, 6, 7};
__code unsigned char date_ddram[6] = {0, 1, 3, 4, 6, 7};

/* =========================
   Global clock variables
========================= */

unsigned char hh = 0;    /* Hours   0-23 */
unsigned char mm = 0;    /* Minutes 0-59 */
unsigned char ss = 0;    /* Seconds 0-59 */

unsigned char dd  = 1;   /* Day   1-31  */
unsigned char mon = 1;   /* Month 1-12  */
unsigned char yy  = 0;   /* Year  0-99  */

/* =========================
   Timer tick counter
   100 ticks = 1 second
========================= */

unsigned char tick_count = 0;

/* =========================
   Mode and cursor state
========================= */

unsigned char mode    = MODE_RUN;
unsigned char cur_pos = 0;        /* 0-5 within the active line */

/* =========================
   Keypad global
========================= */

unsigned char key = 0;

/* =========================
   Dirty flags
   Set by ISR, cleared by
   main loop after redraw
========================= */

__bit time_dirty = 1;   /* Force initial draw on startup */
__bit date_dirty = 1;

/* =========================
   Function prototypes
========================= */

void initState(void);

/* LCD low-level */
void delay(void);
__bit getBit(char c, char n);
void functionSet(void);
void entryModeSet(void);
void displayOn(void);
void cursorOn(void);
void clearDisplay(void);
void setDdRamAddress(unsigned char addr);
void sendChar(char c);
void write2(unsigned char v);

/* Display render */
void renderTime(void);
void renderDate(void);
void placeCursor(void);

/* LCD init */
void lcdInit(void);

/* Clock logic */
void tickClock(void);
__bit isLeap(unsigned char y);
unsigned char daysInMonth(unsigned char m, unsigned char y);

/* Keypad */
void wait_key_release(void);
void IDCode0(void);
void IDCode1(void);
void IDCode2(void);
void IDCode3(void);
void ScanKeyPad(void);

/* Key handlers */
void handleTimeKey(unsigned char k);
void handleDateKey(unsigned char k);

/* ISRs */
void timer0_ISR(void) __interrupt(1);
void ext0_ISR(void)   __interrupt(0);
void ext1_ISR(void)   __interrupt(2);

/* SDCC: disable auto-init */
unsigned char __sdcc_external_startup(void) { return 1; }

/* ═══════════════════════════════════════════
   LCD LOW-LEVEL ROUTINES (4-bit mode)
═══════════════════════════════════════════ */

/* Basic delay — used between all LCD operations */
void delay(void)
{
    unsigned int c;
    for (c = 0; c < 500; c++) {
        __asm nop __endasm;
    }
}

/* Return bit n of byte c */
__bit getBit(char c, char n)
{
    return (c >> n) & 1;
}

/* Function Set: 4-bit bus, 2-line display, 5x8 font
   Uses the 3-pulse init sequence required by HD44780 */
void functionSet(void)
{
    RS  = 0;
    DB7 = 0; DB6 = 0; DB5 = 1; DB4 = 0;
    E = 1; E = 0;
    delay();
    E = 1; E = 0;           /* Second pulse */
    DB7 = 1;                /* N=1 (2 lines), F=0 (5x8) */
    E = 1; E = 0;
    delay();
}

/* Entry Mode Set: 0x06 — increment cursor, no display shift */
void entryModeSet(void)
{
    RS  = 0;
    DB7 = 0; DB6 = 0; DB5 = 0; DB4 = 0;
    E = 1; E = 0;
    DB7 = 0; DB6 = 1; DB5 = 1; DB4 = 0;
    E = 1; E = 0;
    delay();
}

/* Display On/Off: 0x0C — display on, cursor off, blink off */
void displayOn(void)
{
    RS  = 0;
    DB7 = 0; DB6 = 0; DB5 = 0; DB4 = 0;
    E = 1; E = 0;
    DB7 = 1; DB6 = 1; DB5 = 0; DB4 = 0;
    E = 1; E = 0;
    delay();
}

/* Display On/Off: 0x0F — display on, cursor on, blink on
   Used when entering time/date setting mode */
void cursorOn(void)
{
    RS  = 0;
    DB7 = 0; DB6 = 0; DB5 = 0; DB4 = 0;
    E = 1; E = 0;
    DB7 = 1; DB6 = 1; DB5 = 1; DB4 = 1;
    E = 1; E = 0;
    delay();
}

/* Clear Display: 0x01 — clears DDRAM and resets cursor */
void clearDisplay(void)
{
    RS  = 0;
    DB7 = 0; DB6 = 0; DB5 = 0; DB4 = 0;
    E = 1; E = 0;
    DB7 = 0; DB6 = 0; DB5 = 0; DB4 = 1;
    E = 1; E = 0;
    delay(); delay(); delay();   /* Clear needs extra time */
}

/* Set DDRAM Address: sends 0x80 | addr as two nibbles */
void setDdRamAddress(unsigned char addr)
{
    unsigned char cmd = 0x80 | addr;
    RS  = 0;
    DB7 = getBit(cmd, 7);
    DB6 = getBit(cmd, 6);
    DB5 = getBit(cmd, 5);
    DB4 = getBit(cmd, 4);
    E = 1; E = 0;
    DB7 = getBit(cmd, 3);
    DB6 = getBit(cmd, 2);
    DB5 = getBit(cmd, 1);
    DB4 = getBit(cmd, 0);
    E = 1; E = 0;
    delay();
}

/* Send one character to the LCD at the current cursor position */
void sendChar(char c)
{
    RS  = 1;
    DB7 = getBit(c, 7);
    DB6 = getBit(c, 6);
    DB5 = getBit(c, 5);
    DB4 = getBit(c, 4);
    E = 1; E = 0;
    DB7 = getBit(c, 3);
    DB6 = getBit(c, 2);
    DB5 = getBit(c, 1);
    DB4 = getBit(c, 0);
    E = 1; E = 0;
    delay();
}

/* Write a 2-digit decimal value at the current cursor position */
void write2(unsigned char v)
{
    sendChar('0' + v / 10);
    sendChar('0' + v % 10);
}

/* ═══════════════════════════════════════════
   LCD INITIALISATION
   Correct HD44780 sequence:
   functionSet -> entryModeSet -> displayOn -> clearDisplay
═══════════════════════════════════════════ */

void lcdInit(void)
{
    functionSet();       /* Set 4-bit, 2-line mode         */
    entryModeSet();      /* Increment cursor, no shift      */
    displayOn();         /* Display on, cursor off          */
    clearDisplay();      /* Clear DDRAM, home cursor        */
}

/* ═══════════════════════════════════════════
   DISPLAY RENDER HELPERS
   Only called when dirty flags are set,
   not on every loop iteration
═══════════════════════════════════════════ */

/* Redraw entire time line: HH:MM:SS at DDRAM 0x00 */
void renderTime(void)
{
    setDdRamAddress(0x00);
    write2(hh); sendChar(':');
    write2(mm); sendChar(':');
    write2(ss);
}

/* Redraw entire date line: DD-MM-YY at DDRAM 0x40 */
void renderDate(void)
{
    setDdRamAddress(0x40);
    write2(dd);  sendChar('-');
    write2(mon); sendChar('-');
    write2(yy);
}

/* Move the hardware cursor to the DDRAM cell for cur_pos.
   Uses lookup tables to skip separator characters. */
void placeCursor(void)
{
    unsigned char addr;
    if (mode == MODE_TIME) {
        addr = time_ddram[cur_pos];         /* Line 1: 0x00-0x07 */
    } else {
        addr = 0x40 + date_ddram[cur_pos];  /* Line 2: 0x40-0x47 */
    }
    setDdRamAddress(addr);
}

/* ═══════════════════════════════════════════
   CALENDAR HELPERS
═══════════════════════════════════════════ */

/* Leap year check: yy is 2-digit offset from 2000
   Years 0,4,8...96 are leap within the 0-99 range */
__bit isLeap(unsigned char y)
{
    return (y % 4 == 0) ? 1 : 0;
}

/* Returns the number of days in month m for year y */
unsigned char daysInMonth(unsigned char m, unsigned char y)
{
    if (m == 2)
        return isLeap(y) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11)
        return 30;
    return 31;
}

/* ═══════════════════════════════════════════
   CLOCK TICK
   Called from Timer0 ISR once per second.
   Only ticks when in MODE_RUN.
   Sets dirty flags so main loop redraws.
═══════════════════════════════════════════ */

void tickClock(void)
{
    if (mode != MODE_RUN) return;

    ss++;
    if (ss >= 60) {
        ss = 0;
        mm++;
        if (mm >= 60) {
            mm = 0;
            hh++;
            if (hh >= 24) {
                hh = 0;
                /* Increment date */
                dd++;
                if (dd > daysInMonth(mon, yy)) {
                    dd = 1;
                    mon++;
                    if (mon > 12) {
                        mon = 1;
                        yy++;
                        if (yy > 99) yy = 0;
                    }
                }
                date_dirty = 1;
            }
        }
    }
    time_dirty = 1;
}

/* ═══════════════════════════════════════════
   TIMER 0 ISR — 10ms tick
   Reloads TH0/TL0 each time.
   Every 100 ticks (~1 second) calls tickClock.
═══════════════════════════════════════════ */

void timer0_ISR(void) __interrupt(1)
{
    TH0 = 0xD8;
    TL0 = 0x00;

    tick_count++;
    if (tick_count >= 100) {
        tick_count = 0;
        tickClock();
    }
}

/* ═══════════════════════════════════════════
   EXTERNAL INTERRUPT 0 — SW0 on P3.2
   Toggle time-setting mode on/off.
   Press once to enter, press again to exit.
═══════════════════════════════════════════ */

void ext0_ISR(void) __interrupt(0)
{
    if (mode == MODE_TIME) {
        /* Exit time-setting mode */
        mode = MODE_RUN;
        displayOn();         /* Turn cursor off */
    } else {
        /* Enter time-setting mode */
        mode    = MODE_TIME;
        cur_pos = 0;

        setDdRamAddress(0x0F);
        sendChar('T');

        renderTime();        /* Refresh line before showing cursor */
        cursorOn();          /* Turn cursor + blink on */
        placeCursor();       /* Position cursor at first digit */
    }
}

/* ═══════════════════════════════════════════
   EXTERNAL INTERRUPT 1 — SW1 on P3.3
   Toggle date-setting mode on/off.
   Press once to enter, press again to exit.
═══════════════════════════════════════════ */

void ext1_ISR(void) __interrupt(2)
{
    if (mode == MODE_DATE) {
        /* Exit date-setting mode */
        mode = MODE_RUN;
        displayOn();
    } else {
        /* Enter date-setting mode */
        mode    = MODE_DATE;
        cur_pos = 0;
        renderDate();
        cursorOn();
        placeCursor();
    }
}

/* ═══════════════════════════════════════════
   KEYPAD ROUTINES
═══════════════════════════════════════════ */

void wait_key_release(void)
{
    unsigned char i;
    while (P0_4 == 0 || P0_5 == 0 || P0_6 == 0) { delay(); }
    for (i = 0; i < 20; i++) { delay(); }
}

/* Row 3: keys 1, 2, 3 */
void IDCode0(void)
{
    if      (P0_4 == 0) { F0 = 1; key = '3'; }
    else if (P0_5 == 0) { F0 = 1; key = '2'; }
    else if (P0_6 == 0) { F0 = 1; key = '1'; }
}

/* Row 2: keys 4, 5, 6 */
void IDCode1(void)
{
    if      (P0_4 == 0) { F0 = 1; key = '6'; }
    else if (P0_5 == 0) { F0 = 1; key = '5'; }
    else if (P0_6 == 0) { F0 = 1; key = '4'; }
}

/* Row 1: keys 7, 8, 9 */
void IDCode2(void)
{
    if      (P0_4 == 0) { F0 = 1; key = '9'; }
    else if (P0_5 == 0) { F0 = 1; key = '8'; }
    else if (P0_6 == 0) { F0 = 1; key = '7'; }
}

/* Row 0: keys *, 0, # */
void IDCode3(void)
{
    if      (P0_4 == 0) { F0 = 1; key = '#'; }
    else if (P0_5 == 0) { F0 = 1; key = '0'; }
    else if (P0_6 == 0) { F0 = 1; key = '*'; }
}

/* Non-blocking keypad scan.
   Sets global key and returns immediately.
   key=0 means nothing was pressed. */
void ScanKeyPad(void)
{
    key = 0;
    F0  = 0;

    P0 = 0xFF;

    P0_3 = 0; IDCode0(); P0_3 = 1; if (F0) goto done;
    P0_2 = 0; IDCode1(); P0_2 = 1; if (F0) goto done;
    P0_1 = 0; IDCode2(); P0_1 = 1; if (F0) goto done;
    P0_0 = 0; IDCode3(); P0_0 = 1;

    done:
    P0 = 0xFF;
    if (F0) { wait_key_release(); F0 = 0; }
}

/* ═══════════════════════════════════════════
   TIME KEY HANDLER
   cur_pos 0-5 maps to: H1 H2 M1 M2 S1 S2
   * moves cursor left  (wraps 0->5)
   # moves cursor right (wraps 5->0)
   Digits validated before applying.
═══════════════════════════════════════════ */

void handleTimeKey(unsigned char k)
{
    unsigned char d;

    /* Cursor left */
    if (k == '*') {
        cur_pos = (cur_pos == 0) ? 5 : cur_pos - 1;
        placeCursor();
        return;
    }

    /* Cursor right */
    if (k == '#') {
        cur_pos = (cur_pos == 5) ? 0 : cur_pos + 1;
        placeCursor();
        return;
    }

    if (k < '0' || k > '9') return;

    d = k - '0';

    switch (cur_pos) {
        case 0: /* Tens of hours: 0-2 */
            if (d > 2) return;
            hh = d * 10 + (hh % 10);
            if (hh > 23) hh = 20;   /* Clamp if units now invalid */
            break;

        case 1: /* Units of hours: capped at 3 when tens=2 */
            if ((hh / 10 == 2) && d > 3) return;
            hh = (hh / 10) * 10 + d;
            break;

        case 2: /* Tens of minutes: 0-5 */
            if (d > 5) return;
            mm = d * 10 + (mm % 10);
            break;

        case 3: /* Units of minutes: 0-9 */
            mm = (mm / 10) * 10 + d;
            break;

        case 4: /* Tens of seconds: 0-5 */
            if (d > 5) return;
            ss = d * 10 + (ss % 10);
            break;

        case 5: /* Units of seconds: 0-9 */
            ss = (ss / 10) * 10 + d;
            break;
    }

    /* Redraw time line immediately after digit entry */
    renderTime();

    /* Advance cursor right, wrap at end */
    cur_pos = (cur_pos == 5) ? 0 : cur_pos + 1;
    placeCursor();
}

/* ═══════════════════════════════════════════
   DATE KEY HANDLER
   cur_pos 0-5 maps to: D1 D2 Mo1 Mo2 Y1 Y2
   * moves cursor left  (wraps 0->5)
   # moves cursor right (wraps 5->0)
   Digits validated before applying.
   Day is clamped if month/year changes reduce max days.
═══════════════════════════════════════════ */

void handleDateKey(unsigned char k)
{
    unsigned char d;
    unsigned char max_d;

    /* Cursor left */
    if (k == '*') {
        cur_pos = (cur_pos == 0) ? 5 : cur_pos - 1;
        placeCursor();
        return;
    }

    /* Cursor right */
    if (k == '#') {
        cur_pos = (cur_pos == 5) ? 0 : cur_pos + 1;
        placeCursor();
        return;
    }

    if (k < '0' || k > '9') return;

    d     = k - '0';
    max_d = daysInMonth(mon, yy);

    switch (cur_pos) {
        case 0: /* Tens of day: 0-3 */
            if (d > 3) return;
            dd = d * 10 + (dd % 10);
            if (dd < 1)      dd = 1;
            if (dd > max_d)  dd = max_d;
            break;

        case 1: /* Units of day */
            {
                unsigned char new_dd = (dd / 10) * 10 + d;
                if (new_dd < 1 || new_dd > max_d) return;
                dd = new_dd;
            }
            break;

        case 2: /* Tens of month: 0-1 */
            if (d > 1) return;
            mon = d * 10 + (mon % 10);
            if (mon < 1)  mon = 1;
            if (mon > 12) mon = 12;
            max_d = daysInMonth(mon, yy);
            if (dd > max_d) dd = max_d;   /* Clamp day */
            break;

        case 3: /* Units of month */
            {
                unsigned char new_mon = (mon / 10) * 10 + d;
                if (new_mon < 1 || new_mon > 12) return;
                mon = new_mon;
                max_d = daysInMonth(mon, yy);
                if (dd > max_d) dd = max_d;
            }
            break;

        case 4: /* Tens of year: 0-9 */
            yy = d * 10 + (yy % 10);
            max_d = daysInMonth(mon, yy);
            if (dd > max_d) dd = max_d;
            break;

        case 5: /* Units of year: 0-9 */
            yy = (yy / 10) * 10 + d;
            max_d = daysInMonth(mon, yy);
            if (dd > max_d) dd = max_d;
            break;
    }

    /* Redraw date line immediately after digit entry */
    renderDate();

    /* Advance cursor right, wrap at end */
    cur_pos = (cur_pos == 5) ? 0 : cur_pos + 1;
    placeCursor();
}

void initState(void)
{
    hh = 0;
    mm = 0;
    ss = 0;

    dd  = 1;
    mon = 1;
    yy  = 0;

    tick_count = 0;
    mode = MODE_RUN;
    cur_pos = 0;
    key = 0;

    time_dirty = 1;
    date_dirty = 1;
}

/* ═══════════════════════════════════════════
   MAIN
═══════════════════════════════════════════ */

void main(void) __naked
{
    initState();
    P0 = 0xFF;
    P3_2 = 1;
    P3_3 = 1;
    /* --- LCD initialisation ---
       Correct order per HD44780 datasheet:
       functionSet -> entryModeSet -> displayOn -> clearDisplay */
    lcdInit();

    /* Draw initial time and date before interrupts start */
    renderTime();
    renderDate();

    /* --- Timer 0 setup ---
       Mode 1 (16-bit), 10ms tick at 11.0592 MHz
       TH0/TL0 = 0xD800 -> 55296 -> 65536-55296 = 10240 cycles ~ 10ms */
    TMOD = (TMOD & 0xF0) | 0x01;
    TH0  = 0xD8;
    TL0  = 0x00;
    ET0  = 1;   /* Enable Timer0 overflow interrupt */
    TR0  = 1;   /* Start Timer0                     */

    /* --- External interrupts ---
       Both edge-triggered (falling edge on SW press) */
    IT0 = 1;    /* INT0 edge triggered */
    IT1 = 1;    /* INT1 edge triggered */
    EX0 = 1;    /* Enable INT0 (SW0 -> P3.2) */
    EX1 = 1;    /* Enable INT1 (SW1 -> P3.3) */

    /* --- Global interrupt enable --- */
    EA  = 1;

    /* =====================
       Main loop
       - Only redraws when dirty flags are set by ISR
       - Only scans keypad when in a setting mode
    ===================== */
    while (1) {

        /* Refresh time display if ISR flagged a tick */
        if (time_dirty && mode == MODE_RUN) {
            time_dirty = 0;
            renderTime();
        }

        /* Refresh date display if date rolled over */
        if (date_dirty && mode == MODE_RUN) {
            date_dirty = 0;
            renderDate();
        }

        /* Keypad only active in setting modes */
        if (mode == MODE_TIME || mode == MODE_DATE) {
            ScanKeyPad();
            if (key != 0) {
                if (mode == MODE_TIME)
                    handleTimeKey(key);
                else
                    handleDateKey(key);
                key = 0;
            }
        }
    }
}