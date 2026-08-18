################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/lr_fhss_mac.c \
/home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio.c \
/home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_driver.c \
/home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_fw.c \
/home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/wl_lr_fhss.c 

OBJS += \
./Middlewares/SubGHz_Phy/lr_fhss_mac.o \
./Middlewares/SubGHz_Phy/radio.o \
./Middlewares/SubGHz_Phy/radio_driver.o \
./Middlewares/SubGHz_Phy/radio_fw.o \
./Middlewares/SubGHz_Phy/wl_lr_fhss.o 

C_DEPS += \
./Middlewares/SubGHz_Phy/lr_fhss_mac.d \
./Middlewares/SubGHz_Phy/radio.d \
./Middlewares/SubGHz_Phy/radio_driver.d \
./Middlewares/SubGHz_Phy/radio_fw.d \
./Middlewares/SubGHz_Phy/wl_lr_fhss.d 


# Each subdirectory must supply rules for building sources it contributes
Middlewares/SubGHz_Phy/lr_fhss_mac.o: /home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/lr_fhss_mac.c Middlewares/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WLE5xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../../../Test2_433MHz/Utilities/trace/adv_trace -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../../Test2_433MHz/Utilities/misc -I../../../Test2_433MHz/Utilities/sequencer -I../../../Test2_433MHz/Utilities/timer -I../../../Test2_433MHz/Utilities/lpm/tiny_lpm -I../../../Test2_433MHz/Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../../Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../../Test2_433MHz/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Middlewares/SubGHz_Phy/radio.o: /home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio.c Middlewares/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WLE5xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../../../Test2_433MHz/Utilities/trace/adv_trace -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../../Test2_433MHz/Utilities/misc -I../../../Test2_433MHz/Utilities/sequencer -I../../../Test2_433MHz/Utilities/timer -I../../../Test2_433MHz/Utilities/lpm/tiny_lpm -I../../../Test2_433MHz/Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../../Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../../Test2_433MHz/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Middlewares/SubGHz_Phy/radio_driver.o: /home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_driver.c Middlewares/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WLE5xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../../../Test2_433MHz/Utilities/trace/adv_trace -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../../Test2_433MHz/Utilities/misc -I../../../Test2_433MHz/Utilities/sequencer -I../../../Test2_433MHz/Utilities/timer -I../../../Test2_433MHz/Utilities/lpm/tiny_lpm -I../../../Test2_433MHz/Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../../Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../../Test2_433MHz/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Middlewares/SubGHz_Phy/radio_fw.o: /home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/radio_fw.c Middlewares/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WLE5xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../../../Test2_433MHz/Utilities/trace/adv_trace -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../../Test2_433MHz/Utilities/misc -I../../../Test2_433MHz/Utilities/sequencer -I../../../Test2_433MHz/Utilities/timer -I../../../Test2_433MHz/Utilities/lpm/tiny_lpm -I../../../Test2_433MHz/Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../../Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../../Test2_433MHz/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"
Middlewares/SubGHz_Phy/wl_lr_fhss.o: /home/hernan/Desktop/Comms_CCTE-2026/Software_tests/Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver/wl_lr_fhss.c Middlewares/SubGHz_Phy/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WLE5xx -c -I../Core/Inc -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -I../../../Test2_433MHz/Utilities/trace/adv_trace -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc -I../../../Test2_433MHz/Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -I../../../Test2_433MHz/Utilities/misc -I../../../Test2_433MHz/Utilities/sequencer -I../../../Test2_433MHz/Utilities/timer -I../../../Test2_433MHz/Utilities/lpm/tiny_lpm -I../../../Test2_433MHz/Drivers/CMSIS/Device/ST/STM32WLxx/Include -I../../../Test2_433MHz/Middlewares/Third_Party/SubGHz_Phy/radio_driver -I../../../Test2_433MHz/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Middlewares-2f-SubGHz_Phy

clean-Middlewares-2f-SubGHz_Phy:
	-$(RM) ./Middlewares/SubGHz_Phy/lr_fhss_mac.cyclo ./Middlewares/SubGHz_Phy/lr_fhss_mac.d ./Middlewares/SubGHz_Phy/lr_fhss_mac.o ./Middlewares/SubGHz_Phy/lr_fhss_mac.su ./Middlewares/SubGHz_Phy/radio.cyclo ./Middlewares/SubGHz_Phy/radio.d ./Middlewares/SubGHz_Phy/radio.o ./Middlewares/SubGHz_Phy/radio.su ./Middlewares/SubGHz_Phy/radio_driver.cyclo ./Middlewares/SubGHz_Phy/radio_driver.d ./Middlewares/SubGHz_Phy/radio_driver.o ./Middlewares/SubGHz_Phy/radio_driver.su ./Middlewares/SubGHz_Phy/radio_fw.cyclo ./Middlewares/SubGHz_Phy/radio_fw.d ./Middlewares/SubGHz_Phy/radio_fw.o ./Middlewares/SubGHz_Phy/radio_fw.su ./Middlewares/SubGHz_Phy/wl_lr_fhss.cyclo ./Middlewares/SubGHz_Phy/wl_lr_fhss.d ./Middlewares/SubGHz_Phy/wl_lr_fhss.o ./Middlewares/SubGHz_Phy/wl_lr_fhss.su

.PHONY: clean-Middlewares-2f-SubGHz_Phy

