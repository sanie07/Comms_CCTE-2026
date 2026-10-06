################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../USB_DEVICE/Target/usbd_conf.c 

OBJS += \
./USB_DEVICE/Target/usbd_conf.o 

C_DEPS += \
./USB_DEVICE/Target/usbd_conf.d 


# Each subdirectory must supply rules for building sources it contributes
USB_DEVICE/Target/%.o USB_DEVICE/Target/%.su USB_DEVICE/Target/%.cyclo: ../USB_DEVICE/Target/%.c USB_DEVICE/Target/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F722xx -c -I../Core/Inc -I../FATFS/Target -I../FATFS/App -I../USB_DEVICE/App -I../USB_DEVICE/Target -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/Third_Party/FatFs/src -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Device/ST/STM32F7xx/Include -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-USB_DEVICE-2f-Target

clean-USB_DEVICE-2f-Target:
	-$(RM) ./USB_DEVICE/Target/usbd_conf.cyclo ./USB_DEVICE/Target/usbd_conf.d ./USB_DEVICE/Target/usbd_conf.o ./USB_DEVICE/Target/usbd_conf.su

.PHONY: clean-USB_DEVICE-2f-Target

