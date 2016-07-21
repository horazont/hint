DMA Channels
############

Peripherials which can benefit from DMA:

* NOISE_LEVEL -> ADC
* XBEE_RX -> USARTx_RX
* XBEE_TX -> USARTx_TX
* IMU_RX -> I2Cx_RX
* IMU_TX -> I2Cx_TX
* (ONEWIRE_TX, ONEWIRE_RX)
* LIGHTx_FREQ -> TIMx_CH1

Channels
========

* Channel 1: ADC1, TIM4_CH1
* Channel 2: USART3_TX, TIM1_CH1
* Channel 3: USART3_RX
* Channel 4: USART1_TX, I2C2_TX
* Channel 5: USART1_RX, I2C2_RX
* Channel 6: USART2_RX, I2C1_TX, TIM3_CH1
* Channel 7: USART2_TX, I2C1_RX

Assignment
----------

Channel 1: ADC1 (obviously)
Channel 2: USART3_TX -> XBEE_TX
Channel 3: USART3_RX -> XBEE_RX
Channel 4: USART1_TX -> host TX
Channel 5: USART1_RX -> host RX
Channel 6: TIM3_CH1
Channel 7: I2C1_RX -> IMU_RX
