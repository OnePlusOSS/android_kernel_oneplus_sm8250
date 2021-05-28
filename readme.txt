***********************************************************************************************************************************
#Please pay attention:
#1. If it occurs a compile error after you copy OnePlus kernel code to Qualcomm source code, which like this:
      "include/vdso/limits.h:5:9: warning: 'USHRT_MAX' macro redefined [-Wmacro-redefined] error, forbidden warning: limits.h:5"
    you should delete the redundant file "include/vdso/" and "include/linux/limits.h" in Qualcomm source code.


#2. We will only submit OP8, OP8Pro and OP8T kernel code on "oneplus/SM8250_R_11.0" branch, so we've reverted the previous submissions
    after synchronizing codes for OnePlus 8T Oxygen OS 11.0.1.2.KB05AA and OnePlus 8 Oxygen OS 11.0.1.1.IN21AA and OnePlus 8 Pro
    Oxygen OS 11.0.1.1.IN11AA on this branch.
***********************************************************************************************************************************


***********************************************************************************************************************************
#Synchronize codes for OnePlus 8T Oxygen OS 11.0.8.11.KB05AA and OnePlus 8 Oxygen OS 11.0.5.5.IN21AA and OnePlus 8 Pro Oxygen OS 11.0.5.5.IN11AA
#================
#1. Optimize camera experience;
#2. Optimize display experience;
#3. Optimize charger experience;
#4. Optimize network experience;
#5. Optimize TP experience.
***********************************************************************************************************************************


***********************************************************************************************************************************
#Synchronize codes for OnePlus 8T Oxygen OS 11.0.8.12.KB05AA and OnePlus 8 Oxygen OS 11.0.6.6.IN21AA and OnePlus 8 Pro Oxygen OS 11.0.6.6.IN11AA
#================
#1. Optimize charger experience;
***********************************************************************************************************************************