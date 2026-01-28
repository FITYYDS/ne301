#include "pir.h"

// Read mode sensor configuration values
uint8_t SENS_C = 0x0f; //[7:0] Sensitivity setting, recommended to set greater than 20. If the environment has no interferpire, it can be set to a minimum value of 10. The smaller the value, the more sensitive, but the easier it is to trigger false alarms. (Effective in interrupt mode, ineffective in read mode)
uint8_t BLIND_C= 0x03; //[3:0] The time to ignore motion detection after the interrupt output switches back to 0, range: 0.5s ~ 8s, interrupt time = register value * 0.5s + 0.5s. (Effective in interrupt mode, ineffective in read mode)
uint8_t PULSE_C= 0x01; //[1:0] Pulse counter, the number of pulses required to be reached within the window time. Range: 1 ~ 4 signed pulses, pulse count = register value + 1. The larger the value, the stronger the anti-interferpire ability, but the sensitivity is slightly reduced. (Effective in interrupt mode, ineffective in read mode)
uint8_t WINDOW_C= 0x00; //[1:0] Window time, range: 2s~8s, window time = register value * 2s + 2s. (Effective in interrupt mode, ineffective in read mode)
uint8_t MOTION_C =0x01; //[0] Must be 1
uint8_t INT_C= 0x00; // Interrupt source. 0 = motion detection, 1 = raw data from the filter. Read mode must be set to 1.
uint8_t VOLT_C =0x00; //[1:0] Multiplex ADC resources. The input sources selectable for the ADC are as follows: PIR signal BFP output = 0; PIR signal LPF output = 1; power supply voltage = 2; temperature sensor = 3; choose as needed
uint8_t SUPP_C = 0x00; // Set to 0
uint8_t RSV_C = 0x00; // Set to 0

static uint8_t SENS_W, BLIND_W, PULSE_W, WINDOW_W, MOTION_W, INT_W, VOLT_W, SUPP_W, RSV_W;
static uint8_t PIR_OUT, DATA_H, DATA_L, SENS_R, BLIND_R, PULSE_R, WINDOW_R, MOTION_R, INT_R;
static uint8_t VOLT_R, SUPP_R, RSV_R, BUF1;

extern void delay_us(uint16_t us);
static void pir_delay_us(uint32_t us)
{
    delay_us(us);
}
static void pir_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

static void pir_trigger_in(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = PIR_TRIGGER_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(PIR_TRIGGER_GPIO_Port, &GPIO_InitStruct);
}

static void pir_trigger_set(uint8_t value)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (value) {
        HAL_GPIO_WritePin(PIR_TRIGGER_GPIO_Port, PIR_TRIGGER_Pin, GPIO_PIN_SET);
        GPIO_InitStruct.Pin = PIR_TRIGGER_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(PIR_TRIGGER_GPIO_Port, &GPIO_InitStruct);
    } else {
        HAL_GPIO_WritePin(PIR_TRIGGER_GPIO_Port, PIR_TRIGGER_Pin, GPIO_PIN_RESET);
        GPIO_InitStruct.Pin = PIR_TRIGGER_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull = GPIO_PULLDOWN;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(PIR_TRIGGER_GPIO_Port, &GPIO_InitStruct);
    }
}

//======== Write NBIT subroutine ====================================
static void W_DATA(uint8_t num)
{
    char i;
    for (i=num; i>0; i--) {   
        PIR_SERIAL_LOW;
        pir_delay_us(2); // Delay must be accurate, total 2us
        PIR_SERIAL_HIGH;
        pir_delay_us(2); // Delay must be accurate, total 2us

        if (BUF1 & 0x80) {
            PIR_SERIAL_HIGH;
        }else{
            PIR_SERIAL_LOW;
        }
        pir_delay_us(75); // Delay must be accurate, total 75us
        BUF1 = BUF1 << 1;
    }
}

//====== Write config to IC ==========================
static void CONFIG_W()
{
    BUF1 = SENS_W;
    W_DATA(8);
    BUF1 = BLIND_W;
    BUF1 = BUF1 << 0x04;
    W_DATA(4);
    BUF1 = PULSE_W;
    BUF1 = BUF1 << 0x06;
    W_DATA(2);
    BUF1 = WINDOW_W;
    BUF1 = BUF1 << 0x06;
    W_DATA(2);
    BUF1 = MOTION_W;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);
    BUF1 = INT_W;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);
    BUF1 = VOLT_W;
    BUF1 = BUF1 << 0x06;
    W_DATA(2);
    BUF1 = SUPP_W;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 0;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 1;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 0;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 0;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    PIR_SERIAL_LOW;
    pir_delay_ms(2);
}

//======= Initialize sensor configuration parameters ==============================
static void CONFIG_INI()
{
    SENS_W = SENS_C;  
    BLIND_W = BLIND_C;
    PULSE_W = PULSE_C;
    WINDOW_W = WINDOW_C;
    MOTION_W = MOTION_C;
    INT_W = INT_C;
    VOLT_W = VOLT_C;
    SUPP_W = SUPP_C;
    RSV_W = RSV_C;
}

//====== Read Nbit ====================
static void RD_NBIT(uint8_t num)
{
    uint8_t i;
    BUF1 = 0x00;
    
    for (i=0; i<num; i++) {
        pir_trigger_set(0);
        pir_delay_us(2);

        pir_trigger_set(1);
        pir_delay_us(2);
        pir_trigger_in();
        pir_delay_us(2);

        BUF1 = BUF1 << 1;
        if (PIR_TRIGGER_READ != 0x00u) {
            pir_delay_us(2);
            if (PIR_TRIGGER_READ != 0x00u) {
                BUF1 = BUF1 + 1;
            }
        }
    }
    return;
}

//======= Read end clear subroutine ==================
static void RD_END()
{
    pir_trigger_set(0);
    pir_delay_us(200); // Delay must be accurate, total 200us
    pir_trigger_in();
}

//===== Force DOCI interrupt subroutine ===============
static void F_INT()
{
    pir_trigger_set(1);
    pir_delay_us(200); // Delay must be accurate, total 200us
}

//===== DOCI read out =======================
static void RD_DOCI()
{
    F_INT();

    PIR_OUT = 0;
    RD_NBIT(1);
    PIR_OUT = BUF1;

    DATA_H = 0x00;
    RD_NBIT(6);
    DATA_H = BUF1;

    DATA_L = 0x00;
    RD_NBIT(8);
    DATA_L = BUF1;

    SENS_R = 0x00;
    RD_NBIT(8);
    SENS_R = BUF1;

    BLIND_R = 0x00;
    RD_NBIT(4);
    BLIND_R = BUF1;

    PULSE_R = 0x00;
    RD_NBIT(2);
    PULSE_R = BUF1;

    WINDOW_R = 0x00;
    RD_NBIT(2);
    WINDOW_R = BUF1;

    MOTION_R = 0x00;
    RD_NBIT(1);
    MOTION_R = BUF1;

    INT_R = 0x00;
    RD_NBIT(1);
    INT_R = BUF1;

    VOLT_R = 0x00;
    RD_NBIT(2);
    VOLT_R = BUF1;

    SUPP_R = 0x00;
    RD_NBIT(1);
    SUPP_R = BUF1;

    RSV_R = 0x00;
    RD_NBIT(4);
    RSV_R = BUF1;

    RD_END(); // Read end clear subroutine
    // printf("PIR_OUT:%x\r\n", PIR_OUT); printf("DATA_H:%x\r\n", DATA_H); printf("DATA_L:%x\r\n", DATA_L); printf("SENS_R:%x\r\n", SENS_R);
    // printf("BLIND_R:%x\r\n", BLIND_R); printf("PULSE_R:%x\r\n", PULSE_R); printf("WINDOW_R:%x\r\n", WINDOW_R); printf("MOTION_R:%x\r\n", MOTION_R);
    // printf("INT_R:%x\r\n", INT_R); printf("VOLT_R:%x\r\n", VOLT_R); printf("SUPP_R:%x\r\n", SUPP_R); printf("RSV_R:%x\r\n", RSV_R);
}

int pir_config(pir_config_t *config)
{
    int retry_times = 0, result = 0;

    if (config != NULL) {
        SENS_C = config->SENS;
        BLIND_C = config->BLIND & 0x0F;
        PULSE_C = config->PULSE & 0x03;
        WINDOW_C = config->WINDOW & 0x03;
        MOTION_C = config->MOTION & 0x01;
        INT_C = config->INT & 0x01;
        VOLT_C = config->VOLT & 0x03;
        SUPP_C = config->SUPP & 0x01;
    }
    // Retry to config the sensor if the write is not correct
    do {
        PIR_SERIAL_LOW;
        pir_trigger_set(0);
        CONFIG_INI();       // Initialize sensor configuration parameters
        CONFIG_W();         // Write config to IC
        // pir_delay_ms(25);   // Delay
        RD_DOCI();          // Read data
        // Check if the write is correct
        if(SENS_W != SENS_R)
        { result = 1; }
        else if(BLIND_W != BLIND_R)
        { result = 2; }
        else if(PULSE_W != PULSE_R)
        { result = 3; }
        else if(WINDOW_W != WINDOW_R)
        { result = 4; }
        else if(MOTION_W != MOTION_R)
        { result = 5; }
        else if(INT_W != INT_R)
        { result = 6; }
        else if(VOLT_W != VOLT_R)
        { result = 7; }
        else if(SUPP_W != SUPP_R)
        { result = 8; }
        // else if(RSV_W != RSV_R)
        // { result = 9; }
        else 
        { result = 0; break;}
    } while (++retry_times < PIR_CONFIG_RETRY_TIMES);

    if (result == 0) {
        pir_trigger_set(0);
        pir_trigger_in();
    }
    return result;
}

void pir_trigger_reset(void)
{
    if (PIR_TRIGGER_READ != 0x00u) {
        pir_delay_ms(10);
        pir_trigger_set(0);
        pir_delay_ms(10);
        pir_trigger_in();
    }
}

