################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
sensors/%.obj: ../sensors/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: Arm Compiler'
	"D:/Softwares/TiShit2/ccs/tools/compiler/ti-cgt-arm_20.2.7.LTS/bin/armcl" -mv7M3 --code_state=16 --float_support=none -me --include_path="C:/Users/yumis/Downloads/empty_CC2650STK_TI_2023/empty_CC2650STK_TI_2023" --include_path="C:/Users/yumis/Downloads/empty_CC2650STK_TI_2023/empty_CC2650STK_TI_2023" --include_path="C:/ti/tirtos_cc13xx_cc26xx_2_21_00_06/products/cc26xxware_2_24_03_17272" --include_path="D:/Softwares/TiShit2/ccs/tools/compiler/ti-cgt-arm_20.2.7.LTS/include" --define=ccs -g --diag_warning=225 --diag_warning=255 --diag_wrap=off --display_error_number --gen_func_subsections=on --abi=eabi --preproc_with_compile --preproc_dependency="sensors/$(basename $(<F)).d_raw" --obj_directory="sensors" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


