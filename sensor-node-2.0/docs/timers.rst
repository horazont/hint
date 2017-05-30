TIM2 + TIM3 or TIM1 + TIM2 -> lightsensor processing

this leaves TIM3/TIM1 and TIM4, just two timers

TIM1/TIM3 could be used to drive ADC+DMA for noise sampling, this takes load off the scheduler
TIM4 could be used to drive trigger for accel i2c transfers
