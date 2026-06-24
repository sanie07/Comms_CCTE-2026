################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../SubGHz_Phy/App/app_subghz_phy.c \
../SubGHz_Phy/App/subg_at.c \
../SubGHz_Phy/App/subg_command.c \
../SubGHz_Phy/App/subghz_phy_app.c \
../SubGHz_Phy/App/test_rf.c 

OBJS += \
./SubGHz_Phy/App/app_subghz_phy.o \
./SubGHz_Phy/App/subg_at.o \
./SubGHz_Phy/App/subg_command.o \
./SubGHz_Phy/App/subghz_phy_app.o \
./SubGHz_Phy/App/test_rf.o 

C_DEPS += \
./SubGHz_Phy/App/app_subghz_phy.d \
./SubGHz_Phy/App/subg_at.d \
./SubGHz_Phy/App/subg_command.d \
./SubGHz_Phy/App/subghz_phy_app.d \
./SubGHz_Phy/App/test_rf.d 


# Each subdirectory must supply rules for building sources it contributes
SubGHz_Phy/App/%.o SubGHz_Phy/App/%.su SubGHz_Phy/App/%.cyclo: ../SubGHz_Phy/App/%.c SubGHz_Phy/App/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DCORE_CM4 -DUSE_HAL_DRIVER -DSTM32WLE5xx -DHSE_STARTUP_TIMEOUT=10000 -c -I../Core/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Drivers/STM32WLxx_HAL_Driver/Inc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Drivers/STM32WLxx_HAL_Driver/Inc/Legacy -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Drivers/CMSIS/Device/ST/STM32WLxx/Include -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Drivers/CMSIS/Include -I../SubGHz_Phy/App -I../SubGHz_Phy/Target -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Utilities/trace/adv_trace -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Utilities/misc -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Utilities/sequencer -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Utilities/timer -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Utilities/lpm/tiny_lpm -IC:/Users/sanie/STM32Cube/Repository/STM32Cube_FW_WL_V1.5.0/Middlewares/Third_Party/SubGHz_Phy/radio_driver -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-SubGHz_Phy-2f-App

clean-SubGHz_Phy-2f-App:
	-$(RM) ./SubGHz_Phy/App/app_subghz_phy.cyclo ./SubGHz_Phy/App/app_subghz_phy.d ./SubGHz_Phy/App/app_subghz_phy.o ./SubGHz_Phy/App/app_subghz_phy.su ./SubGHz_Phy/App/subg_at.cyclo ./SubGHz_Phy/App/subg_at.d ./SubGHz_Phy/App/subg_at.o ./SubGHz_Phy/App/subg_at.su ./SubGHz_Phy/App/subg_command.cyclo ./SubGHz_Phy/App/subg_command.d ./SubGHz_Phy/App/subg_command.o ./SubGHz_Phy/App/subg_command.su ./SubGHz_Phy/App/subghz_phy_app.cyclo ./SubGHz_Phy/App/subghz_phy_app.d ./SubGHz_Phy/App/subghz_phy_app.o ./SubGHz_Phy/App/subghz_phy_app.su ./SubGHz_Phy/App/test_rf.cyclo ./SubGHz_Phy/App/test_rf.d ./SubGHz_Phy/App/test_rf.o ./SubGHz_Phy/App/test_rf.su

.PHONY: clean-SubGHz_Phy-2f-App

