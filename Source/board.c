#include "board.h"

/* Released keypad rows rely on the column pull-ups. Give the lines time
   to recover before the next sample, including the all-rows-released read. */
#define KEYPAD_SETTLE_US 25u

static volatile uint32_t milliseconds;
static uint32_t cycles_per_us;
static volatile bool sample_rx, sample_tx;
static bool adc_ready;
static uint8_t gps_rx[256];
static volatile uint8_t gps_head, gps_tail;

static void clock_init(void)
{
    /* HICK / 2 is the 4 MHz PLL reference. APB1 and APB2 run at 100 MHz. */
    FLASH->psr |= 1u << 4;
    crm_ahb_div_set(CRM_AHB_DIV_1);
    crm_apb1_div_set(CRM_APB1_DIV_2);
    crm_apb2_div_set(CRM_APB2_DIV_2);
    crm_adc_clock_div_set(CRM_ADC_DIV_12);
    crm_pll_config(CRM_PLL_SOURCE_HICK, CRM_PLL_MULT_50,
                   CRM_PLL_OUTPUT_RANGE_GT72MHZ);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
    for (unsigned i = 0; i < 100000 && crm_flag_get(CRM_PLL_STABLE_FLAG) == RESET; ++i)
        __NOP();
    if (crm_flag_get(CRM_PLL_STABLE_FLAG) != RESET) {
        crm_auto_step_mode_enable(TRUE);
        crm_sysclk_switch(CRM_SCLK_PLL);
        while (crm_sysclk_switch_status_get() != CRM_SCLK_PLL)
            __NOP();
        crm_auto_step_mode_enable(FALSE);
    }
    system_core_clock_update();
    cycles_per_us = system_core_clock / 1000000u;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void SysTick_Handler(void)
{
    ++milliseconds;
}

uint32_t board_ms(void)
{
    return milliseconds;
}

void board_delay_ms(uint32_t ms)
{
    uint32_t start = board_ms();
    while (board_ms() - start < ms)
        __WFI();
}

void board_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * cycles_per_us;
    while (DWT->CYCCNT - start < cycles)
        __NOP();
}

void board_pin_mode(gpio_type *port, uint16_t pins, gpio_mode_type mode,
                    gpio_pull_type pull, gpio_output_type output)
{
    gpio_init_type config;
    gpio_default_para_init(&config);
    config.gpio_pins = pins;
    config.gpio_mode = mode;
    config.gpio_pull = pull;
    config.gpio_out_type = output;
    gpio_init(port, &config);
}

static void outputs(gpio_type *port, uint16_t high, uint16_t low)
{
    GPIO_HIGH(port, high);
    GPIO_LOW(port, low);
    board_pin_mode(port, high | low, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_OUTPUT_PUSH_PULL);
}

static void inputs(gpio_type *port, uint16_t pins)
{
    board_pin_mode(port, pins, GPIO_MODE_INPUT, GPIO_PULL_UP, GPIO_OUTPUT_PUSH_PULL);
}

static void gpio_setup(void)
{
    crm_periph_clock_enable(CRM_IOMUX_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOD_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOE_PERIPH_CLOCK, TRUE);
    gpio_pin_remap_config(SWJTAG_GMUX_010, TRUE);

    outputs(GPIOA, PIN(7) | PIN(8) | PIN(11) | PIN(15), PIN(6));
    outputs(GPIOB, PIN(8) | PIN(12), PIN(0) | PIN(1) | PIN(3) | PIN(9));
    outputs(GPIOC, 0, PIN(4) | PIN(5) | PIN(6) | PIN(12) | PIN(13) | PIN(14));
    outputs(GPIOD, PIN(0) | PIN(1) | PIN(2) | PIN(3), 0xff00u);
    outputs(GPIOE, PIN(1) | BK_SELECT_A | BK_SELECT_B | BK_CLOCK | BK_DATA,
            PIN(4) | PIN(7) | PIN(12) | PIN(13) | PIN(14));

    /* Open-drain rows leave all nonselected rows electrically released. */
    GPIO_HIGH(GPIOC, 0x000fu);
    board_pin_mode(GPIOC, 0x000fu, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, GPIO_OUTPUT_OPEN_DRAIN);
    inputs(GPIOA, PIN(10) | PIN(12));
    inputs(GPIOB, PIN(4) | PIN(5) | PIN(11) | PIN(14));
    inputs(GPIOC, PIN(15));
    inputs(GPIOD, 0x00f0u);
    inputs(GPIOE, PIN(0) | PIN(2) | PIN(3) | PIN(5) | PIN(6));
    board_pin_mode(GPIOA, PIN(0) | PIN(1) | PIN(2) | PIN(4) | PIN(5),
                   GPIO_MODE_ANALOG, GPIO_PULL_NONE, GPIO_OUTPUT_PUSH_PULL);
    GPIO_HIGH(GPIOB, PIN(6) | PIN(7));
    board_pin_mode(GPIOB, PIN(6) | PIN(7), GPIO_MODE_OUTPUT,
                   GPIO_PULL_NONE, GPIO_OUTPUT_OPEN_DRAIN);
}

static void backlight_init(void)
{
    crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK, TRUE);
    gpio_pin_remap_config(TMR3_GMUX_0011, TRUE);
    board_pin_mode(GPIOC, PIN(6), GPIO_MODE_MUX, GPIO_PULL_NONE, GPIO_OUTPUT_PUSH_PULL);
    tmr_base_init(TMR3, 999, system_core_clock / 1000000u - 1);
    tmr_output_config_type output;
    tmr_output_default_para_init(&output);
    output.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
    output.oc_output_state = TRUE;
    tmr_output_channel_config(TMR3, TMR_SELECT_CHANNEL_1, &output);
    tmr_counter_enable(TMR3, TRUE);
}

void board_backlight(unsigned percent)
{
    tmr_channel_value_set(TMR3, TMR_SELECT_CHANNEL_1, percent * 10u);
    if (percent)
        GPIO_HIGH(GPIOB, PIN(3));
    else
        GPIO_LOW(GPIOB, PIN(3));
}

static bool adc_init(adc_type *adc, adc_channel_select_type channel)
{
    adc_base_config_type config;
    adc_base_default_para_init(&config);
    config.ordinary_channel_length = 1;
    adc_base_config(adc, &config);
    adc_ordinary_channel_set(adc, channel, 1, ADC_SAMPLETIME_71_5);
    adc_ordinary_conversion_trigger_set(adc, ADC12_ORDINARY_TRIG_SOFTWARE, TRUE);
    adc_enable(adc, TRUE);
    board_delay_us(10);
    adc_calibration_init(adc);
    for (unsigned i = 0; adc_calibration_init_status_get(adc) != RESET; ++i)
        if (i == 10000)
            return false;
    adc_calibration_start(adc);
    for (unsigned i = 0; adc_calibration_status_get(adc) != RESET; ++i)
        if (i == 10000)
            return false;
    return true;
}

static uint16_t adc_read(adc_type *adc)
{
    adc_ordinary_software_trigger_enable(adc, TRUE);
    for (unsigned i = 0; !(adc->sts & ADC_CCE_FLAG); ++i)
        if (i == 2000)
            return AUDIO_IDLE;
    return (uint16_t)adc->odt;
}

static void audio_init(void)
{
    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_ADC2_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_DAC_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_TMR6_PERIPH_CLOCK, TRUE);
    adc_ready = adc_init(ADC1, ADC_CHANNEL_2);
    adc_init(ADC2, ADC_CHANNEL_1);
    DAC->d1dth12r = AUDIO_IDLE;
    DAC->d2dth12r = 0;
    dac_enable(DAC1_SELECT, TRUE);
    dac_enable(DAC2_SELECT, TRUE);

    /* APB1 is divided by two and the timer clock is twice APB1. */
    tmr_base_init(TMR6, (system_core_clock + AUDIO_RATE / 2) / AUDIO_RATE - 1, 0);
    tmr_flag_clear(TMR6, TMR_OVF_FLAG);
    tmr_interrupt_enable(TMR6, TMR_OVF_INT, TRUE);
    NVIC_SetPriority(TMR6_GLOBAL_IRQn, 1);
    NVIC_EnableIRQ(TMR6_GLOBAL_IRQn);
}

void TMR6_GLOBAL_IRQHandler(void)
{
    tmr_flag_clear(TMR6, TMR_OVF_FLAG);
    if (sample_tx)
        DAC->d1dth12r = packet_sample_out();
    else if (sample_rx)
        packet_sample_in(adc_read(ADC1));
}

void board_packet_audio(bool receive, bool transmit)
{
    tmr_counter_enable(TMR6, FALSE);
    NVIC_DisableIRQ(TMR6_GLOBAL_IRQn);
    sample_rx = receive && adc_ready;
    sample_tx = transmit;
    tmr_flag_clear(TMR6, TMR_OVF_FLAG);
    NVIC_ClearPendingIRQ(TMR6_GLOBAL_IRQn);
    DAC->d1dth12r = AUDIO_IDLE;
    if (transmit)
        GPIO_HIGH(GPIOC, PIN(12));
    else
        GPIO_LOW(GPIOC, PIN(12));
    if (sample_rx || sample_tx) {
        tmr_counter_value_set(TMR6, 0);
        NVIC_EnableIRQ(TMR6_GLOBAL_IRQn);
        tmr_counter_enable(TMR6, TRUE);
    }
}

void board_apc(uint16_t level)
{
    DAC->d2dth12r = level;
}

void board_audio(bool acquire, bool speaker)
{
    if (!speaker)
        GPIO_LOW(GPIOE, PIN(4));
    if (acquire || speaker)
        GPIO_LOW(GPIOE, PIN(1));
    else
        GPIO_HIGH(GPIOE, PIN(1));
    if (speaker)
        GPIO_HIGH(GPIOE, PIN(4));
}

uint16_t board_battery_mv(void)
{
    return (uint16_t)((adc_read(ADC2) * 12000u + 2047u) / 4095u);
}

bool board_headset(void)
{
    return !GPIO_READ(GPIOC, PIN(15));
}

bool board_ptt(void)
{
    return (GPIOE->idt & (PIN(3) | PIN(6))) != (PIN(3) | PIN(6));
}

bool board_power_on(void)
{
    return !GPIO_READ(GPIOE, PIN(0));
}

void board_power_release(void)
{
    GPIO_LOW(GPIOA, PIN(11));
}

static Key read_keypad_row(const Key keys[4])
{
    uint32_t columns = ~GPIOD->idt;
    for (unsigned col = 0; col < 4; ++col)
        if (columns & PIN(7 - col))
            return keys[col];
    return KEY_NONE;
}

static Key matrix_key(void)
{
    static const Key matrix[5][4] = {
        {KEY_MENU, KEY_ABC, KEY_EXIT, KEY_VM},
        {KEY_3, KEY_6, KEY_9, KEY_HASH},
        {KEY_2, KEY_5, KEY_8, KEY_0},
        {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT},
        {KEY_1, KEY_4, KEY_7, KEY_STAR}
    };
    Key found = KEY_NONE;
    for (unsigned row = 0; row < 4; ++row) {
        GPIO_LOW(GPIOC, PIN(row));
        board_delay_us(KEYPAD_SETTLE_US);
        Key key = read_keypad_row(matrix[row]);
        GPIO_HIGH(GPIOC, PIN(row));
        if (found == KEY_NONE)
            found = key;
    }

    /* The four keys wired to ground take priority over matrix keys. */
    board_delay_us(KEYPAD_SETTLE_US);
    Key special = read_keypad_row(matrix[4]);
    return special == KEY_NONE ? found : special;
}

Key board_key(void)
{
    static uint8_t encoder;
    static int encoder_steps;
    static const int8_t transitions[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    static Key candidate = KEY_NONE, stable = KEY_NONE;
    static uint32_t changed, repeated;
    uint8_t phase = (GPIOB->idt >> 4) & 3u;
    encoder_steps += transitions[(encoder << 2) | phase];
    encoder = phase;

    Key current = matrix_key();
    if (!GPIO_READ(GPIOE, PIN(2)))
        current = KEY_SK1;
    else if (!GPIO_READ(GPIOE, PIN(5)))
        current = KEY_SK2;
    else if (!GPIO_READ(GPIOA, PIN(12)))
        current = KEY_SK3;

    uint32_t now = board_ms();
    if (current != candidate) {
        changed = now;
        candidate = current;
    } else if (candidate != stable && now - changed >= 20) {
        stable = candidate;
        repeated = now;
        return stable;
    } else if ((stable == KEY_UP || stable == KEY_DOWN || stable == KEY_LEFT ||
                stable == KEY_RIGHT) && candidate == stable &&
                now - changed >= 400 && now - repeated >= 100) {
        repeated = now;
        return stable;
    }

    if (encoder_steps >= 4 || encoder_steps <= -4) {
        Key key = encoder_steps > 0 ? KEY_UP : KEY_DOWN;
        encoder_steps = 0;
        return key;
    }
    return KEY_NONE;
}

static void gps_init(void)
{
    crm_periph_clock_enable(CRM_USART3_PERIPH_CLOCK, TRUE);
    gpio_pin_remap_config(USART3_GMUX_0011, FALSE); /* Default RX on PB11. */
    usart_init(USART3, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
    usart_receiver_enable(USART3, TRUE);
    usart_interrupt_enable(USART3, USART_RDBF_INT, TRUE);
    NVIC_SetPriority(USART3_IRQn, 2);
    NVIC_EnableIRQ(USART3_IRQn);
    usart_enable(USART3, TRUE);
}

void USART3_IRQHandler(void)
{
    uint32_t status = USART3->sts;
    uint8_t byte = (uint8_t)USART3->dt;
    if (status & USART_RDBF_FLAG) {
        uint8_t head = gps_head;
        uint8_t next = (uint8_t)(head + 1);
        if (next != gps_tail) {
            gps_rx[head] = byte;
            gps_head = next;
        }
    }
}

void board_gps_power(bool on)
{
    if (on)
        GPIO_LOW(GPIOA, PIN(8));
    else {
        GPIO_HIGH(GPIOA, PIN(8));
        gps_tail = gps_head;
    }
}

int board_gps_get(void)
{
    uint8_t tail = gps_tail;
    if (tail == gps_head)
        return -1;
    int byte = gps_rx[tail];
    gps_tail = (uint8_t)(tail + 1);
    return byte;
}

void board_init(void)
{
    /* The bootloader holds the supply on PA11 during handoff. */
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    outputs(GPIOA, PIN(11), 0);
    clock_init();
    gpio_setup();
    SysTick_Config(system_core_clock / 1000u);
    __enable_irq();
    backlight_init();
    audio_init();
    gps_init();
}
