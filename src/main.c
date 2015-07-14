#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>

#include "gamma.h"

const uint16_t gamma_lut[4096] =  {GAMMA_LUT};

/*


	TIM2_CCR2 = B;
	TIM2_CCR3 = G;
	TIM3_CCR3 = R;	


*/

#define RMAX 4096
#define GMAX 3500
#define BMAX 2500


void SetPWM(int R, int G, int B) {

	int cR = gamma_lut[ (R*RMAX) >> 12 ];
	int cG = gamma_lut[ (G*GMAX) >> 12 ];
	int cB = gamma_lut[ (B*BMAX) >> 12 ];

	TIM3_CCR3 = cR;	
	TIM2_CCR3 = cG;
	TIM2_CCR2 = cB;
}

void SetupPWM() {

	SetPWM(0,0,0);

	TIM2_CR1 = TIM_CR1_CKD_CK_INT | TIM_CR1_CMS_EDGE;
	/* Period */
	TIM2_ARR = 4096-1;
	/* Prescaler */
	TIM2_PSC = 0;
	TIM2_EGR = TIM_EGR_UG;

	/* Output compare 3 mode and preload */
	TIM2_CCMR1 |= TIM_CCMR1_OC2M_PWM1 | TIM_CCMR1_OC2PE;
	TIM2_CCMR2 |= TIM_CCMR2_OC3M_PWM1 | TIM_CCMR2_OC3PE;

	/* Polarity and state */
	TIM2_CCER |= TIM_CCER_CC2E | TIM_CCER_CC3E;

	/* ARR reload enable */
	TIM2_CR1 |= TIM_CR1_ARPE;

	/* Counter enable */
	TIM2_CR1 |= TIM_CR1_CEN;



	TIM3_CR1 = TIM_CR1_CKD_CK_INT | TIM_CR1_CMS_EDGE;
	/* Period */
	TIM3_ARR = 4096-1;
	/* Prescaler */
	TIM3_PSC = 0;
	TIM3_EGR = TIM_EGR_UG;

	/* Output compare 3 mode and preload */
	TIM3_CCMR2 |= TIM_CCMR2_OC3M_PWM1 | TIM_CCMR2_OC3PE;

	/* Polarity and state */
	TIM3_CCER |= TIM_CCER_CC3E;

	/* ARR reload enable */
	TIM3_CR1 |= TIM_CR1_ARPE;

	/* Counter enable */
	TIM3_CR1 |= TIM_CR1_CEN;


}


static volatile int ms_time_delay;	//WTF!? Why do I have to use static!???

void sys_tick_handler(void) {
	if (ms_time_delay) {
		ms_time_delay--;
	}
}

void sleep_ms(int t) {
	ms_time_delay = t;	while (ms_time_delay);
}

inline void colorHexagon(int hue, int *R, int *G, int *B) {
	int frac = hue >> 12;
	int ci = hue & 0xFFF;
	int cd = 4095 - ci;
	int cs = 4095;
	switch (frac) {
		case 0:	*R = cs;	*G = ci;	*B = 0; break;		//R1	G+	B0
		case 1:	*R = cd;	*G = cs;	*B = 0; break;		//R-	G1	B0
		case 2:	*R = 0;	*G = cs;	*B = ci; break;	//R0	G1	B+
		case 3:	*R = 0;	*G = cd;	*B = cs; break;	//R0	G-	B1
		case 4:	*R = ci;	*G = 0;	*B = cs; break;	//R+	G0	B1
		case 5:	*R = cs;	*G = 0;	*B = cd; break;	//R1	G0	B-
	}
}

static unsigned long xorshf96(void) {    /* A George Marsaglia generator, period 2^96-1 */
	static unsigned long x=123456789, y=362436069, z=521288629;
	unsigned long t;

	x ^= x << 16;
	x ^= x >> 5;
	x ^= x << 1;

	t = x;
	x = y;
	y = z;

	z = t ^ x ^ y;
	return z;
}

static volatile done=0;


void SetupADC() {
	adc_off(ADC1);

	adc_enable_scan_mode(ADC1);	
	adc_set_single_conversion_mode(ADC1);

	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_239DOT5CYC);
	adc_enable_external_trigger_regular(ADC1, ADC_CR2_EXTSEL_SWSTART);
	
	adc_power_on(ADC1);


	sleep_ms(2);	//Sleeping 1 ms may be less than one ms if systick is about to happen, 
	// we actually just need to wait 3µS according to http://libopencm3.github.io/docs/latest/stm32f1/html/group__adc__file.html#ga51f01f6dedbcfc4231e0fc1d8943d956

	adc_reset_calibration(ADC1);
	adc_calibration(ADC1);

	uint8_t channel_array[2];
	channel_array[0] = 3;
	
	adc_set_regular_sequence(ADC1, 1, channel_array);

}

volatile uint16_t adc_samples[16];

int main() {


	rcc_clock_setup_in_hsi_out_24mhz();
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_TIM2);
	rcc_periph_clock_enable(RCC_TIM3);
	rcc_periph_clock_enable(RCC_DMA1);
	rcc_periph_clock_enable(RCC_ADC1);


	//PA11, PA4
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO3);



	ms_time_delay=0;
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(24000-1);  // 1 kHz
	systick_interrupt_enable();
	systick_counter_enable();


	SetupADC();

	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO1 | GPIO2);
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO0);
	SetupPWM();


	//Start sampling
	dma_channel_reset(DMA1, DMA_CHANNEL1);

	dma_set_peripheral_address(DMA1, DMA_CHANNEL1, (uint32_t)&ADC1_DR);
	dma_set_memory_address(DMA1, DMA_CHANNEL1,(uint32_t)adc_samples);
	dma_set_number_of_data(DMA1, DMA_CHANNEL1, 1);
	dma_set_read_from_peripheral(DMA1, DMA_CHANNEL1);
	dma_enable_memory_increment_mode(DMA1, DMA_CHANNEL1);
	dma_enable_circular_mode(DMA1, DMA_CHANNEL1);
	dma_set_peripheral_size(DMA1, DMA_CHANNEL1, DMA_CCR_PSIZE_16BIT);
	dma_set_memory_size(DMA1, DMA_CHANNEL1, DMA_CCR_MSIZE_16BIT);
	dma_set_priority(DMA1, DMA_CHANNEL1, DMA_CCR_PL_VERY_HIGH);


	dma_enable_channel(DMA1, DMA_CHANNEL1);

	dma_enable_transfer_complete_interrupt(DMA1, DMA_CHANNEL1);

	nvic_set_priority(NVIC_DMA1_CHANNEL1_IRQ, 0);
	nvic_enable_irq(NVIC_DMA1_CHANNEL1_IRQ);


	adc_start_conversion_regular(ADC1);
	adc_enable_dma(ADC1);




	int q=0;
	// int R, G, B;
	// int hue=0;
	while(1) {
		// sleep_ms(1);
		// hue = (hue + 1) % (4096*6);
		// colorHexagon(hue, &R, &G, &B);
		// SetPWM(R, G, B);


		// sleep_ms(10);
		// adc_start_conversion_regular(ADC1);

	}

	return 0;
}



void dma1_channel1_isr(void) {
	DMA1_IFCR |= DMA_IFCR_CTCIF1;

	int R, G, B;
	colorHexagon(adc_samples[0]*6, &R, &G, &B);

	SetPWM(R, G, B);

	// int z;
	// if (adc_samples[1] & 2048) {
	// 	z = adc_samples[1] - 2048;
	// 	int zi = 2047 - z;
	// 	SetPWM(((R * zi)>>11)+z, ((G * zi)>>11)+z, ((B * zi)>>11)+z);
	// } else {
	// 	z = adc_samples[1];
	// 	SetPWM((R * z)>>11, (G * z)>>11, (B * z)>>11);
	// }



	adc_start_conversion_regular(ADC1);

}
