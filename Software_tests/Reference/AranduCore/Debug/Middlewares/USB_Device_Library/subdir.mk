################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c \
C:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c \
C:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c \
C:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c 

OBJS += \
./Middlewares/USB_Device_Library/usbd_cdc.o \
./Middlewares/USB_Device_Library/usbd_core.o \
./Middlewares/USB_Device_Library/usbd_ctlreq.o \
./Middlewares/USB_Device_Library/usbd_ioreq.o 

C_DEPS += \
./Middlewares/USB_Device_Library/usbd_cdc.d \
./Middlewares/USB_Device_Library/usbd_core.d \
./Middlewares/USB_Device_Library/usbd_ctlreq.d \
./Middlewares/USB_Device_Library/usbd_ioreq.d 


# Each subdirectory must supply rules for building sources it contributes
Middlewares/USB_Device_Library/usbd_cdc.o: C:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c Middlewares/USB_Device_Library/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F722xx -c -I../Core/Inc -I../FATFS/Target -I../FATFS/App -I../USB_DEVICE/App -I../USB_DEVICE/Target -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/Third_Party/FatFs/src -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Device/ST/STM32F7xx/Include -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Middlewares/USB_Device_Library/usbd_core.o: C:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c Middlewares/USB_Device_Library/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F722xx -c -I../Core/Inc -I../FATFS/Target -I../FATFS/App -I../USB_DEVICE/App -I../USB_DEVICE/Target -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/Third_Party/FatFs/src -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Device/ST/STM32F7xx/Include -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Middlewares/USB_Device_Library/usbd_ctlreq.o: C:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c Middlewares/USB_Device_Library/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F722xx -c -I../Core/Inc -I../FATFS/Target -I../FATFS/App -I../USB_DEVICE/App -I../USB_DEVICE/Target -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/Third_Party/FatFs/src -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Device/ST/STM32F7xx/Include -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
Middlewares/USB_Device_Library/usbd_ioreq.o: C:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c Middlewares/USB_Device_Library/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F722xx -c -I../Core/Inc -I../FATFS/Target -I../FATFS/App -I../USB_DEVICE/App -I../USB_DEVICE/Target -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/Third_Party/FatFs/src -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Core/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Device/ST/STM32F7xx/Include -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Middlewares-2f-USB_Device_Library

clean-Middlewares-2f-USB_Device_Library:
	-$(RM) ./Middlewares/USB_Device_Library/usbd_cdc.cyclo ./Middlewares/USB_Device_Library/usbd_cdc.d ./Middlewares/USB_Device_Library/usbd_cdc.o ./Middlewares/USB_Device_Library/usbd_cdc.su ./Middlewares/USB_Device_Library/usbd_core.cyclo ./Middlewares/USB_Device_Library/usbd_core.d ./Middlewares/USB_Device_Library/usbd_core.o ./Middlewares/USB_Device_Library/usbd_core.su ./Middlewares/USB_Device_Library/usbd_ctlreq.cyclo ./Middlewares/USB_Device_Library/usbd_ctlreq.d ./Middlewares/USB_Device_Library/usbd_ctlreq.o ./Middlewares/USB_Device_Library/usbd_ctlreq.su ./Middlewares/USB_Device_Library/usbd_ioreq.cyclo ./Middlewares/USB_Device_Library/usbd_ioreq.d ./Middlewares/USB_Device_Library/usbd_ioreq.o ./Middlewares/USB_Device_Library/usbd_ioreq.su

.PHONY: clean-Middlewares-2f-USB_Device_Library

