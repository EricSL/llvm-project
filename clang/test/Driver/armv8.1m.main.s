# RUN: not %clang -c -target arm-none-none-eabi -march=armv8-m.main %s 2>%t
# RUN:      FileCheck --check-prefix=ERROR-V8M < %t %s
# RUN: not %clang -c -target arm-none-none-eabi -march=armv8.1-m.main %s 2>%t
# RUN:      FileCheck --check-prefix=ERROR-V81M < %t %s
# RUN: not %clang -c -target arm-none-none-eabi -march=armv8.1-m.main+dsp %s 2>%t
# RUN:      FileCheck --check-prefix=ERROR-V81M_DSP < %t %s
# RUN: not %clang -c -target arm-none-none-eabi -march=armv8.1-m.main+mve %s 2>%t
# RUN:      FileCheck --check-prefix=ERROR-V81M_MVE < %t %s
# RUN: not %clang -c -target arm-none-none-eabi -march=armv8.1-m.main+mve.fp %s 2>%t
# RUN:      FileCheck --check-prefix=ERROR-V81M_MVEFP < %t %s

.syntax unified
.thumb
.text

csinc r0, r1, r2, eq
# ERROR-V8M: :[[@LINE-1]]:1: error

qadd     r0, r1, r2
# ERROR-V8M: :[[@LINE-1]]:1: error
# ERROR-V81M: :[[@LINE-2]]:1: error
# ERROR-V81M_FP: :[[@LINE-3]]:1: error
# ERROR-V81M_FPDP: :[[@LINE-4]]:1: error

vadd.f16 s0, s1, s2
# ERROR-V8M: :[[@LINE-1]]:1: error
# ERROR-V81M: :[[@LINE-2]]:1: error
# ERROR-V81M_DSP: :[[@LINE-3]]:1: error
# ERROR-V81M_MVE: :[[@LINE-4]]:1: error

vabs.f32 s0, s1
# ERROR-V8M: :[[@LINE-1]]:1: error
# ERROR-V81M: :[[@LINE-2]]:1: error
# ERROR-V81M_DSP: :[[@LINE-3]]:1: error
# ERROR-V81M_MVE: :[[@LINE-4]]:1: error

vcmp.f64 d0,d1
# ERROR-V8M: :[[@LINE-1]]:1: error
# ERROR-V81M: :[[@LINE-2]]:1: error
# ERROR-V81M_DSP: :[[@LINE-3]]:1: error
# ERROR-V81M_MVE: :[[@LINE-4]]:1: error
# ERROR-V81M_MVEFP: :[[@LINE-5]]:1: error

asrl r0, r1, r2
# ERROR-V8M: :[[@LINE-1]]:1: error
# ERROR-V81M: :[[@LINE-2]]:1: error
# ERROR-V81M_DSP: :[[@LINE-3]]:1: error

vcadd.i8 q0, q1, q2, #90
# ERROR-V8M: :[[@LINE-1]]:1: error
# ERROR-V81M: :[[@LINE-2]]:1: error
# ERROR-V81M_DSP: :[[@LINE-3]]:1: error
